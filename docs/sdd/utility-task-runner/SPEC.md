# UtilityTaskRunner Specification

## Overview

`UtilityTaskRunner` is a subsystem for executing one-shot, non-streaming model API calls for background utility tasks. In this initial cycle, its sole consumer is **asynchronous conversation-title generation** — automatically generating a more descriptive title for a conversation after the first assistant response completes, replacing the synchronously-derived fallback title. This specification defines the architecture, trigger conditions, failure handling, and UX contract for title generation, with explicit non-goals listed below.

## Glossary

**UtilityTaskRunner**: A subsystem that owns isolated instances of the four provider adapters (Ollama, OpenAI, Anthropic, Google) and orchestrates one-shot, non-streaming model calls for background utility purposes. It is distinct from `ChatViewModel`'s provider instances, which handle real-time conversational chat.

**ModelProfile / Model Resolution Chain**: A three-tier fallback mechanism for selecting which provider and model to use for a utility task: (1) per-task override (not implemented in this cycle), (2) `default_utility_model` from `UtilityConfig` in `config.json` (if set), (3) the conversation's currently active chat provider and model (always present as fallback). The chain always resolves to a usable model.

**TitleSource**: An enum with three states — `Fallback`, `Generated`, `Manual` — persisted per conversation to the SQLite database and stored in the `conversations` table `title_source` column. Controls whether the title is eligible for async generation.

**Fallback Title**: The synchronous, deterministic title derived by `deriveConversationTitle(firstMessageText)` in `holonight_domain::Conversation` — first line of the text, or first 48 characters, whichever is shorter; "Untitled" if empty. Shown immediately when a conversation is created; replaced if generation succeeds.

---

## Requirements

### Architecture & Isolation

#### REQ-NF-001: Provider Instance Separation
**Statement:** The UtilityTaskRunner shall own separate instances of all four provider adapters (`OllamaProvider`, `OpenAIProvider`, `AnthropicProvider`, `GoogleProvider`) distinct from those owned by `ChatViewModel`.

**Rationale:** Provider instances have stateful setters (`setTemperature`, `setMaxOutputTokens`, `setContextWindow`) that apply to all subsequent calls. Isolation prevents utility-task settings from contaminating real-chat calls and vice versa.

**Acceptance criteria:**
- UtilityTaskRunner instantiates its own four provider objects in its constructor
- UtilityTaskRunner providers are not references to `ChatViewModel`'s providers
- Each UtilityTaskRunner provider instance can be configured independently without affecting chat-traffic providers

#### REQ-NF-002: Credential Wiring for Utility Providers
**Statement:** UtilityTaskRunner's provider instances shall be wired to the same Secret Service credential store as `ChatViewModel`'s providers, using the same `ProviderRuntimeCoordinator` async-credential-retrieval pattern.

**Rationale:** Cloud providers (OpenAI, Anthropic, Google) require authentication; title generation against them must use the same stored credentials as live chat.

**Acceptance criteria:**
- Utility providers receive credential tokens from Secret Service on the same retrieval path as chat providers
- If a user has not stored credentials for a provider (e.g., no OpenAI API key), a generation attempt using that provider fails gracefully without crashing
- Utility providers are notified of credential updates (add, change, delete) via the same callback chain as chat providers

#### REQ-C-001: No Settings UI in This Cycle
**Statement:** The UtilityTaskRunner subsystem shall not implement a settings UI for configuring the default utility model.

**Rationale:** Settings UI is deferred to a later cycle. Configuration is stored in `config.json` only.

**Acceptance criteria:**
- No QML/UI components exist for selecting or editing `default_utility_model`
- Configuration is only readable from / writable to `holonight_config::UtilityConfig` in-memory and `config.json` on disk

---

### Configuration

#### REQ-F-001: UtilityConfig Structure
**Statement:** The holonight_config module shall define a `UtilityConfig` struct with a single field `default_utility_model` (ModelId-shaped: `{provider_id, model_name}`) that is persisted to `config.json` with default empty/unset value.

**Acceptance criteria:**
- `UtilityConfig` struct exists in `holonight_config` with same pattern as `OllamaProviderConfig`, `OpenAIProviderConfig`, etc.
- `default_utility_model` field can hold a provider ID and model name, or be empty/unset
- `config.json` round-trips `UtilityConfig` without data loss
- Default value (unset) permits title generation with fallback-model resolution

