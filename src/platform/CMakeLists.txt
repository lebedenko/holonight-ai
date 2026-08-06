add_library(holonight_platform STATIC
    include/holonight_platform/holonight_platform.h
    include/holonight_platform/desktop_notifier.h
    include/holonight_platform/panel_surface.h
    src/desktop_notifier.cpp
    src/panel_surface.cpp
)

qt6_generate_wayland_protocol_client_sources(holonight_platform
    FILES
        ${PROJECT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml
        ${XDG_SHELL_PROTOCOL}
)

add_custom_target(holonight_platform_protocol_headers
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/wayland-wlr-layer-shell-unstable-v1-client-protocol.h
        ${CMAKE_CURRENT_BINARY_DIR}/wayland-xdg-shell-client-protocol.h
)

target_include_directories(holonight_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(holonight_platform PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::DBus
    holonight_qt_gui_private
    ${WAYLAND_CLIENT_LIBRARIES}
)

target_include_directories(holonight_platform PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
# Protocol headers are generated C API and must be treated as system headers by clang-tidy.
target_compile_options(holonight_platform PRIVATE "-isystem${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(holonight_platform PUBLIC cxx_std_23)
