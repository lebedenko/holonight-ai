# SDD Tasks — google-tool-calling-listfiles

- [x] T-001: Add ToolRequestEvent thought_signature and provider_call_id_synthesized fields
  - REQs: REQ-F-005, REQ-F-006, REQ-F-009
  - Check: ToolRequestEvent in holonight_domain::stream_event.h includes std::optional<QString> thought_signature and bool provider_call_id_synthesized = false fields; compiles without error

- [x] T-002: Add ToolCallEntry thought_signature and provider_call_id_synthesized fields
  - REQs: REQ-F-005, REQ-F-006, REQ-F-008, REQ-F-009
  - Check: ToolCallEntry in holonight_domain::tool_call.h includes std::optional<QString> thought_signature and bool provider_call_id_synthesized = false fields; operator== picks them up automatically

- [x] T-003: Extend conversation_repository_worker encodeToolCalls/decodeToolCalls for new fields
  - REQs: REQ-F-005, REQ-F-006, REQ-F-008, REQ-F-009
  - Check: encodeToolCalls() conditionally writes thought_signature and provider_call_id_synthesized to JSON; decodeToolCalls() reads them back with proper defaults; round-trip preserves both values across app restart

- [x] T-004: Implement GoogleToolCodec with encodeDefinitions, decodeRequest, encodeFunctionResponse, encodeHistory methods
  - REQs: REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-NF-001, REQ-NF-003
  - Check: GoogleToolCodec in holonight_providers compiles with all four static methods; encodeDefinitions produces Gemini Tool schema [{"functionDeclarations":[...]}]; decodeRequest returns std::optional<ToolRequestEvent>; encodeFunctionResponse omits id when provider_call_id_synthesized is true

- [x] T-005: Update GoogleProvider::sendChat() signature to accept ToolCatalogSnapshot parameter
  - REQs: REQ-F-001
  - Check: GoogleProvider::sendChat() in holonight_providers compiles with 5th parameter const holonight_domain::ToolCatalogSnapshot& tool_catalog = {}; existing 4-arg calls remain compatible; parameter is usable in method body

- [x] T-006: Add provider_instance_id field to StreamContext and implement function call parsing
  - REQs: REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-NF-004
  - Check: StreamContext in google_provider.cpp includes QString provider_instance_id field; routeSseEvent() collects functionCall parts in order, calls GoogleToolCodec::decodeRequest() per part, and emits ToolRequestEvent events before finishReason handling; malformed args cause failStream() call

- [x] T-007: Modify GoogleProvider::sendChat() to use GoogleToolCodec::encodeHistory for request body construction
  - REQs: REQ-F-007, REQ-F-008, REQ-F-009
  - Check: GoogleProvider::sendChat() calls GoogleToolCodec::encodeHistory(history, systemParts) and groups same-role Messages; follow-up turn includes reconstructed functionCall and functionResponse parts with correct name/args/response; thought_signature is echoed when present; id is omitted when provider_call_id_synthesized is true

- [x] T-008: Add Google provider gating branch to ProviderAdapterRouter::sendChat
  - REQs: REQ-F-010, REQ-F-011
  - Check: ProviderAdapterRouter::sendChat() checks GoogleProviderConfig::tool_calling_enabled via std::get_if and passes toolCatalog to GoogleProvider when true; dispatch uses if constexpr with both AnthropicProvider and GoogleProvider alternatives; stale scope comment is updated to describe two-provider reality

- [x] T-009: Thread thought_signature and provider_call_id_synthesized through ChatController ToolCallEntry construction sites
  - REQs: REQ-F-005, REQ-F-006, REQ-F-008, REQ-F-009
  - Check: ChatController::makeInvocationCallEntry() and handleToolCall() populate .thought_signature and .provider_call_id_synthesized from ToolRequestEvent parameters; both construction sites in handleToolCall() include the fields; Result-kind entries are unchanged

- [x] T-010: Add tool_calling_enabled field to GoogleProviderConfig
  - REQs: REQ-F-012, REQ-F-013
  - Check: GoogleProviderConfig in holonight_config includes bool tool_calling_enabled = false; default constructor sets it to false; new instances created via settings UI default to false

- [x] T-011: Update config_repository.cpp to parse and serialize GoogleProviderConfig::tool_calling_enabled
  - REQs: REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-015
  - Check: parseSettings() reads toolCallingEnabled before type split and applies to both Anthropic and Google configs; serializeSettings() if constexpr guards tool_calling_enabled write for both types; missing key on disk decodes as false; round-trip with true and false both preserve the value

- [x] T-012: Add toolCallingEnabled Q_PROPERTY to GoogleProviderSettingsController
  - REQs: REQ-F-016
  - Check: GoogleProviderSettingsController includes Q_PROPERTY(bool toolCallingEnabled READ toolCallingEnabled WRITE setToolCallingEnabled NOTIFY toolCallingEnabledChanged); getter/setter and signal mirror AnthropicProviderSettingsController exactly; loadDraft() and updateDraft() thread the value from/to GoogleProviderConfig