#### REQ-F-002: Model Resolution Chain
**Statement:** When resolving which model to use for a utility task, the UtilityTaskRunner shall apply the fallback chain: per-task override (if provided, reserved for future use) → `default_utility_model` from `UtilityConfig` (if set) → the conversation's currently active chat provider and model.

**Acceptance criteria:**
- If `default_utility_model` is set in `UtilityConfig`, that model is used (assuming provider credentials are available)
- If `default_utility_model` is unset, the conversation's chat provider and model are used
- The conversation's chat provider and model always exist as a fallback (no resolution-to-null case)
- Per-task override parameter exists in the API but raises NotImplementedError or similar if called (acknowledged as future work)

---

### Trigger & State Management

#### REQ-F-003: Generate Title After First Assistant Response Completes
**Statement:** When a `Message` in any conversation reaches status `Complete` AND the message's role is `assistant` AND the conversation's `title_source` column is `Fallback`, the UtilityTaskRunner shall issue a title-generation request.

**Rationale:** Title generation is deferred until the first assistant response is fully received, ensuring there is meaningful content to generate from.

**Acceptance criteria:**
- Title generation fires exactly once per conversation, on the first assistant `Complete` message
- If the user manually renames a conversation before that message completes, title generation does not fire (title_source is `Manual`)
- If the first assistant message is never received (e.g., conversation abandoned by user), title generation never fires
- Checking `title_source == Fallback` prevents re-generation of already-titled conversations (idempotency)

#### REQ-F-004: Title Generation Input
**Statement:** The title-generation prompt shall use as input the first user message text and the first assistant response text only.

**Acceptance criteria:**
- Title-generation request includes both first user message and first assistant response
- No message attachments, system messages, or other message properties are included (none exist in this codebase)
- If the first user message is empty or missing, generation still proceeds with available text
- Generation request includes both texts even if one or both are very short

#### REQ-F-005: TitleSource Column Persistence
**Statement:** The `conversations` table in SQLite shall include a `title_source` column of type TEXT (`Fallback`, `Generated`, or `Manual`).

**Acceptance criteria:**
- `title_source` column exists and is readable on all rows
- Valid values are exactly `Fallback`, `Generated`, `Manual`
- Database enforces or validates this enum (constraint or application-level check)

#### REQ-F-006: Manual Rename Sets TitleSource to Manual
**Statement:** When `ChatViewModel::renameConversation(id, newTitle)` is called, the system shall set the conversation's `title_source` to `Manual`.

**Acceptance criteria:**
- Calling `renameConversation()` updates both the `title` and `title_source` columns
- After a manual rename, `title_source` is `Manual` and remains so indefinitely
- Subsequent assistant messages do not reset `title_source` to `Fallback` or trigger re-generation

#### REQ-NF-003: Title Update Is Asynchronous
**Statement:** While a title-generation request is in flight, the conversation's UI shall remain responsive and the user shall continue to see the fallback title; when generation completes, the new title shall replace the fallback without blocking or reloading the conversation.

**Rationale:** Title generation is a low-stakes background task; network latency or provider delay must not freeze the chat UI.

**Acceptance criteria:**
- Title generation is issued as a fire-and-forget task on a background thread or event queue
- Fallback title is visible immediately and remains visible until generation completes
- If generation succeeds, UI updates asynchronously to show the new title (no window refresh required)
- User can interact with the conversation (send messages, read chat) during title generation

---

### Failure Handling

#### REQ-F-007: Single Generation Attempt
**Statement:** UtilityTaskRunner shall issue exactly one generation request per conversation and shall not retry on any failure (network error, empty response, auth failure, unresolvable model, invalid config, etc.).

**Acceptance criteria:**
- A conversation eligible for generation (title_source == `Fallback`) results in at most one title-generation API call
- If generation fails (timeout, 4xx/5xx error, no response, empty response), no automatic or queued retry occurs
- Fallback title is retained permanently for that conversation if generation fails

#### REQ-F-008: Failure Does Not Require User Notification
**Statement:** If title generation fails, the system shall silently retain the fallback title with no user-facing error message, dialog, or notification.

