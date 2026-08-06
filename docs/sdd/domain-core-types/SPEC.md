# Domain Core Types Specification

**Document Version**: 1.0
**Date**: 2026-07-21
**Module**: `holonight_domain` (core value types)
**Language/Standard**: C++23, Qt6::Core
**Status**: Implemented

---

## Executive Summary

This specification defines the foundational value types for the `holonight-ai` domain layer: `Message`, `Conversation`, `ModelId`, and `StreamEvent`. These types model in-memory conversations and message lifecycles with validated state transitions, serving as the contract that all downstream modules (`application`, `providers`, `persistence`) will depend on. This cycle implements only headlessly-testable C++23 value types; serialization, persistence, provider adapters, and UI integration are out of scope.

---

## Non-Goals (Explicit Out-of-Scope)

The following are explicitly NOT addressed in this specification or implementation:

- JSON serialization or deserialization of domain types
- SQLite or any persistence layer integration
- Network code, provider adapters (Ollama, OpenAI, Anthropic, Google)
- QML/UI integration or bindings
- `ToolCall` and `Usage` variants for `StreamEvent` (deferred to provider-capability cycle)
- Model capability metadata (e.g. context window limits, streaming support) — that is a future provider registry concern
- Thread safety or concurrent access guarantees beyond value semantics

---

## Module Layout

**Namespace**: `holonight_domain`

**Headers** (under `src/domain/include/holonight_domain/`):
- `message.h` — `MessageId`, `Message`, and role/status enums
- `conversation.h` — `ConversationId`, `Conversation`
- `model_id.h` — `ModelId`
- `stream_event.h` — `StreamEvent` and variant types

**Implementation** (under `src/domain/src/`):
- `.cpp` files as needed (may be header-only depending on C++23 inline capabilities)

**Build Target**:
- `src/domain/CMakeLists.txt` changes from `INTERFACE` to `STATIC` library
- Links Qt6::Core only (for `QUuid`, `QString`, `QDateTime`)
- No external dependencies beyond standard library and Qt6::Core

---

## Requirements

### Message Type

#### REQ-F-001: MessageId as Opaque UUID Type

**Requirement** (Ubiquitous):
The system shall provide a `MessageId` type that wraps a UUID-formatted string internally and prevents direct construction from arbitrary caller-supplied strings.

**Acceptance Criteria**:
- `MessageId` is a distinct type (not a type alias for `QString` or `std::string`)
- `MessageId` instances are constructed via a static factory method (e.g. `MessageId::generate()`) that internally calls `QUuid::createUuid()` and formats it as a string
- The underlying UUID string is accessible via a read-only method (e.g. `toString()` or `value()`)
- Attempting to construct a `MessageId` from an arbitrary string at compile-time or runtime is not part of the public API

---

#### REQ-F-002: Message Role Enumeration

**Requirement** (Ubiquitous):
The system shall represent a `Message`'s role as a scoped enumeration with three values: `System`, `User`, and `Assistant`.

**Acceptance Criteria**:
- An enum class named `MessageRole` or similar exists in the `holonight_domain` namespace
- It defines exactly three enumerators: `System`, `User`, `Assistant` (order unspecified)
- A `Message` instance holds a `MessageRole` field and exposes it via accessor

---

#### REQ-F-003: Message Status Enumeration

**Requirement** (Ubiquitous):
The system shall represent a `Message`'s lifecycle status as a scoped enumeration with five values: `Pending`, `Streaming`, `Complete`, `Error`, and `Cancelled`.

**Acceptance Criteria**:
- An enum class named `MessageStatus` or similar exists in the `holonight_domain` namespace
- It defines exactly five enumerators: `Pending`, `Streaming`, `Complete`, `Error`, `Cancelled`
- A `Message` instance holds a `MessageStatus` field and exposes it via accessor
- The default status of a newly constructed `Message` is `Pending`

---

#### REQ-F-004: Message Field Composition

**Requirement** (Ubiquitous):
The system shall provide a `Message` value type with five fields: `id` (MessageId), `role` (MessageRole), `text` (string), `status` (MessageStatus), and an implementation-level default constructor that assigns `Pending` status and empty text.

**Acceptance Criteria**:
- A class named `Message` exists in the `holonight_domain` namespace
- It is constructible with parameters for `id`, `role`, `text`, and optionally `status` (defaulting to `Pending`)
- All five fields are accessible via named accessors (member functions, not public data members)
- Text content is stored as `QString` or `std::string` (design choice)
- A default-constructed `Message` is valid (though likely a design antipattern; exact decision deferred to Design phase)

---

#### REQ-F-005: Message Plain-Text Content Only

