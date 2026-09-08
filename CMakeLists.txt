cmake_minimum_required(VERSION 3.25)
project(holonight-ai VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

install(FILES LICENSE LICENSES/GPL-3.0-or-later.txt
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Network Concurrent Sql DBus)
find_package(HolonightQt REQUIRED COMPONENTS Controls Wayland)
unset(HOLONIGHT_QML_IMPORT_PATH CACHE)
find_path(HOLONIGHT_QML_IMPORT_PATH
  NAMES Holonight/qmldir
  HINTS "${HolonightQt_DIR}/../../.."
  PATH_SUFFIXES lib/qt6/qml lib64/qt6/qml
  NO_DEFAULT_PATH
  REQUIRED
)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSECRET REQUIRED libsecret-1)

# Generate qmldir redirectors for QML files in module subdirectories.
qt_policy(SET QTP0004 NEW)

# ============================================================
# Libraries and apps
# ============================================================
add_subdirectory(src/domain)
add_subdirectory(src/config)
add_subdirectory(src/rendering)
add_subdirectory(src/application)
add_subdirectory(src/providers)
add_subdirectory(src/persistence)
add_subdirectory(src/credentials)
add_subdirectory(src/platform)
add_subdirectory(apps/chat)

# ============================================================
# Tests
# ============================================================
option(BUILD_TESTS "Build unit tests (requires network access at configure time)" OFF)
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

  find_package(Qt6 REQUIRED COMPONENTS Test)

  find_package(GTest CONFIG QUIET)
  if(NOT GTest_FOUND OR NOT TARGET GTest::gtest OR NOT TARGET GTest::gmock)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL      https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
      URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

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
        holonight_rendering
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
find_program(CLANG_TIDY clang-tidy)

find_program(RUN_CLANG_TIDY run-clang-tidy)
find_program(QMLLINT qmllint
    HINTS
        "${Qt6_DIR}/../../../lib/qt6/bin"
        "/usr/lib/qt6/bin"
)

file(GLOB_RECURSE ALL_SOURCE_FILES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    apps/*.cpp apps/*.h src/*.cpp src/*.h tests/*.cpp tests/*.h
)

file(GLOB_RECURSE ALL_CXX_SRC_IMPL_FILES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    apps/*.cpp src/*.cpp
)

file(GLOB_RECURSE ALL_CXX_TEST_IMPL_FILES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    tests/*.cpp
)
list(REMOVE_ITEM ALL_CXX_TEST_IMPL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/tests/main.cpp")

set(ALL_CXX_IMPL_FILES ${ALL_CXX_SRC_IMPL_FILES} ${ALL_CXX_TEST_IMPL_FILES})

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

if(CLANG_TIDY)
  include(ProcessorCount)
  ProcessorCount(TIDY_DEFAULT_JOBS)
  if(TIDY_DEFAULT_JOBS EQUAL 0)
    set(TIDY_DEFAULT_JOBS 1)
  endif()
  set(TIDY_JOBS "${TIDY_DEFAULT_JOBS}" CACHE STRING "Maximum parallel clang-tidy processes")

  # run-clang-tidy gained -hide-progress in LLVM 20. On older toolchains
  # (e.g. Debian's clang-tidy) the flag is unknown, and run-clang-tidy's
  # argparse latches onto the leading "-h", prints --help, and exits 0 —
  # silently skipping every file while the build target reports success.
  # Only pass the flag when the installed run-clang-tidy advertises it.
  set(TIDY_HIDE_PROGRESS_FLAG "")
  if(RUN_CLANG_TIDY)
    execute_process(
      COMMAND ${RUN_CLANG_TIDY} --help
      OUTPUT_VARIABLE RUN_CLANG_TIDY_HELP
      ERROR_VARIABLE RUN_CLANG_TIDY_HELP
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(RUN_CLANG_TIDY_HELP MATCHES "-hide-progress")
      set(TIDY_HIDE_PROGRESS_FLAG "-hide-progress")
    endif()
  endif()

  set(TIDY_DB_DIR ${CMAKE_BINARY_DIR}/tidy)
  set(STRIP_TIDY_FLAGS_SCRIPT ${CMAKE_BINARY_DIR}/strip_tidy_flags.cmake)
  file(MAKE_DIRECTORY ${TIDY_DB_DIR})
  file(WRITE ${STRIP_TIDY_FLAGS_SCRIPT}
        "file(READ \"${CMAKE_BINARY_DIR}/compile_commands.json\" db)\n"
        "string(REPLACE \"-mno-direct-extern-access\" \"\" db \"\${db}\")\n"
        "file(WRITE \"${TIDY_DB_DIR}/compile_commands.json\" \"\${db}\")\n"
    )

  function(add_holonight_tidy_target target_name target_sources)
    if(RUN_CLANG_TIDY)
      add_custom_target(${target_name}
            COMMAND ${CMAKE_COMMAND} -P ${STRIP_TIDY_FLAGS_SCRIPT}
            COMMAND ${RUN_CLANG_TIDY}
                -quiet
                ${TIDY_HIDE_PROGRESS_FLAG}
                -j ${TIDY_JOBS}
                -p ${TIDY_DB_DIR}
                ${${target_sources}}
            COMMENT "Running ${target_name}"
            VERBATIM
        )
    else()
      add_custom_target(${target_name}
            COMMAND ${CMAKE_COMMAND} -P ${STRIP_TIDY_FLAGS_SCRIPT}
            COMMAND ${CLANG_TIDY} --quiet -p ${TIDY_DB_DIR} ${${target_sources}}
            COMMENT "Running ${target_name}"
            VERBATIM
        )
    endif()
  endfunction()

  add_holonight_tidy_target(tidy-src ALL_CXX_SRC_IMPL_FILES)
  add_holonight_tidy_target(tidy-tests ALL_CXX_TEST_IMPL_FILES)
  add_holonight_tidy_target(tidy ALL_CXX_IMPL_FILES)

  if(NOT RUN_CLANG_TIDY)
    message(STATUS "run-clang-tidy was not found; tidy targets will use clang-tidy sequentially")
  endif()
endif()

if(QMLLINT)
  add_custom_target(qml-lint
        COMMAND ${QMLLINT}
            --max-warnings 0
            --unqualified disable
            --unused-imports disable
            -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/qmllint"
            -I "${CMAKE_CURRENT_BINARY_DIR}/apps/chat"
            -I "${CMAKE_BINARY_DIR}"
            -I "${HOLONIGHT_QML_IMPORT_PATH}"
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
