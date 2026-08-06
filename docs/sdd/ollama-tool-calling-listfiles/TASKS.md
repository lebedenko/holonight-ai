# SDD Tasks — ollama-tool-calling-listfiles

- [x] T-001: Add 5-arg overload to OllamaProvider::sendChat() with tool_catalog parameter
  - REQs: REQ-F-001
  - Check: OllamaProvider::sendChat() in holonight_providers has 5-arg overload with const holonight_domain::ToolCatalogSnapshot& tool_catalog = {} as trailing parameter; existing 4-arg call sites remain compatible; signature compiles without error

- [x] T-002: Implement OllamaToolCodec class with encodeDefinitions, decodeRequests, and encodeHistory methods
  - REQs: REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-018, REQ-F-019, REQ-NF-003, REQ-NF-004
  - Check: OllamaToolCodec in holonight_providers compiles with three static methods; encodeDefinitions() returns QJsonArray matching Ollama's {"type":"function","function":{name,description,parameters}} schema; decodeRequests() returns std::expected with ToolRequestEvent vector; encodeHistory() groups consecutive Assistant messages and renders tool_calls arrays and standalone tool-result messages

- [x] T-003: Implement OllamaToolCodec encoding tests
  - REQs: REQ-F-002, REQ-F-017, REQ-NF-002, REQ-NF-003
  - Check: test_ollama_tool_codec.cpp exists with TEST(OllamaToolCodec, EncodesToolDefinitionsAsFunctionObjects) and EncodeDefinitionsReturnsEmptyArrayForEmptyCatalog tests passing; tools array matches Ollama's documented schema; no hardcoded endpoints or live API calls

- [x] T-004: Implement OllamaToolCodec decoding tests (single and multiple calls, ID handling, error cases)
  - REQs: REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-NF-002, REQ-NF-004
  - Check: test_ollama_tool_codec.cpp includes passing tests DecodeRequestsUsesProvidedIdVerbatim, DecodeRequestsSynthesizesUuidWhenIdAbsent, DecodeRequestsEmitsMultipleEventsInArrayOrder, DecodeRequestsFailsOnMissingFunctionName, DecodeRequestsFailsWhenArgumentsIsJsonString, DecodeRequestsRejectsAllCallsWhenAnyEntryMalformed; single and multiple calls emit events in array order with correct provider_call_id_synthesized flag; malformed JSON returns std::unexpected without partial events

- [x] T-005: Implement OllamaToolCodec history encoding tests
  - REQs: REQ-F-007, REQ-F-018, REQ-NF-003
  - Check: test_ollama_tool_codec.cpp includes passing tests EncodeHistoryReconstructsToolCallAndResultAsSeparateMessages, EncodeHistoryGroupsConsecutiveAssistantTextAndInvocationIntoOneMessage, EncodeHistoryDoesNotMergeConsecutiveResultMessages; reconstructed messages include {"role":"assistant",...,"tool_calls":[...]} and {"role":"tool","content":...,"tool_name":...} objects with correct structure and order

- [x] T-006: Add provider_instance_id field to OllamaProvider::StreamContext
  - REQs: REQ-F-003, REQ-F-004
  - Check: StreamContext in ollama_provider.cpp includes QString provider_instance_id field; field is populated in sendChat() alongside model_identifier assignment

- [x] T-007: Modify OllamaProvider::processLine() to extract and decode tool_calls array
  - REQs: REQ-F-003, REQ-F-004, REQ-NF-002, REQ-NF-004
  - Check: processLine() checks for message.tool_calls key, calls OllamaToolCodec::decodeRequests(context->provider_instance_id, toolCalls), emits ToolRequestEvent per decoded call in array order, and calls failStream() if decoding returns error; existing ContentDelta and done handling remains unchanged

- [x] T-008: Wire OllamaToolCodec::encodeHistory() and conditional tools key into OllamaProvider::sendChat() request body
  - REQs: REQ-F-002, REQ-F-007
  - Check: OllamaProvider::sendChat() calls OllamaToolCodec::encodeHistory(history) for messages array and OllamaToolCodec::encodeDefinitions(tool_catalog) for tools; request body includes "tools" key only when tools array is non-empty; 4-arg call sites produce request bodies identical to pre-refactor implementation

- [x] T-009: Implement OllamaProvider tool calling integration tests
  - REQs: REQ-F-002, REQ-F-003, REQ-F-004, REQ-NF-002
  - Check: test_ollama_provider.cpp includes passing tests SendChatIncludesToolsArrayInRequestBodyWhenProvided, SendChatOmitsToolsKeyWhenCatalogEmpty, SendChatEmitsToolRequestEventOnSingleToolCallLine, SendChatEmitsMultipleToolRequestEventsForMultipleToolCallsInOrder, SendChatFailsStreamOnMalformedToolCallArguments, SendChatContinuesToEmitDoneLineAfterToolCallLine; all use FakeHttpClient without live API calls