**Requirement** (Ubiquitous):
The system shall accept only plain text in a `Message`'s content field; attachment data, tool call metadata, and usage statistics are explicitly NOT stored in the `Message` type.

**Acceptance Criteria**:
- The `text` field of `Message` is a single string (no array/list structure)
- No `attachments`, `tool_calls`, or `usage` members exist on the `Message` type
- Attempting to set or query such fields results in a compile-time error (not a runtime null or undefined behavior)

---

#### REQ-F-006: Message Status Transition Validation

**Requirement** (State-driven):
While a `Message` is in the `Pending` state, the system shall allow transition to `Streaming`; while `Streaming`, transition to `Complete`, `Error`, or `Cancelled` is allowed; once in any of `{Complete, Error, Cancelled}` states, the system shall NOT permit any further state changes.

**Acceptance Criteria**:
- A method exists to transition `Message` status (e.g. `setStatus()`, `transitionTo()`, or equivalent)
- `Pending` → `Streaming` succeeds without rejection
- `Streaming` → `Complete` succeeds
- `Streaming` → `Error` succeeds
- `Streaming` → `Cancelled` succeeds
- `Pending` → `Complete` is rejected (illegal transition)
- `Pending` → `Error` is rejected
- `Pending` → `Cancelled` is rejected
- `Complete` → `Streaming` is rejected
- `Error` → `Streaming` is rejected
- `Cancelled` → `Streaming` is rejected
- Invalid transitions result in a detectably rejected state (mechanism—exception, `std::expected`, assertion—is a Design-phase decision; the requirement is that the invalid transition is NOT silently accepted)

---

#### REQ-F-007: Message Equality Comparison

**Requirement** (Ubiquitous):
The system shall support value equality comparison (`operator==`) for `Message` instances, such that two messages are equal if and only if all five fields (id, role, text, status) are equal.

**Acceptance Criteria**:
- `operator==` is defined for `Message`
- Two `Message` instances with identical fields compare equal (`lhs == rhs` returns true)
- Two `Message` instances differing in any field compare unequal
- Inequality (`operator!=`) is also defined and returns the logical negation of `==`

---

### Conversation Type

#### REQ-F-008: ConversationId as Opaque UUID Type

**Requirement** (Ubiquitous):
The system shall provide a `ConversationId` type that wraps a UUID-formatted string internally and prevents direct construction from arbitrary caller-supplied strings, following the same pattern as `MessageId`.

**Acceptance Criteria**:
- `ConversationId` is a distinct type (not a type alias)
- Instances are constructed via a static factory method (e.g. `ConversationId::generate()`) that internally calls `QUuid::createUuid()`
- The underlying UUID string is accessible via a read-only method
- Attempting to construct a `ConversationId` from an arbitrary string is not part of the public API

---

#### REQ-F-009: Conversation Field Composition

**Requirement** (Ubiquitous):
The system shall provide a `Conversation` value type with fields: `id` (ConversationId), `title` (string), `createdAt` (timestamp), `updatedAt` (timestamp), and an ordered list of `Message` instances.

**Acceptance Criteria**:
- A class named `Conversation` exists in the `holonight_domain` namespace
- It is constructible with parameters for `id`, `title`, and `createdAt` (with sensible defaults or factory methods)
- `createdAt` and `updatedAt` are `QDateTime` or platform-independent timestamp values
- All five attributes are accessible via named accessors
- The message list is accessible and queryable (e.g. size, iteration)

---

#### REQ-F-010: Conversation Message Insertion Order Preservation

**Requirement** (Ubiquitous):
The system shall store messages within a `Conversation` in an ordered list that preserves insertion order; messages appended at different times must appear in that temporal sequence when iterated.

**Acceptance Criteria**:
- A method exists to append a message to a `Conversation` (e.g. `addMessage()`, `appendMessage()`, or equivalent)
- After appending three messages M1, M2, M3 in that order, iterating or indexing the conversation's message list yields M1, then M2, then M3
- The internal storage mechanism must guarantee order preservation (e.g. `std::vector`, not an unordered map or set)

---

#### REQ-F-011: Conversation UpdatedAt Synchronization

**Requirement** (Event-driven):
When a message is appended to a `Conversation`, the system shall update the `updatedAt` timestamp to the current time.

**Acceptance Criteria**:
- Before appending a message, note the `Conversation`'s `updatedAt` value (time T1)
- Append a message and wait a measurable time interval (e.g. at least 1 millisecond)
- After appending, the `updatedAt` value (time T2) is strictly later than T1

---

#### REQ-F-012: Conversation Equality Comparison

