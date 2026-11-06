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

qt_add_executable(astro-ui
    src/main.cpp
    src/catalog.cpp
    src/sky_backend.cpp
    src/sky_chart.cpp
    src/mount_client.cpp
)

qt_add_resources(astro-ui "qml" PREFIX "/" FILES
    qml/Main.qml
    qml/AstroButton.qml
    qml/TonightPage.qml
    qml/SkyPage.qml
    qml/MountPage.qml
    qml/ToolsPage.qml
    qml/SettingsPage.qml
)

target_include_directories(astro-ui PRIVATE ${INDI_INCLUDE_DIR}/libindi)
target_link_libraries(astro-ui PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick
    ${ASTRO_CORE_LIBRARY} ${NOVA_LIBRARY} ${INDICLIENT_LIBRARY}
)

install(TARGETS astro-ui RUNTIME DESTINATION bin)
install(FILES data/stars.csv data/deepsky.csv data/ATTRIBUTION DESTINATION share/astro)

# Tests that run the real code against INDI's simulators. See tools/try-app.sh
option(ASTRO_UI_BUILD_TESTS "Build tests" OFF)
if(ASTRO_UI_BUILD_TESTS)
    qt_add_executable(mount_e2e tests/mount_e2e.cpp src/catalog.cpp src/sky_backend.cpp src/mount_client.cpp)
    target_include_directories(mount_e2e PRIVATE src ${INDI_INCLUDE_DIR}/libindi)
    target_link_libraries(mount_e2e PRIVATE Qt6::Core ${ASTRO_CORE_LIBRARY} ${NOVA_LIBRARY} ${INDICLIENT_LIBRARY})

    qt_add_executable(ui_screens tests/ui_screens.cpp src/catalog.cpp src/sky_backend.cpp src/sky_chart.cpp src/mount_client.cpp)
    qt_add_resources(ui_screens "qml" PREFIX "/" FILES
        qml/Main.qml qml/AstroButton.qml qml/TonightPage.qml qml/SkyPage.qml
        qml/MountPage.qml qml/ToolsPage.qml qml/SettingsPage.qml)
    target_include_directories(ui_screens PRIVATE src ${INDI_INCLUDE_DIR}/libindi)
    target_link_libraries(ui_screens PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
        ${ASTRO_CORE_LIBRARY} ${NOVA_LIBRARY} ${INDICLIENT_LIBRARY})
endif()
