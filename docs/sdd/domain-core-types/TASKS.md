# SDD Tasks — domain-core-types

- [x] T-001: Update src/domain/CMakeLists.txt from INTERFACE to STATIC library
  - REQs: REQ-NF-001, REQ-NF-002, REQ-NF-003
  - Check: src/domain/CMakeLists.txt defines add_library(holonight_domain STATIC ...) and links Qt6::Core PUBLIC with C++23 target_compile_features.

- [x] T-002: Implement message.h and message.cpp with MessageId, MessageRole, MessageStatus, and Message types
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-NF-001
  - Check: Files compile without warnings, MessageId provides generate() and toString(), Message constructors default to Pending status, and transitionTo() returns std::expected<void, TransitionError> that rejects invalid transitions.

- [x] T-003: Implement conversation.h and conversation.cpp with ConversationId and Conversation types
  - REQs: REQ-F-008, REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012, REQ-NF-001
  - Check: Files compile without warnings, conversation.h includes message.h, Conversation stores messages in std::vector, and appendMessage() stamps updatedAt to current time.

- [x] T-004: Implement model_id.h with ModelId struct
  - REQs: REQ-F-013, REQ-F-014, REQ-NF-001
  - Check: model_id.h compiles without warnings and declares ModelId struct with provider_id and model_name QString members with defaulted equality operator.

- [x] T-005: Implement stream_event.h with StreamEvent variant and variant types
  - REQs: REQ-F-015, REQ-F-016, REQ-F-017, REQ-F-018, REQ-F-019, REQ-F-020, REQ-F-021, REQ-NF-001
  - Check: stream_event.h compiles without warnings and defines std::variant<ContentDelta, Completed, Error, Cancelled> as StreamEvent with all four variant types.

- [x] T-006: Update holonight_domain.h umbrella header to re-export all four type headers
  - REQs: REQ-NF-002
  - Check: holonight_domain.h includes message.h, conversation.h, model_id.h, and stream_event.h, with outdated attachment comment removed.

- [x] T-007: Update tests/CMakeLists.txt to wire domain tests and link holonight_domain
  - REQs: REQ-NF-001
  - Check: tests/CMakeLists.txt adds domain/test_message.cpp, domain/test_conversation.cpp, domain/test_model_id.cpp, and domain/test_stream_event.cpp to test_holonight_ai executable and links holonight_domain target.

- [x] T-008: Implement test_message.cpp with MessageId and Message unit tests
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-C-001
  - Check: test_message.cpp compiles and all TEST cases pass for MessageId::generate() uniqueness (1000 IDs), Message default status, equality, and all valid/invalid state transitions per SPEC.md Acceptance Testing Strategy items 1-5.

- [x] T-009: Implement test_conversation.cpp with ConversationId and Conversation unit tests
  - REQs: REQ-F-008, REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012, REQ-C-001
  - Check: test_conversation.cpp compiles, all TEST cases pass for ConversationId uniqueness (100 IDs), message insertion order preservation, updatedAt sync with explicit sleep mitigation per DESIGN.md §8, and equality per SPEC.md items 6-10.

- [x] T-010: Implement test_model_id.cpp with ModelId unit tests
  - REQs: REQ-F-013, REQ-F-014
  - Check: test_model_id.cpp compiles and all TEST cases pass for ModelId construction, equality with identical provider_id and model_name, and inequality with differing fields per SPEC.md item 11.

- [x] T-011: Implement test_stream_event.cpp with StreamEvent variant unit tests
  - REQs: REQ-F-015, REQ-F-016, REQ-F-017, REQ-F-018, REQ-F-019, REQ-F-020, REQ-F-021
  - Check: test_stream_event.cpp compiles and all TEST cases pass for all four variant types, std::holds_alternative queries, cross-variant inequality, and equality semantics per SPEC.md items 12-13 and DESIGN.md §1.5.

- [x] T-012: Verify full build and test suite passes
  - REQs: REQ-NF-001, REQ-C-001, REQ-C-002
  - Check: Running 'task configure-tests && task test' succeeds with all domain tests passing and zero failures or warnings.

- [x] T-013: Verify code formatting and clang-tidy compliance
  - REQs: REQ-NF-001
  - Check: Running 'task format-check' and 'task tidy' report zero violations in domain source and test files.