- [x] T-010: Add tool_calling_enabled field to OllamaProviderConfig
  - REQs: REQ-F-010, REQ-F-011
  - Check: OllamaProviderConfig in provider_config.h includes bool tool_calling_enabled = false; default constructor and aggregate initializers set field to false; compiles without error

- [x] T-011: Update config_repository.cpp parseSettings() and serializeSettings() for OllamaProviderConfig
  - REQs: REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-013
  - Check: parseSettings() Ollama case reads tool_calling_enabled with .toBool(false) default; serializeSettings() if constexpr includes OllamaProviderConfig in tool_calling_enabled branch; missing key on disk decodes as false; serialized JSON includes the field when true

- [x] T-012: Implement config_repository OllamaProviderConfig tool calling persistence tests
  - REQs: REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-013, REQ-NF-002
  - Check: test_config_repository.cpp includes passing tests OllamaToolCallingEnabledRoundTripsTrue, OllamaToolCallingEnabledRoundTripsFalse, OllamaToolCallingEnabledDefaultsToFalseWhenKeyMissingFromOnDiskConfig; round-trip with true and false both preserve value; missing key deserializes as false

- [x] T-013: Add Ollama tool calling gating branch to ProviderAdapterRouter::sendChat()
  - REQs: REQ-F-008, REQ-F-009
  - Check: ProviderAdapterRouter::sendChat() includes std::get_if<OllamaProviderConfig> branch checking tool_calling_enabled and populating toolCatalog when true; if constexpr dispatch includes OllamaProvider in the four-way disjunct passing toolCatalog; compiler accepts the template instantiation

- [x] T-014: Implement ProviderAdapterRouter Ollama tool calling tests
  - REQs: REQ-F-008, REQ-F-009, REQ-NF-002
  - Check: test_provider_adapter_router.cpp includes passing tests SendsToolsArrayToOllamaWhenToolCallingEnabled, OmitsToolsArrayWhenOllamaToolCallingDisabled, OmitsToolsArrayWithoutRegistryEvenWhenOllamaToolCallingEnabled; router passes non-empty catalog when flag is true and empty catalog when false or when registry is null

- [x] T-015: Add toolCallingEnabled Q_PROPERTY to ProviderSettingsController
  - REQs: REQ-F-014, REQ-F-016
  - Check: ProviderSettingsController includes Q_PROPERTY(bool toolCallingEnabled READ toolCallingEnabled WRITE setToolCallingEnabled NOTIFY toolCallingEnabledChanged); getter/setter and signal definitions exist; loadDraft() reads OllamaProviderConfig::tool_calling_enabled via setToolCallingEnabled(); updateDraft() includes .tool_calling_enabled = tool_calling_enabled_ in aggregate initializer

- [x] T-016: Implement ProviderSettingsController tool calling property tests
  - REQs: REQ-F-014, REQ-F-015, REQ-F-016, REQ-NF-002
  - Check: test_provider_settings_controller.cpp includes passing tests for loadDraft, updateDraft, and property change notification; toolCallingEnabled property binds from QML without errors; toggling the property updates internal state and persists via draftSession()->setSettings()

- [x] T-017: Add tool calling toggle control to OllamaSettingsPanel.qml
  - REQs: REQ-F-015, REQ-F-016
  - Check: OllamaSettingsPanel.qml includes ProviderFormActionRow with HnFormField/Switch bound to ProviderSettingsController.toolCallingEnabled; label text is "Enable tool calling"; Switch.onToggled updates ProviderSettingsController.toolCallingEnabled; QML syntax is valid and file compiles without qmllint errors

- [x] T-018: Verify ChatController requires no changes for tool calling support
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-015, REQ-F-016, REQ-F-017, REQ-F-018, REQ-F-019
  - Check: Code review of chat_controller.cpp confirms handleStreamEvent()'s ToolRequestEvent branch and makeInvocationCallEntry()/makeResultCallEntry()/kMaxToolCallsPerTurn logic are fully provider-agnostic and unchanged; no new ChatController commits are created by this cycle

- [x] T-019: Full build and test suite verification
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-015, REQ-F-016, REQ-F-017, REQ-F-018, REQ-F-019, REQ-NF-001, REQ-NF-002, REQ-NF-003, REQ-NF-004, REQ-C-001, REQ-C-002, REQ-C-003, REQ-C-004, REQ-C-005, REQ-C-006, REQ-C-007, REQ-C-008
  - Check: task configure && task build completes without errors or warnings; ctest -R Ollama passes with 100% of Ollama-specific tests; no regression in pre-existing test suites; full holonight-chat binary launches without crash; manual smoke test sends a message to Ollama with tool_calling_enabled=true and verifies ListFiles tool call delivery in chat transcript
