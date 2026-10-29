cmake_minimum_required(VERSION 3.25)
project(holonight-ai VERSION 0.1.0 LANGUAGES C CXX)

# Installed QML providers follow the standard CMake prefix search path.
set(_qml_prefixes ${CMAKE_PREFIX_PATH})
if(DEFINED ENV{CMAKE_PREFIX_PATH})
  string(REPLACE ":" ";" _prefix_environment "$ENV{CMAKE_PREFIX_PATH}")
  list(APPEND _qml_prefixes ${_prefix_environment})
endif()
foreach(_prefix IN LISTS _qml_prefixes)
  if(IS_DIRECTORY "${_prefix}/lib/qt6/qml")
    list(APPEND QML_IMPORT_PATH "${_prefix}/lib/qt6/qml")
  endif()
endforeach()
if(DEFINED ENV{QML_IMPORT_PATH})
  string(REPLACE ":" ";" _qml_environment "$ENV{QML_IMPORT_PATH}")
  list(APPEND QML_IMPORT_PATH ${_qml_environment})
endif()
list(REMOVE_DUPLICATES QML_IMPORT_PATH)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

install(FILES LICENSE LICENSES/GPL-3.0-or-later.txt
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}")

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Quick Qml Network Concurrent Sql DBus)
find_package(HolonightQt REQUIRED COMPONENTS Controls Wayland)
find_package(QMarkdown 1.18 CONFIG REQUIRED)
list(APPEND QML_IMPORT_PATH "${QMarkdown_QML_IMPORT_PATH}")
unset(HOLONIGHT_QML_IMPORT_PATH CACHE)
find_path(HOLONIGHT_QML_IMPORT_PATH
  NAMES Holonight/qmldir
  HINTS "${HolonightQt_DIR}/../../.."
  PATH_SUFFIXES lib/qt6/qml lib64/qt6/qml
  NO_DEFAULT_PATH
  REQUIRED
)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)

# Generate qmldir redirectors for QML files in module subdirectories.
qt_policy(SET QTP0004 NEW)

# ============================================================
# Libraries and apps
# ============================================================
add_subdirectory(src/domain)
add_subdirectory(src/config)
add_subdirectory(src/application)
add_subdirectory(src/providers)
add_subdirectory(src/persistence)
add_subdirectory(src/credentials)
add_subdirectory(src/platform)
add_subdirectory(apps/chat)

# ============================================================
# Tests
# ============================================================
option(BUILD_TESTS "Build unit tests using installed GTest and GMock" OFF)
option(ENABLE_COVERAGE "Instrument for gcov/gcovr coverage reporting" OFF)

if(DEFINED BUILD_TESTS AND
   NOT "${BUILD_TESTS}" STREQUAL "ON" AND
   NOT "${BUILD_TESTS}" STREQUAL "OFF" AND
   NOT "${BUILD_TESTS}" STREQUAL "1" AND
   NOT "${BUILD_TESTS}" STREQUAL "0" AND
   NOT "${BUILD_TESTS}" STREQUAL "TRUE" AND
   NOT "${BUILD_TESTS}" STREQUAL "FALSE" AND
   NOT "${BUILD_TESTS}" STREQUAL "YES" AND
   NOT "${BUILD_TESTS}" STREQUAL "NO")
  message(FATAL_ERROR "BUILD_TESTS must be ON or OFF, got: '${BUILD_TESTS}'")
endif()

if(BUILD_TESTS)
  enable_testing()

  find_package(Qt6 6.11 REQUIRED COMPONENTS Test)

  find_package(GTest 1.14 CONFIG REQUIRED)
  foreach(required_target IN ITEMS GTest::gtest GTest::gmock)
    if(NOT TARGET ${required_target})
      message(FATAL_ERROR "Installed GTest lacks ${required_target}; install GTest and GMock >=1.14")
    endif()
  endforeach()

  if(ENABLE_COVERAGE)
    find_program(GCOVR gcovr REQUIRED)
    add_custom_target(clean-coverage-counters
      COMMAND find ${CMAKE_BINARY_DIR} -name "*.gcda" -delete
      COMMENT "Cleaning stale gcov counters"
      VERBATIM
    )
    target_compile_options(holonight-chat PRIVATE --coverage)
    target_link_options(holonight-chat PRIVATE --coverage)

    # The STATIC library targets hold all the actual logic under test — without instrumenting
    # them too, gcovr sees only the thin test_holonight_ai TU itself and reports near-zero
    # coverage for src/ regardless of how thorough the tests are.
    foreach(coverage_target IN ITEMS
        holonight_domain
        holonight_config
        holonight_application
        holonight_providers
        holonight_persistence
        holonight_credentials
        holonight_platform
    )
      target_compile_options(${coverage_target} PRIVATE --coverage)
      target_link_options(${coverage_target} PRIVATE --coverage)
    endforeach()
  endif()

  add_subdirectory(tests)
endif()

# ============================================================
# Format / Tidy / qmllint targets
# ============================================================
find_program(CLANG_FORMAT clang-format)

if(NOT QMLLINT)
  get_target_property(QMLLINT Qt6::qmllint IMPORTED_LOCATION)
endif()

file(GLOB_RECURSE ALL_SOURCE_FILES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    apps/*.cpp apps/*.h src/*.cpp src/*.h tests/*.cpp tests/*.h
)

file(GLOB_RECURSE ALL_QML_FILES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    qml/*.qml
)

if(CLANG_FORMAT)
  add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES}
        COMMENT "Running clang-format"
        VERBATIM
    )
  add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ALL_SOURCE_FILES}
        COMMENT "Checking clang-format"
        VERBATIM
    )
endif()

# Use the same direct compilation-database check as task tidy.
add_custom_target(tidy
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/quality.py" tidy --build "${CMAKE_BINARY_DIR}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  VERBATIM
)

if(QMLLINT)
  cmake_path(ABSOLUTE_PATH QT6_INSTALL_QML BASE_DIRECTORY "${QT6_INSTALL_PREFIX}"
             OUTPUT_VARIABLE HOLONIGHT_QT_QML_DIR)
  add_custom_target(qml-lint
        COMMAND ${QMLLINT}
            --bare
            --max-warnings 0
            --unqualified disable
            --unused-imports disable
            -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/qmllint"
            -I "${CMAKE_CURRENT_BINARY_DIR}/apps/chat"
            -I "${CMAKE_BINARY_DIR}"
            -I "${HOLONIGHT_QML_IMPORT_PATH}"
            -I "${QMarkdown_QML_IMPORT_PATH}"
            -I "${HOLONIGHT_QT_QML_DIR}"
            ${ALL_QML_FILES}
        DEPENDS
            holonight-chat_qmltyperegistration
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running qmllint"
        VERBATIM
    )
else()
  add_custom_target(qml-lint
        COMMAND ${CMAKE_COMMAND} -E echo "qmllint was not found. Install qt6-declarative-dev-tools."
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM
    )
endif(QMLLINT)
