cmake_minimum_required(VERSION 3.21)
project(astro-ui CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Qml Quick)
find_library(ASTRO_CORE_LIBRARY astro-core REQUIRED)
find_library(NOVA_LIBRARY nova REQUIRED)
find_library(INDICLIENT_LIBRARY indiclient REQUIRED)
find_path(INDI_INCLUDE_DIR libindi/baseclient.h REQUIRED)
find_library(TCL_LIBRARY NAMES tcl8.6 tcl REQUIRED)
find_library(READLINE_LIBRARY readline REQUIRED)

qt_add_executable(astro-ui
    src/main.cpp
    src/catalog.cpp
    src/auto_detect.cpp
    src/sky_backend.cpp
    src/sky_chart.cpp
    src/mount_client.cpp
    src/input_mode.cpp
)

qt_add_resources(astro-ui "qml" PREFIX "/" FILES
    qml/Main.qml
    qml/AText.qml
    qml/AstroButton.qml
    qml/FocusRing.qml
    qml/Card.qml
    qml/Glyph.qml
    qml/Logo.qml
    qml/MoonDisc.qml
    qml/PolarDial.qml
    qml/TonightPage.qml
    qml/SkyPage.qml
    qml/MountPage.qml
    qml/ToolsPage.qml
    qml/SettingsPage.qml
    data/fonts/IBMPlexSans.ttf
    data/fonts/IBMPlexMono-Regular.ttf
    data/fonts/IBMPlexMono-Medium.ttf
    data/fonts/SpaceGrotesk.ttf
)

target_include_directories(astro-ui PRIVATE ${INDI_INCLUDE_DIR}/libindi)
target_link_libraries(astro-ui PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick
    ${ASTRO_CORE_LIBRARY} ${NOVA_LIBRARY} ${INDICLIENT_LIBRARY}
)

# The interactive shell. It shares the settings and safety checks, but needs no display.
qt_add_executable(astro_sky
    src/astro_sky_shell.cpp
    src/catalog.cpp
    src/auto_detect.cpp
    src/sky_backend.cpp
    src/mount_client.cpp
)
target_include_directories(astro_sky PRIVATE ${INDI_INCLUDE_DIR}/libindi)
target_link_libraries(astro_sky PRIVATE
    Qt6::Core Qt6::Network
    ${ASTRO_CORE_LIBRARY} ${NOVA_LIBRARY} ${INDICLIENT_LIBRARY} ${TCL_LIBRARY} ${READLINE_LIBRARY}
)

install(TARGETS astro-ui astro_sky RUNTIME DESTINATION bin)
install(FILES data/stars.csv data/stars-faint.bin data/deepsky.csv data/constellations.csv data/constellation-names.csv data/milkyway.png data/moon-map.jpg data/ATTRIBUTION data/fonts/OFL-IBMPlex.txt data/fonts/OFL-SpaceGrotesk.txt DESTINATION share/astro)

# Tests that run the real code against INDI's simulators. See tools/try-app.sh
option(ASTRO_UI_BUILD_TESTS "Build tests" OFF)
if(ASTRO_UI_BUILD_TESTS)
    add_executable(test_chart_math tests/test_chart_math.cpp)
    target_include_directories(test_chart_math PRIVATE src)

    qt_add_executable(test_chart_tap tests/test_chart_tap.cpp src/catalog.cpp src/auto_detect.cpp src/sky_backend.cpp src/sky_chart.cpp)
    target_include_directories(test_chart_tap PRIVATE src)
    target_link_libraries(test_chart_tap PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick
        ${ASTRO_CORE_LIBRARY} ${NOVA_LIBRARY})

    qt_add_executable(test_auto_detect tests/test_auto_detect.cpp src/auto_detect.cpp)
    target_include_directories(test_auto_detect PRIVATE src)
    target_link_libraries(test_auto_detect PRIVATE Qt6::Core)

    qt_add_executable(mount_e2e tests/mount_e2e.cpp src/catalog.cpp src/auto_detect.cpp src/sky_backend.cpp src/mount_client.cpp)
    target_include_directories(mount_e2e PRIVATE src ${INDI_INCLUDE_DIR}/libindi)
    target_link_libraries(mount_e2e PRIVATE Qt6::Core Qt6::Network ${ASTRO_CORE_LIBRARY} ${NOVA_LIBRARY} ${INDICLIENT_LIBRARY})

    set(ASTRO_UI_TEST_RESOURCES
        qml/Main.qml qml/AText.qml qml/AstroButton.qml qml/FocusRing.qml qml/Card.qml qml/Glyph.qml qml/Logo.qml qml/MoonDisc.qml qml/PolarDial.qml qml/TonightPage.qml qml/SkyPage.qml qml/MountPage.qml qml/ToolsPage.qml qml/SettingsPage.qml data/fonts/IBMPlexSans.ttf data/fonts/IBMPlexMono-Regular.ttf data/fonts/IBMPlexMono-Medium.ttf data/fonts/SpaceGrotesk.ttf)
    foreach(ui_test ui_screens ui_e2e)
        qt_add_executable(${ui_test} tests/${ui_test}.cpp src/catalog.cpp src/auto_detect.cpp src/sky_backend.cpp src/sky_chart.cpp src/mount_client.cpp src/input_mode.cpp)
        qt_add_resources(${ui_test} "qml" PREFIX "/" FILES ${ASTRO_UI_TEST_RESOURCES})
        target_include_directories(${ui_test} PRIVATE src ${INDI_INCLUDE_DIR}/libindi)
        # GuiPrivate is for sending real key and wheel events, as a keyboard and mouse do
        target_link_libraries(${ui_test} PRIVATE Qt6::GuiPrivate Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick
            ${ASTRO_CORE_LIBRARY} ${NOVA_LIBRARY} ${INDICLIENT_LIBRARY})
    endforeach()
endif()
