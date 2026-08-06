# OpenAI Tool Calling with ListFiles — Design

`OpenAIToolCodec` is the wire-format boundary. It encodes registry definitions and persisted conversation history,
and decodes the complete `response.output` attached to `response.completed`. Calls are emitted only at completion so
their argument JSON and encrypted reasoning context are complete. All calls are validated before any event is emitted,
preventing partial execution when a later parallel call is malformed.

Reasoning output items are stored unchanged as provider-neutral opaque JSON on invocation records. Their output item
IDs and function-call item IDs are additive fields in the existing JSON persistence payload, requiring no migration.
History encoding maintains chronological message/tool order and suppresses duplicate reasoning items by ID.

The provider adds `tools` and `include: ["reasoning.encrypted_content"]` only for a non-empty catalog. Consequently the
legacy no-tools body remains byte-shape compatible. The router supplies that catalog only when the OpenAI instance's
opt-in flag is true and a registry exists. All execution continues through `ToolOrchestrator` and `ListFilesTool`, so
the existing call cap and home-directory policy remain authoritative.

Malformed calls fail the stream with a readable error before `Completed`; the controller therefore cannot execute
them. Opaque context is never exposed through transcript projection.

Regeneration preserves the assistant message ID. Usage persistence therefore uses an SQLite upsert keyed by
`message_id`, replacing a timeout attempt's partial usage with the successful attempt's complete token statistics.
This maintains the one-row-per-message invariant without a migration. The shared streaming HTTP client reports idle
timeouts without naming a provider.