**Rationale:** Title generation is a low-stakes background enhancement; failure is not actionable by the user.

**Acceptance criteria:**
- No error dialog, toast, or alert is shown if generation fails
- No error entry is written to a user-visible event log or notification center
- System logs may record the failure for debugging; application logs do not surface it to the UI

#### REQ-C-002: No Automatic Retry or Queued Retry
**Statement:** The UtilityTaskRunner shall not implement automatic retry, scheduled retry, or a retry queue for failed title-generation attempts.

**Rationale:** Retry logic is deferred to a future cycle if retry becomes necessary.

**Acceptance criteria:**
- No retry timer, retry queue, or exponential backoff mechanism exists
- Failed generations are not enqueued for later retry
- If user manually triggers a re-generation in a future cycle (via a "Regenerate Title" button, not yet implemented), that is a separate requirement

#### REQ-U-001: Unvalidated Config Does Not Crash
**Statement:** If `default_utility_model` in `config.json` names an unknown `provider_id` or unsupported `model_name`, then the system shall fail gracefully: skip title generation, retain the fallback title, and log the resolution error without crashing the application.

**Acceptance criteria:**
- Unknown `provider_id` is detected and logged; generation attempt is skipped
- Unsupported `model_name` (e.g., model does not exist on that provider) is detected during provider API call; generation fails and fallback is kept
- Application continues to function normally after the error (no unhandled exception, no process exit)
- Error is logged with enough detail to help the user diagnose the config mistake in `config.json`

#### REQ-U-002: Empty or Invalid First Messages Do Not Cause Crashes
**Statement:** If the first user message or first assistant response is empty, very short, malformed, or missing, then the system shall still attempt title generation with available text rather than skipping generation.

**Acceptance criteria:**
- Empty first user message does not skip generation (uses only first assistant response)
- Empty first assistant response does not skip generation (uses only first user message)
- Both messages missing/empty does not crash; generation is attempted and likely fails gracefully (provider returns empty or error)
- Malformed UTF-8 or special characters in messages do not crash; they are passed to the provider as-is

---

### Data Migration

#### REQ-F-009: TitleSource Column Migration
**Statement:** A new SQLite migration `0003_add_title_source.sql` shall add the `title_source` column to the `conversations` table with type TEXT.

**Acceptance criteria:**
- Migration file `src/persistence/migrations/0003_add_title_source.sql` exists and is numbered correctly after existing migrations
- Migration adds `title_source` column as TEXT or similar string type
- Migration is idempotent (running it twice does not error)

#### REQ-F-010: Backfill Existing Conversations with Manual
**Statement:** Migration `0003_add_title_source.sql` shall backfill all pre-existing conversation rows (created before this feature ships) with `title_source = 'Manual'`.

**Rationale:** Prevents retroactive title changes to old conversations via surprise provider calls. Only conversations created after the migration are born with `title_source = 'Fallback'` and are eligible for generation.

**Acceptance criteria:**
- All rows in `conversations` table receive `title_source = 'Manual'` during migration
- Conversations created after the migration (by the new code) receive `title_source = 'Fallback'` on insert
- A pre-existing conversation's title is never automatically replaced, even if the fallback-title logic would generate a better one
- New conversations are eligible for generation (title_source == `Fallback` at creation)

#### REQ-C-003: New Conversations Default to Fallback TitleSource
**Statement:** When a new `Conversation` is created and inserted into the SQLite database, the system shall set `title_source = 'Fallback'`.

**Acceptance criteria:**
- `ConversationRepository::create()` or insert logic sets `title_source` to `Fallback` for new rows
- Title is set to the fallback value via `deriveConversationTitle()` (existing behavior)
- Backfilled pre-existing rows retain `Manual` (from REQ-F-010)

---

### Non-Functional Guarantees

#### REQ-NF-004: Idempotency
**Statement:** The UtilityTaskRunner shall be idempotent with respect to title generation: if a title-generation attempt has already completed (either succeeded or failed), subsequent attempts to generate a title for the same conversation shall be a no-op.

**Rationale:** Prevents duplicate generations, duplicate API calls, or race conditions if the same conversation is processed twice.

