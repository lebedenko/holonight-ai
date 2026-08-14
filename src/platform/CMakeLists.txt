add_library(holonight_platform STATIC
    include/holonight_platform/holonight_platform.h
    include/holonight_platform/desktop_notifier.h
    include/holonight_platform/panel_surface.h
    src/desktop_notifier.cpp
    src/panel_surface.cpp
)

target_include_directories(holonight_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(holonight_platform PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::DBus
    HolonightQt::Wayland
)
target_compile_features(holonight_platform PUBLIC cxx_std_23)
