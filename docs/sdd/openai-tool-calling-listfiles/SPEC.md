# OpenAI Tool Calling with ListFiles — Specification

## Goal

Allow an explicitly enabled OpenAI provider instance to invoke the existing local `ListFiles` tool through the
Responses API while preserving the existing execution, transcript, and filesystem security policies.

## Requirements

- Tool calling defaults off and is configured per OpenAI instance.
- Enabled instances receive the registry snapshot; disabled instances and instances without a registry do not.
- Requests remain stateless (`store: false`, no `previous_response_id`).
- Function definitions use Responses API function-tool fields: `type`, `name`, `description`, and `parameters`.
- Completed responses may produce multiple ordered calls. A missing name/call ID or non-object JSON arguments is a
  terminal error and must not reach execution.
- Follow-up input chronologically reconstructs messages, function calls, compact-JSON outputs, and opaque encrypted
  reasoning items. Opaque items are deduplicated by provider item ID and never rendered.
- Provider item IDs and opaque context round-trip additively in the existing `tool_calls` JSON column; legacy rows
  and configs retain safe defaults.
- Retrying or regenerating an assistant message replaces its existing usage record because message identity is
  intentionally preserved. A failed first attempt must not prevent successful usage statistics from being persisted.
- Shared transport errors use provider-neutral wording; an OpenAI timeout must never be labeled as an Ollama timeout.
- Existing limits remain unchanged: ten local calls per turn, home-directory-only access, visible tool activity, and
  no new approval or filesystem capability.

## Out of scope

Hosted tools, MCP tools, free-form tools, approvals, and tools other than the existing local registry contents.

## Manual acceptance

Verified with both `o4-mini` and `gpt-5.6-sol`: `ListFiles` executes, the follow-up answer is persisted, token usage is
shown, and retrying after a timeout no longer produces a duplicate-usage error.
