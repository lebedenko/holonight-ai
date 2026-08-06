add_library(holonight_application STATIC
    include/holonight_application/chat_controller.h
    include/holonight_application/chat_view_model.h
    include/holonight_application/message_list_model.h
    include/holonight_application/message_content_model.h
    include/holonight_application/conversation_list_model.h
    include/holonight_application/provider_settings_controller_base.h
    include/holonight_application/provider_runtime_coordinator.h
    include/holonight_application/provider_adapter_router.h
    include/holonight_application/provider_instance_list_model.h
    include/holonight_application/provider_instance_registry.h
    include/holonight_application/provider_draft_session.h
    include/holonight_application/provider_draft_committer.h
    include/holonight_application/provider_draft_guard.h
    include/holonight_application/provider_instance_deleter.h
    include/holonight_application/provider_management_controller.h
    include/holonight_application/provider_settings_controller.h
    include/holonight_application/openai_provider_settings_controller.h
    include/holonight_application/anthropic_provider_settings_controller.h
    include/holonight_application/google_provider_settings_controller.h
    include/holonight_application/utility_task_runner.h
    include/holonight_application/utility_settings_controller.h
    include/holonight_application/token_formatting.h
    include/holonight_application/token_formatter.h
    include/holonight_application/tools/i_tool.h
    include/holonight_application/tools/tool_contracts.h
    include/holonight_application/tools/tool_orchestrator.h
    include/holonight_application/tools/tool_presenters.h
    include/holonight_application/tools/tool_registry.h
    include/holonight_application/tools/list_files_tool.h
    src/chat_controller.cpp
    src/chat_view_model.cpp
    src/message_list_model.cpp
    src/message_content_model.cpp
    src/conversation_list_model.cpp
    src/provider_settings_controller_base.cpp
    src/provider_runtime_coordinator.cpp
    src/provider_adapter_router.cpp
    src/provider_instance_list_model.cpp
    src/provider_instance_registry.cpp
    src/provider_draft_session.cpp
    src/provider_draft_committer.cpp
    src/provider_draft_guard.cpp
    src/provider_instance_deleter.cpp
    src/provider_management_controller.cpp
    src/provider_settings_controller.cpp
    src/openai_provider_settings_controller.cpp
    src/anthropic_provider_settings_controller.cpp
    src/google_provider_settings_controller.cpp
    src/utility_task_runner.cpp
    src/utility_settings_controller.cpp
    src/token_formatting.cpp
    src/token_formatter.cpp
    src/tools/tool_registry.cpp
    src/tools/tool_orchestrator.cpp
    src/tools/tool_presenters.cpp
    src/tools/list_files_tool.cpp
)

target_include_directories(holonight_application PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# qmltyperegistrar emits bare-filename `#if __has_include(<chat_view_model.h>)` guards in the
# generated registration .cpp regardless of the header's real include path (verified against
# holonight-shell's generated output, which relies on its own headers living flat under src/ for
# the same reason) — so the consuming target (holonight-chat) needs the header's own directory on
# its include path, not just the parent include/ root used everywhere else in this repo.
target_include_directories(holonight_application PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/holonight_application
)

target_link_libraries(holonight_application PUBLIC
    holonight_domain
    holonight_providers
    holonight_persistence
    holonight_config
    holonight_credentials
    Qt6::Core
    Qt6::Qml
)

target_link_libraries(holonight_application PRIVATE
    holonight_rendering
)

target_compile_features(holonight_application PUBLIC cxx_std_23)
