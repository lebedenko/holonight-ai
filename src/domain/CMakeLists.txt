add_library(holonight_domain STATIC
    src/message.cpp
    src/conversation.cpp
)

target_include_directories(holonight_domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(holonight_domain PUBLIC
    Qt6::Core
)

target_compile_features(holonight_domain PUBLIC cxx_std_23)