- [x] T-013: Add toggle control to GoogleSettingsPanel.qml
  - REQs: REQ-F-017, REQ-F-018
  - Check: GoogleSettingsPanel.qml includes ProviderFormActionRow with HnFormField and Switch bound to GoogleProviderSettingsController.toolCallingEnabled; label text is "Enable tool calling"; toggling the switch updates the underlying config and persists to storage

- [x] T-014: Implement GoogleToolCodec definitions encoding tests
  - REQs: REQ-F-002, REQ-NF-002, REQ-NF-003
  - Check: TEST(GoogleProvider, SendChatIncludesToolsArrayInRequestBodyWhenProvided) passes and TEST(GoogleProvider, SendChatOmitsToolsKeyWhenToolsAbsentOrEmpty) passes; tools array matches Gemini schema; no live API calls

- [x] T-015: Implement GoogleToolCodec atomic and parallel parsing tests
  - REQs: REQ-F-003, REQ-F-004, REQ-NF-002, REQ-NF-004
  - Check: TEST(GoogleProvider, SendChatEmitsToolCallOnAtomicFunctionCallPart), SendChatEmitsMultipleToolCallsForMultipleFunctionCallPartsInOrder all pass; parallel calls emit events in array order; events carry correct name and args

- [x] T-016: Implement GoogleToolCodec metadata (thought signature and ID) parsing tests
  - REQs: REQ-F-005, REQ-F-006, REQ-NF-002
  - Check: TEST(GoogleProvider, SendChatCapturesThoughtSignatureWhenPresent), SendChatOmitsThoughtSignatureWhenAbsent, SendChatUsesProviderSuppliedIdVerbatim, SendChatSynthesizesUuidWhenIdAbsent all pass; thought_signature is captured when present; synthesized IDs are valid UUIDs and provider_call_id_synthesized == true; model-provided IDs are used verbatim with provider_call_id_synthesized == false

- [x] T-017: Implement GoogleToolCodec error handling parsing tests
  - REQs: REQ-F-003, REQ-NF-002
  - Check: TEST(GoogleProvider, SendChatFailsStreamOnFunctionCallMissingName) and SendChatFailsStreamOnNonObjectArgs pass; malformed JSON in args causes Error event, not a garbage ToolRequestEvent

- [x] T-018: Implement GoogleToolCodec history reconstruction round-trip tests
  - REQs: REQ-F-007, REQ-F-008, REQ-F-009, REQ-NF-002
  - Check: TEST(GoogleProvider, SendChatReconstructsFunctionCallAndFunctionResponseAcrossTurns), SendChatEchoesThoughtSignatureOnFollowUp, SendChatOmitsIdInFunctionResponseWhenIdWasSynthesized, SendChatIncludesIdInFunctionResponseWhenIdWasFromModel, SendChatGroupsParallelFunctionCallsIntoOneContentEntry all pass; follow-up request includes correct parts; signature and ID handling match spec

- [x] T-019: Split and rename SendChatIgnoresFunctionCallAndOtherNonTextParts test
  - REQs: REQ-C-001
  - Check: Original test split into SendChatIgnoresInlineDataAndOtherNonTextNonFunctionCallParts (uses inlineData part, asserts empty events) and SendChatEmitsToolCallOnAtomicFunctionCallPart (now expects ToolRequestEvent); both pass

- [x] T-020: Implement ProviderAdapterRouter Google tool calling tests
  - REQs: REQ-F-010, REQ-F-011, REQ-NF-002
  - Check: TEST(ProviderAdapterRouter, SendsToolsArrayToGoogleWhenToolCallingEnabled), OmitsToolsArrayWhenGoogleToolCallingDisabled, OmitsToolsArrayWithoutRegistryEvenWhenGoogleToolCallingEnabled all pass; router correctly gates by flag and handles null registry

- [x] T-021: Implement GoogleProviderConfig persistence tests
  - REQs: REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-015, REQ-NF-002
  - Check: TEST(ConfigRepository, GoogleToolCallingEnabledRoundTripsTrue), GoogleToolCallingEnabledRoundTripsFalse, GoogleToolCallingEnabledDefaultsToFalseWhenKeyMissingFromOnDiskConfig all pass; missing key decodes as false

- [x] T-022: Implement GoogleProviderSettingsController property tests
  - REQs: REQ-F-016, REQ-F-017, REQ-F-018
  - Check: GoogleProviderSettingsController tests for loadDraft, updateDraft, property change notification mirror AnthropicProviderSettingsController coverage; toolCallingEnabled property binds from QML; toggling persists config

- [x] T-023: Implement ToolCallEntry persistence round-trip test
  - REQs: REQ-F-005, REQ-F-006, REQ-F-008, REQ-F-009
  - Check: TEST with ToolCallEntry having thought_signature set and provider_call_id_synthesized == true survives encodeToolCalls() then decodeToolCalls() unchanged; round-trip maintains both field values
