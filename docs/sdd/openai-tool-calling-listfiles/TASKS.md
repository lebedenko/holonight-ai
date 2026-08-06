# OpenAI Tool Calling with ListFiles — Tasks

- [x] Document requirements, wire format, security constraints, and edge cases.
- [x] Add the OpenAI tool codec for definitions, history reconstruction, ordered calls, and reasoning replay.
- [x] Gate request tools and encrypted reasoning inclusion on a non-empty catalog.
- [x] Decode completed output, validate all calls, and emit calls before completion.
- [x] Persist provider item IDs and opaque context without a schema migration.
- [x] Add the opt-in OpenAI configuration, controller property, and settings switch.
- [x] Route the registry catalog only to enabled OpenAI instances.
- [x] Make retry usage persistence idempotent and transport timeout text provider-neutral.
- [x] Pass focused and full verification.
