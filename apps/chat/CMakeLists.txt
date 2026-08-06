qt_add_executable(holonight-chat
    main.cpp
    app/ChatApplication.h
    app/ChatApplication.cpp
    app/ChatDbusAdaptor.h
    app/ChatDbusAdaptor.cpp
)

set_target_properties(holonight-chat PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)

file(GLOB_RECURSE HOLONIGHT_CHAT_QML_FILES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/qml/*.qml"
)
list(SORT HOLONIGHT_CHAT_QML_FILES)

foreach(qml_file IN LISTS HOLONIGHT_CHAT_QML_FILES)
    cmake_path(RELATIVE_PATH qml_file
        BASE_DIRECTORY "${PROJECT_SOURCE_DIR}/qml"
        OUTPUT_VARIABLE qml_alias)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()

file(GLOB HOLONIGHT_CHAT_PROVIDER_ICONS
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/assets/providers/*.svg"
)
list(SORT HOLONIGHT_CHAT_PROVIDER_ICONS)

file(GLOB HOLONIGHT_CHAT_ACTION_ICONS
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/assets/icons/*.svg"
)
list(SORT HOLONIGHT_CHAT_ACTION_ICONS)

set(HOLONIGHT_CHAT_ICONS
    ${HOLONIGHT_CHAT_PROVIDER_ICONS}
    ${HOLONIGHT_CHAT_ACTION_ICONS}
    "${PROJECT_SOURCE_DIR}/assets/holonight-ai.svg"
)

foreach(icon_file IN LISTS HOLONIGHT_CHAT_ICONS)
    cmake_path(RELATIVE_PATH icon_file
        BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE icon_alias)
    set_source_files_properties("${icon_file}" PROPERTIES QT_RESOURCE_ALIAS "${icon_alias}")
endforeach()

qt_add_qml_module(holonight-chat
    URI HolonightChat
    VERSION 1.0
    RESOURCE_PREFIX "/"
    TYPEINFO HolonightChat.qmltypes
    NO_GENERATE_QMLTYPES
    NO_IMPORT_SCAN
    QML_FILES ${HOLONIGHT_CHAT_QML_FILES}
    RESOURCES ${HOLONIGHT_CHAT_ICONS}
)

target_link_libraries(holonight-chat PRIVATE
    HolonightQt::Controls
    holonight_domain
    holonight_application
    holonight_providers
    holonight_persistence
    holonight_credentials
    holonight_platform
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
)

# holonight_rendering is already pulled into holonight-chat's link line transitively (static-lib
# archive propagation through holonight_application's PRIVATE link — see
# src/application/CMakeLists.txt's comment on REQ-C-005). But the *generated*
# holonight-chat_qmltyperegistrations.cpp below is compiled as part of the holonight-chat target
# itself, and its __has_include(<code_highlighter.h>)-guarded includes only succeed if
# holonight_rendering's PUBLIC include directories reach holonight-chat's own compile line —
# PRIVATE linkage through holonight_application does not relay usage requirements that far. Linking
# it here too (redundant for the archive, load-bearing for the include path) is what makes that
# registration TU compile; holonight_application's own link to it stays PRIVATE, so REQ-C-005 still
# holds for every other consumer.
target_link_libraries(holonight-chat PRIVATE
    holonight_rendering
)

# ChatViewModel/MessageListModel (QML_ELEMENT types) live in holonight_application, a static
# library — qt_add_qml_module() only sees metatypes attached directly to the holonight-chat
# executable target, so they must be extracted and merged in manually. See CLAUDE.md's "Future:
# QML Singletons From Static Libraries" section and holonight-shell/apps/shell/CMakeLists.txt for
# the pattern this is ported from.
set_property(TARGET holonight_application PROPERTY INTERFACE_SOURCES "")

qt6_extract_metatypes(holonight_application OUTPUT_FILES _HOLONIGHT_APPLICATION_METATYPES)

# qt6_extract_metatypes() re-populates INTERFACE_SOURCES on holonight_application as a side effect
# of its own extraction. Left in place, _qt_internal_qml_type_registration(holonight-chat) below
# would walk that non-empty INTERFACE_SOURCES again via its own foreign-types link-dependency
# discovery, re-registering every type a second time (ODR-violation warnings, e.g. "Multiple C++
# types called holonight_application::ChatViewModel found") on top of the manually merged file
# below. Clearing it again here mirrors holonight-shell/apps/shell/CMakeLists.txt's proven pattern.
set_property(TARGET holonight_application PROPERTY INTERFACE_SOURCES "")

# Same extraction, same clear-twice dance, for holonight_rendering (ContentBlockTypeNs,
# ContentBlock, CodeHighlighter are all QML-visible types declared there). holonight_rendering is
# in holonight-chat's transitive link graph (via holonight_application, even though that link is
# PRIVATE) so _qt_internal_qml_type_registration()'s foreign-types walk would re-discover it too if
# its own INTERFACE_SOURCES were left populated after this extraction.
set_property(TARGET holonight_rendering PROPERTY INTERFACE_SOURCES "")
qt6_extract_metatypes(holonight_rendering OUTPUT_FILES _HOLONIGHT_RENDERING_METATYPES)
set_property(TARGET holonight_rendering PROPERTY INTERFACE_SOURCES "")

# qt6_extract_metatypes()'s OUTPUT_FILES value is the BYPRODUCTS path (a dummy file touched at
# configure time), not a real ninja OUTPUT — depending on it directly does not establish a build
# order. The actual declared OUTPUT is the same path with ".gen" appended (see
# Qt6CoreMacros.cmake's qt6_extract_metatypes: add_custom_command(OUTPUT ${metatypes_file_gen}
# BYPRODUCTS ${metatypes_file} ...)). holonight-shell's apps/shell/CMakeLists.txt depends on the
# ".gen" variant for exactly this reason — mirrored here.
set(_HOLONIGHT_APPLICATION_METATYPES_DEP "${_HOLONIGHT_APPLICATION_METATYPES}.gen")
set(_HOLONIGHT_RENDERING_METATYPES_DEP "${_HOLONIGHT_RENDERING_METATYPES}.gen")

set(_HOLONIGHT_CHAT_METATYPES "${CMAKE_CURRENT_BINARY_DIR}/meta_types/qt6holonight-chat_metatypes.json")
add_custom_command(
    OUTPUT "${_HOLONIGHT_CHAT_METATYPES}"
    DEPENDS
        ${_HOLONIGHT_APPLICATION_METATYPES_DEP}
        ${_HOLONIGHT_RENDERING_METATYPES_DEP}
        "${PROJECT_SOURCE_DIR}/cmake/combine-metatypes.cmake"
    COMMAND ${CMAKE_COMMAND}
        "-DINPUT_FILES=${_HOLONIGHT_APPLICATION_METATYPES}|${_HOLONIGHT_RENDERING_METATYPES}"
        "-DOUTPUT=${_HOLONIGHT_CHAT_METATYPES}"
        -P "${PROJECT_SOURCE_DIR}/cmake/combine-metatypes.cmake"
    COMMENT "Collecting HolonightChat C++ metatypes"
    VERBATIM
)
_qt_internal_assign_build_metatypes_files_and_properties(holonight-chat
    METATYPES_FILE_NAME "qt6holonight-chat_metatypes.json"
    METATYPES_FILE_PATH "${_HOLONIGHT_CHAT_METATYPES}"
)
set_target_properties(holonight-chat PROPERTIES _qt_internal_has_qmltypes TRUE)
_qt_internal_qml_type_registration(holonight-chat)

target_compile_definitions(holonight-chat PRIVATE
    HOLONIGHT_CHAT_VERSION="${PROJECT_VERSION}"
    HOLONIGHT_QML_IMPORT_PATH="${HOLONIGHT_QML_IMPORT_PATH}"
)

target_include_directories(holonight-chat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
)

target_compile_features(holonight-chat PRIVATE cxx_std_23)

install(TARGETS holonight-chat DESTINATION ${CMAKE_INSTALL_BINDIR})

configure_file(
    "${PROJECT_SOURCE_DIR}/data/dbus-1/services/org.holonight.Chat.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/org.holonight.Chat.service"
    @ONLY
)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/org.holonight.Chat.service"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/dbus-1/services"
)
