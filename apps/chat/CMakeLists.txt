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

include("${PROJECT_SOURCE_DIR}/cmake/ChatQmlInventory.cmake")

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
    QMarkdown::QMarkdown
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

# qt6_extract_metatypes()'s OUTPUT_FILES value is the BYPRODUCTS path (a dummy file touched at
# configure time), not a real ninja OUTPUT — depending on it directly does not establish a build
# order. The actual declared OUTPUT is the same path with ".gen" appended (see
# Qt6CoreMacros.cmake's qt6_extract_metatypes: add_custom_command(OUTPUT ${metatypes_file_gen}
# BYPRODUCTS ${metatypes_file} ...)). holonight-shell's apps/shell/CMakeLists.txt depends on the
# ".gen" variant for exactly this reason — mirrored here.
set(_HOLONIGHT_APPLICATION_METATYPES_DEP "${_HOLONIGHT_APPLICATION_METATYPES}.gen")

set(_HOLONIGHT_CHAT_METATYPES "${CMAKE_CURRENT_BINARY_DIR}/meta_types/qt6holonight-chat_metatypes.json")
add_custom_command(
    OUTPUT "${_HOLONIGHT_CHAT_METATYPES}"
    DEPENDS
        ${_HOLONIGHT_APPLICATION_METATYPES_DEP}
        "${PROJECT_SOURCE_DIR}/cmake/combine-metatypes.cmake"
    COMMAND ${CMAKE_COMMAND}
        "-DINPUT_FILES=${_HOLONIGHT_APPLICATION_METATYPES}"
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
    QMARKDOWN_QML_IMPORT_PATH="${QMarkdown_QML_IMPORT_PATH}"
    HOLONIGHT_QML_IMPORT_PATH="${HOLONIGHT_QML_IMPORT_PATH}"
    HOLONIGHT_BUILD_EXECUTABLE="$<TARGET_FILE:holonight-chat>"
    HOLONIGHT_INSTALL_QML_PATH="../${CMAKE_INSTALL_LIBDIR}/qt6/qml"
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

qt_add_resources(holonight-chat controls_style_config
    PREFIX "/" FILES qtquickcontrols2.conf)

install(FILES "${PROJECT_SOURCE_DIR}/data/applications/org.holonight.Chat.desktop"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES "${PROJECT_SOURCE_DIR}/assets/holonight-ai.svg"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