**Requirement** (Ubiquitous):
The system shall support value equality comparison (`operator==`) for `Conversation` instances, such that two conversations are equal if and only if all attributes (id, title, createdAt, updatedAt) and their message lists are equal in order.

**Acceptance Criteria**:
- `operator==` is defined for `Conversation`
- Two `Conversation` instances with identical attributes and message lists compare equal
- Two conversations differing in id, title, timestamps, or message list (order or content) compare unequal
- Message order is significant: a `Conversation` with messages [M1, M2] is not equal to one with [M2, M1]

---

### ModelId Type

#### REQ-F-013: ModelId Minimal Value Type

**Requirement** (Ubiquitous):
The system shall provide a `ModelId` value type holding exactly two fields: `providerId` (string) and `modelName` (string), with no capability metadata, token limits, or provider-specific features.

**Acceptance Criteria**:
- A class or struct named `ModelId` exists in the `holonight_domain` namespace
- It stores a provider identifier (e.g. "ollama", "openai", "anthropic") as a string
- It stores a model name (e.g. "gpt-4-turbo", "claude-3-sonnet") as a string
- Both fields are accessible via named accessors
- No fields for context window, streaming capability, cost, latency, or other metadata exist

---

#### REQ-F-014: ModelId Equality Comparison

**Requirement** (Ubiquitous):
The system shall support value equality comparison (`operator==`) for `ModelId` instances, such that two `ModelId` instances are equal if and only if both `providerId` and `modelName` are equal.

**Acceptance Criteria**:
- `operator==` is defined for `ModelId`
- Two `ModelId` instances with identical `providerId` and `modelName` compare equal
- Two `ModelId` instances differing in either field compare unequal
- Case sensitivity matches the implementation's string comparison (design choice, but must be consistent and documented)

---

### StreamEvent Type

#### REQ-F-015: StreamEvent as Closed Sum Type

**Requirement** (Ubiquitous):
The system shall provide a `StreamEvent` type that is a closed sum type (discriminated union or `std::variant`-like) with exactly four variants: `ContentDelta`, `Completed`, `Error`, and `Cancelled`.

**Acceptance Criteria**:
- A type named `StreamEvent` exists in the `holonight_domain` namespace
- It is constructible from exactly one of four variant types at any given time (no union state where multiple variants are simultaneously "active")
- A method or pattern (e.g. `std::holds_alternative`, pattern matching) exists to safely query which variant is active
- Attempting to construct a `StreamEvent` with a variant type outside these four does not compile or is otherwise rejected

---

#### REQ-F-016: ContentDelta Variant

**Requirement** (Ubiquitous):
The system shall define a `ContentDelta` variant type carrying a single field: `text` (string), representing an incremental chunk of message content during streaming.

**Acceptance Criteria**:
- A type named `ContentDelta` (or nested under `StreamEvent`) exists
- It holds a string field accessible via named accessor
- Multiple `ContentDelta` variants can be created with different text values
- Two `ContentDelta` instances with identical text compare equal

---

#### REQ-F-017: Completed Variant

**Requirement** (Ubiquitous):
The system shall define a `Completed` variant type representing a clean, successful end of message streaming with no additional data.

**Acceptance Criteria**:
- A type named `Completed` (or nested under `StreamEvent`) exists
- It is constructible with no parameters (or only default-constructed)
- Two `Completed` instances always compare equal
- Instantiating `Completed` does not require caller-supplied data

---

#### REQ-F-018: Error Variant

**Requirement** (Ubiquitous):
The system shall define an `Error` variant type carrying error information (message and/or error code), representing a streaming failure.

**Acceptance Criteria**:
- A type named `Error` (or nested under `StreamEvent`) exists
- It holds at minimum an error message as a string (additional fields such as error code are design-stage decisions)
- Two `Error` instances with identical message (and other fields) compare equal
- Two `Error` instances with different messages compare unequal

---

#### REQ-F-019: Cancelled Variant

**Requirement** (Ubiquitous):
The system shall define a `Cancelled` variant type representing a user-initiated stop of message streaming with no additional data.

**Acceptance Criteria**:
- A type named `Cancelled` (or nested under `StreamEvent`) exists
- It is constructible with no parameters (or only default-constructed)
- Two `Cancelled` instances always compare equal
- Instantiating `Cancelled` does not require caller-supplied data

---

#### REQ-F-020: StreamEvent No ToolCall or Usage Variants

**Requirement** (Ubiquitous):
The system shall NOT include `ToolCall` or `Usage` variants in the `StreamEvent` type; these are deferred to a future provider-integration cycle.