**Acceptance criteria:**
- If `title_source != Fallback` (i.e., already `Generated` or `Manual`), no generation request is issued
- Generating a title updates `title_source` to `Generated` atomically with the title update
- Calling title generation again for a conversation with `title_source == Generated` does nothing

#### REQ-NF-005: Concurrent First-Message Completion Handled
**Statement:** If multiple messages in the same conversation reach `Complete` status concurrently or in rapid succession (user sends message, gets response, provider latency causes arrival timing ambiguity), the system shall fire at most one title-generation request.

**Rationale:** The implementation must guard against race conditions in trigger evaluation.

**Acceptance criteria:**
- Title generation fires exactly once per conversation even if multiple `Complete` messages arrive in quick succession
- The trigger evaluation (checking `title_source == Fallback` and `role == assistant`) is atomic or protected by synchronization
- No duplicate generation requests are issued for the same conversation

#### REQ-NF-006: No Ollama Scheduling or Priority Coordination
**Statement:** UtilityTaskRunner shall not attempt to detect, prioritize, or avoid concurrent-model-load contention on a shared Ollama endpoint.

**Rationale:** Ollama scheduling is deferred to a future cycle. Title generation proceeds without coordination.

**Acceptance criteria:**
- No check for Ollama queue depth or active-model status before issuing a title-generation request
- If a user is running a real-time chat on Ollama simultaneously with title generation, there is no scheduling logic to defer one or the other
- This is noted as an explicit non-goal (not an oversight)

---

### Cross-Provider Behavior

#### REQ-C-004: No Cross-Provider Privacy Gate in This Cycle
**Statement:** If `default_utility_model` specifies a different provider than the conversation's chat provider, the system shall proceed with title generation without warning, confirmation, or blocking gate.

**Rationale:** Cross-provider privacy considerations are deferred to a future cycle. In v1, user config is trusted.

**Acceptance criteria:**
- Chat provider is Ollama (on-device), but `default_utility_model` is OpenAI (cloud) — generation proceeds without prompting
- No confirmation dialog, tooltip, or warning is shown to the user
- No audit log or privacy-event marker is recorded (future feature)

---

## Non-Goals

The following features and concerns are **explicitly out of scope** for this cycle and are deferred to future cycles:

| Feature | Reason | Future Cycle |
|---------|--------|--------------|
| **Conversation Compaction** | Not a consumer of UtilityTaskRunner in this cycle; requires separate design for summarizing conversation history. | v2.0+ |
| **Settings UI for Default Utility Model** | Configuration exists in `config.json` only; a QML settings screen is deferred. | v1.1+ |
| **Per-Task Model Overrides** | Model resolution chain is hard-coded (config → chat model fallback); per-task overrides are reserved for future API expansion. | v2.0+ |
| **Automatic Retry or Retry Queue** | Title generation is one-shot; retry logic is deferred. | v1.1+ |
| **Cross-Provider Privacy Gate** | No prompt/warning/block if utility model is from a different provider than chat provider. | v2.0+ |
| **Ollama Scheduling / Priority Coordination** | No detection or avoidance of concurrent-model-load contention on shared Ollama endpoints. | v2.0+ |
| **User Notification of Failures** | Failed generations are silent (logged only); user-facing error reporting is deferred. | v1.1+ |
| **Regenerate Title Button / Manual Retry Trigger** | No UI for the user to manually trigger re-generation; only automatic generation on first assistant response is supported. | v1.1+ |

---

## Summary

This specification defines a minimal, focused `UtilityTaskRunner` subsystem for asynchronous conversation-title generation. The system resolves the model to use via a fallback chain, fires exactly once per conversation after the first assistant response completes, retains the fallback title on any failure, and uses a `TitleSource` enum to ensure manual renames are never overwritten. Provider isolation, credential wiring, and idempotent state management prevent conflicts with real-time chat traffic. Retry logic, cross-provider privacy gates, and Ollama scheduling are explicit non-goals deferred to future cycles.

---

## Requirement Summary

- **Functional Requirements**: REQ-F-001 through REQ-F-010 (10 total)
- **Non-Functional Requirements**: REQ-NF-001 through REQ-NF-006 (6 total)
- **Constraints**: REQ-C-001 through REQ-C-004 (4 total)
- **Unwanted Behaviour**: REQ-U-001 through REQ-U-002 (2 total)

**Total: 22 Requirements**
