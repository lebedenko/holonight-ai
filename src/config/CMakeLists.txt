add_library(holonight_config STATIC
    include/holonight_config/provider_config.h
    include/holonight_config/config_path.h
    include/holonight_config/config_repository.h
    include/holonight_config/utility_config.h
    src/provider_config.cpp
    src/config_path.cpp
    src/config_repository.cpp
)

target_include_directories(holonight_config PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(holonight_config PUBLIC
    Qt6::Core
    holonight_domain
)

target_compile_features(holonight_config PUBLIC cxx_std_23)
