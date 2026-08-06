add_library(holonight_credentials STATIC
    include/holonight_credentials/credential_store.h
    include/holonight_credentials/secret_service_credential_store.h
    include/holonight_credentials/detail/credential_store_worker.h
    src/credential_store.cpp
    src/secret_service_credential_store.cpp
    src/detail/credential_store_worker.cpp
)

target_include_directories(holonight_credentials PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBSECRET_INCLUDE_DIRS}
)

target_link_libraries(holonight_credentials PUBLIC
    Qt6::Core
    ${LIBSECRET_LIBRARIES}
)

target_compile_features(holonight_credentials PUBLIC cxx_std_23)
