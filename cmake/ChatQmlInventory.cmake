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
