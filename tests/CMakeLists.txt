include(GoogleTest)

add_executable(test_holonight_ai
  main.cpp
  test_placeholder.cpp
  domain/test_message.cpp
  domain/test_conversation.cpp
  domain/test_model_id.cpp
  domain/test_tool_activity.cpp
  domain/test_stream_event.cpp
  rendering/test_message_content_parser.cpp
  providers/test_clock.cpp
  providers/test_ollama_provider.cpp
  providers/test_ollama_tool_codec.cpp
  providers/test_openai_provider.cpp
  providers/test_openai_tool_codec.cpp
  providers/test_anthropic_provider.cpp
  providers/test_google_provider.cpp
  application/test_chat_controller.cpp
  application/test_chat_view_model.cpp
  application/test_message_list_model.cpp
  application/test_message_content_model.cpp
  application/test_conversation_list_model.cpp
  application/test_provider_settings_controller.cpp
  application/test_provider_runtime_coordinator.cpp
  application/test_provider_adapter_router.cpp
  application/test_provider_instance_registry.cpp
  application/test_provider_draft_session.cpp
  application/test_provider_draft_committer.cpp
  application/test_provider_draft_guard.cpp
  application/test_provider_instance_deleter.cpp
  application/test_openai_provider_settings_controller.cpp
  application/test_anthropic_provider_settings_controller.cpp
  application/test_google_provider_settings_controller.cpp
  application/test_utility_task_runner.cpp
  application/test_utility_settings_controller.cpp
  application/test_token_formatting.cpp
  application/test_tool_orchestrator.cpp
  application/test_tool_registry.cpp
  application/test_list_files_tool.cpp
  application/test_tool_registry_contract.cpp
  persistence/test_database_path.cpp
  persistence/test_migration_runner.cpp
  persistence/test_conversation_record.cpp
  persistence/test_conversation_repository_worker_threading.cpp
  persistence/test_conversation_repository_sqlite.cpp
  persistence/test_usage_persistence.cpp
  credentials/test_credential_store_fake.cpp
  credentials/test_credential_store_worker_threading.cpp
  config/test_config_path.cpp
  config/test_config_repository.cpp
  platform/test_panel_surface_policy.cpp
  platform/test_desktop_notifier_policy.cpp
  qml/test_canonical_modules.cpp
  qml/test_chat_composer.cpp
  qml/test_chat_composer_actions.cpp
  qml/test_chat_message_delegate.cpp
  qml/test_provider_management.cpp
  qml/test_bottom_anchored_list_view.cpp
  qml/test_markdown_block.cpp
  qml/test_assistant_response_content.cpp
  qml/test_response_stats_footer.cpp
  qml/test_background_ai_settings_panel.cpp
  qml/test_conversation_list_delegate.cpp
  qml/test_tool_renderer_registry.cpp
  qml/test_list_files_tool_content.cpp
  qml/test_tool_activity_card.cpp
  qml/test_generic_tool_content.cpp
)
set_target_properties(test_holonight_ai PROPERTIES AUTOMOC ON)
target_compile_features(test_holonight_ai PRIVATE cxx_std_23)
target_include_directories(test_holonight_ai PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_holonight_ai PRIVATE
  GTest::gtest
  GTest::gmock
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::Sql
  Qt6::Test
  holonight_domain
  holonight_rendering
  holonight_providers
  holonight_application
  holonight_persistence
  holonight_credentials
  holonight_config
  holonight_platform
)
target_compile_definitions(test_holonight_ai PRIVATE
  HOLONIGHT_QML_IMPORT_PATH="${HOLONIGHT_QML_IMPORT_PATH}"
  PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
gtest_discover_tests(test_holonight_ai
  DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_test(
  NAME canonical_qml_import_policy
  COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/check-canonical-qml-imports.sh" "${PROJECT_SOURCE_DIR}"
)

add_test(
  NAME semantic_qml_styling_policy
  COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/check-semantic-qml-styling.sh" "${PROJECT_SOURCE_DIR}"
)

if(ENABLE_COVERAGE)
  target_compile_options(test_holonight_ai PRIVATE --coverage)
  target_link_options(test_holonight_ai PRIVATE --coverage)

  add_custom_target(coverage
    COMMAND find ${CMAKE_BINARY_DIR} -name "*.gcda" -delete
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/coverage
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
    COMMAND $<TARGET_FILE:test_holonight_ai>
    COMMAND ${GCOVR}
        --root ${CMAKE_SOURCE_DIR}
        --filter "${CMAKE_SOURCE_DIR}/src/"
        --html-details ${CMAKE_BINARY_DIR}/coverage/index.html
        --exclude-unreachable-branches
        --print-summary
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS
      test_holonight_ai
      holonight-chat
    COMMENT "Collecting coverage → build/coverage/index.html"
  )
endif()