**Acceptance Criteria**:
- No `ToolCall` member or variant exists on `StreamEvent`
- No `Usage` member or variant exists on `StreamEvent`
- Attempting to add such variants results in a compile-time error (not silently ignored)

---

#### REQ-F-021: StreamEvent Equality Comparison

**Requirement** (Ubiquitous):
The system shall support value equality comparison (`operator==`) for `StreamEvent` instances, such that two `StreamEvent` instances are equal if and only if they contain the same active variant and that variant's fields are equal.

**Acceptance Criteria**:
- `operator==` is defined for `StreamEvent`
- Two `ContentDelta` variants with identical text compare equal
- A `ContentDelta` and a `Completed` variant never compare equal (different active types)
- Two `Error` variants with different messages compare unequal
- Two `Completed` instances compare equal

---

### Non-Functional Requirements

#### REQ-NF-001: C++23 Compliance

**Requirement** (Ubiquitous):
All code shall compile under C++23 with the compiler flags, warnings, and style rules defined in the project's `.clang-format` and `.clang-tidy` configurations.

**Acceptance Criteria**:
- The module compiles without errors or warnings using the project's CMake configuration (`task configure`)
- `task format-check` reports no formatting violations
- `task tidy` reports no clang-tidy violations relevant to this module

---

#### REQ-NF-002: Header Organization

**Requirement** (Ubiquitous):
All public types shall be declared in headers under `src/domain/include/holonight_domain/`, organized into logically separate headers per type family (`message.h`, `conversation.h`, `model_id.h`, `stream_event.h`).

**Acceptance Criteria**:
- Four separate header files exist at the specified paths
- Each header includes only the types and enums necessary for its domain
- A root header (e.g. `holonight_domain.h`) or CMake target exposes all four to downstream consumers
- No cyclic include dependencies exist

---

#### REQ-NF-003: Qt6 Core Dependency Only

**Requirement** (Ubiquitous):
The `holonight_domain` module shall link only against Qt6::Core; no other Qt modules or external libraries (beyond the C++ standard library) are permitted.

**Acceptance Criteria**:
- The `CMakeLists.txt` for `holonight_domain` lists only `Qt6::Core` in `target_link_libraries`
- No `#include` of Qt6::Network, Qt6::Sql, Qt6::DBus, or non-Qt libraries appears in domain headers
- `QUuid` and `QDateTime` (both from Qt6::Core) are the only Qt types used

---

#### REQ-NF-004: No External Serialization Dependencies

**Requirement** (Ubiquitous):
The domain types shall not depend on any JSON libraries (nlohmann/json, rapidjson, boost::json, etc.) or serialization frameworks; JSON support is a cross-cutting concern and will be layered separately.

**Acceptance Criteria**:
- No JSON library include or symbol appears in any domain header
- The module compiles and links without any JSON library installed
- Downstream modules (e.g. `providers`) may depend on JSON libraries independently

---

#### REQ-NF-005: Namespace Convention

**Requirement** (Ubiquitous):
All types shall reside in the `holonight_domain` namespace; no types shall be in the global namespace or alternate project namespaces.

**Acceptance Criteria**:
- All public types are declared with `namespace holonight_domain { ... }`
- No using-declarations in headers bring types into global scope
- Consumers reference types as `holonight_domain::Message`, `holonight_domain::Conversation`, etc.

---

### Constraint Requirements

#### REQ-C-001: UUID Uniqueness

**Requirement** (Ubiquitous):
Each `MessageId` and `ConversationId` generated during a process execution shall be unique; no two calls to `MessageId::generate()` or `ConversationId::generate()` within the same run shall produce identical IDs.

**Acceptance Criteria**:
- Generate 1000 `MessageId` instances in a loop and collect their string values
- Generate 1000 `ConversationId` instances in a loop and collect their string values
- Verify that all 2000 values are distinct (no duplicates)

---

#### REQ-C-002: No Heap Allocation Requirement Beyond Strings

**Requirement** (Ubiquitous):
Types shall not allocate dynamic memory beyond what is implicit in `QString`, `std::string`, or `std::vector<Message>` (i.e. no additional smart pointers, custom allocators, or external heap management).

**Acceptance Criteria**:
- A `Message`, `ModelId`, `StreamEvent` variant instance (excluding a `Conversation` with a large message list) does not internally allocate unbounded heap memory
- String storage (implicit in `QString`/`std::string`) is acceptable
- A `Conversation` may heap-allocate for its message vector but not for extraneous purposes

---

---

## Acceptance Testing Strategy

### Unit Tests (via GTest/CTest)

Tests shall validate the following scenarios and report pass/fail via `task test`:

