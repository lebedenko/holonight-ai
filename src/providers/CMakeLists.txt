add_library(holonight_providers STATIC
    include/holonight_providers/http_client.h
    include/holonight_providers/clock.h
    include/holonight_providers/ollama_provider.h
    include/holonight_providers/ollama_tool_codec.h
    include/holonight_providers/openai_provider.h
    include/holonight_providers/openai_tool_codec.h
    include/holonight_providers/anthropic_provider.h
    include/holonight_providers/anthropic_tool_codec.h
    include/holonight_providers/google_provider.h
    include/holonight_providers/google_tool_codec.h
    include/holonight_providers/qt_network_http_client.h
    src/clock.cpp
    src/cancellation_aware_handle.h
    src/qt_network_http_client.cpp
    src/ollama_provider.cpp
    src/ollama_tool_codec.cpp
    src/openai_provider.cpp
    src/openai_tool_codec.cpp
    src/anthropic_provider.cpp
    src/anthropic_tool_codec.cpp
    src/google_provider.cpp
    src/google_tool_codec.cpp
)

set_target_properties(holonight_providers PROPERTIES AUTOMOC ON)

target_include_directories(holonight_providers PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(holonight_providers PUBLIC
    holonight_domain
    Qt6::Core
    Qt6::Network
)

target_compile_features(holonight_providers PUBLIC cxx_std_23)