1. **Message Construction and Accessors**
   - Construct a `Message` with explicit id, role, text, status; verify all fields are accessible and match the input

2. **Message Default Status**
   - Construct a `Message` without specifying status; verify default status is `Pending`

3. **Message Equality**
   - Create two `Message` instances with identical fields; verify `lhs == rhs` is true
   - Create two `Message` instances differing in text; verify `lhs != rhs` is true

4. **Message Status Transitions: Valid Paths**
   - Create a `Message` in `Pending` state; transition to `Streaming`; verify status is now `Streaming`
   - From `Streaming`, transition to `Complete`; verify status is now `Complete`
   - Repeat for `Error` and `Cancelled` from `Streaming`

5. **Message Status Transitions: Invalid Paths**
   - Create a `Message` in `Pending`; attempt transition to `Complete`; verify transition is rejected and status remains `Pending`
   - Create a `Message` in `Complete`; attempt transition to `Streaming`; verify transition is rejected and status remains `Complete`
   - Repeat for other invalid transition pairs (e.g., `Error` → `Streaming`, `Cancelled` → `Streaming`)

6. **ConversationId Uniqueness**
   - Generate 100 `ConversationId` instances; verify all are distinct

7. **Conversation Construction and Accessors**
   - Construct a `Conversation` with id, title, createdAt; verify all fields are accessible

8. **Conversation Message Insertion Order**
   - Create an empty `Conversation`
   - Append three messages M1, M2, M3 (with distinct IDs or text)
   - Iterate the message list; verify order is M1, M2, M3 (not shuffled)

9. **Conversation UpdatedAt Synchronization**
   - Create a `Conversation` with `updatedAt` = T1
   - Append a message after a delay (e.g. 10 ms)
   - Verify `updatedAt` is now T2 > T1

10. **Conversation Equality**
    - Create two identical `Conversation` instances; verify `lhs == rhs` is true
    - Create two conversations with same id/title but different message lists; verify they are unequal

11. **ModelId Construction and Equality**
    - Construct two `ModelId` instances with identical providerId and modelName; verify equality
    - Construct two `ModelId` instances with differing providerId; verify inequality

12. **StreamEvent Variant Construction**
    - Create a `StreamEvent` holding a `ContentDelta` with text "hello"; verify it holds `ContentDelta`
    - Create a `StreamEvent` holding a `Completed`; verify it holds `Completed` (not `ContentDelta`)
    - Repeat for `Error` and `Cancelled`

13. **StreamEvent Equality**
    - Create two `ContentDelta` instances with identical text; wrap each in `StreamEvent`; verify equality
    - Create two `Completed` instances; wrap in `StreamEvent`; verify equality
    - Create `ContentDelta` and `Completed` in separate `StreamEvent`; verify inequality

---

## Implementation Notes (for Design and Development Phases)

The following are design-stage decisions that this specification defers:

1. **Status Transition Rejection Mechanism**
   - Exception (throw std::runtime_error or custom exception)?
   - Return `std::expected<void, Error>` or `std::optional`?
   - Use assertions and UB in debug builds?
   - Decision: Design phase must choose and document.

2. **StreamEvent Representation**
   - Use `std::variant<ContentDelta, Completed, Error, Cancelled>`?
   - Use hand-rolled visitor pattern or custom discriminated union?
   - Decision: Design phase must choose based on C++23 features and Qt integration.

3. **String Type**
   - Use `QString` throughout (Qt-idiomatic)?
   - Use `std::string` throughout (Qt-agnostic)?
   - Mix (QString in public API, std::string in private)?
   - Decision: Design phase must standardize and document.

4. **Error Variant Details**
   - Error message only, or also error code / category?
   - Platform-specific error codes (errno, HRESULT)?
   - Decision: Design phase must refine.

5. **Conversation Message Storage**
   - `std::vector<Message>`?
   - `std::deque<Message>`?
   - `std::list<Message>`?
   - Decision: Design phase must choose (only order preservation is required).

---

## Success Criteria

This specification is complete and ready for Design/Development when:

1. All 21 functional and non-functional requirements are restated, numbered, and tied to acceptance criteria
2. All acceptance criteria are independently verifiable (falsifiable, not tautological)
3. Non-goals and deferments are clearly listed
4. Design-stage decisions are identified and marked for later phases
5. The testing strategy covers all critical paths (construction, equality, transitions, order preservation)
6. No implementation details (code snippets, exact function signatures) are prescribed here; those come in Design

---

## Document History

| Version | Date       | Author | Changes                                              |
|---------|------------|--------|------------------------------------------------------|
| 1.0     | 2026-07-21 | Claude | Initial specification in EARS format from req-gather decisions |
