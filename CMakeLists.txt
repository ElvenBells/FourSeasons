# SnowOverlay — сезонный оверлей для рабочего стола (Qt 6)
#
# Зависимости Debian 12:
#   sudo apt install build-essential cmake pkg-config qt6-base-dev qt6-wayland \
#                    libgl1-mesa-dev libx11-dev libxext-dev qt6-svg-plugins
#
cmake_minimum_required(VERSION 3.16)
project(SnowOverlay VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Debian 12 поставляет Qt 6.4.x
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)

set(SNOW_SOURCES
    src/main.cpp
    src/Constants.h
    src/Precipitation.h
    src/SnowWindow.h
    src/SnowWindow.cpp
    src/Season.h
    src/SeasonalWeather.h
    src/SeasonalWeather.cpp
    src/SeasonalScenery.h
    src/SeasonalScenery.cpp
    src/SnowScene.h
    src/SnowScene.cpp
    src/SnowProps.h
    src/SnowProps.cpp
    src/SnowSurface.h
    src/SnowCrystal.h
    src/SnowCrystal.cpp
    src/ActiveWindowTracker.h
    src/ActiveWindowTracker.cpp
)
set(SNOW_LIBS Qt6::Widgets Qt6::Network)
set(SNOW_DEFS "")

# ---------------------------------------------------------------------------
# Платформенные бэкенды. Сейчас — только Linux/Unix.
# Для Windows/macOS в будущем сюда добавятся WinTracker / MacTracker.
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    # KWin (Plasma 5.27 и Plasma 6, и Wayland, и X11): D-Bus + KWin-скрипт
    find_package(Qt6 6.4 REQUIRED COMPONENTS DBus)
    list(APPEND SNOW_SOURCES src/KWinTracker.h src/KWinTracker.cpp)
    list(APPEND SNOW_LIBS Qt6::DBus)
    list(APPEND SNOW_DEFS SNOW_HAS_KWIN)

    # Слой поверх всплывающего меню Plasma на Wayland.
    find_package(LayerShellQt QUIET)
    if(TARGET LayerShellQt::Interface)
        list(APPEND SNOW_SOURCES src/PanelGarland.cpp)
        list(APPEND SNOW_LIBS LayerShellQt::Interface)
        list(APPEND SNOW_DEFS SNOW_HAS_LAYER_SHELL)
    endif()

    # Xlib нужен для проверки композитора даже при отключённом трекере X11.
    find_package(X11)
    if(X11_FOUND)
        list(APPEND SNOW_LIBS X11::X11)
        list(APPEND SNOW_DEFS SNOW_HAS_XLIB)
        if(TARGET X11::Xext)
            list(APPEND SNOW_LIBS X11::Xext)
            list(APPEND SNOW_DEFS SNOW_HAS_XSHAPE)
        else()
            message(WARNING "Xext не найден (libxext-dev) — игровое управление кнопками мыши на X11 отключено")
        endif()
    else()
        message(WARNING "Xlib не найден (libx11-dev) — запуск через xcb отключён")
    endif()

    # Чистый X11 (Xlib, _NET_ACTIVE_WINDOW) — запасной бэкенд для не-KDE окружений
    option(SNOW_WITH_X11 "Собрать X11-бэкенд (Xlib)" ON)
    if(SNOW_WITH_X11 AND X11_FOUND)
        list(APPEND SNOW_SOURCES src/X11Tracker.h src/X11Tracker.cpp)
        list(APPEND SNOW_DEFS SNOW_HAS_X11)
    endif()
endif()

add_executable(SnowOverlay ${SNOW_SOURCES})
qt_add_resources(SnowOverlay "season_icons"
    PREFIX "/"
    FILES assets/snowoverlay.svg assets/winter.svg assets/spring.svg assets/summer.svg assets/autumn.svg
)
target_link_libraries(SnowOverlay PRIVATE ${SNOW_LIBS})
target_compile_definitions(SnowOverlay PRIVATE ${SNOW_DEFS})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(SnowOverlay PRIVATE -Wall -Wextra)
endif()

install(TARGETS SnowOverlay RUNTIME DESTINATION bin)
install(FILES assets/snowoverlay.desktop DESTINATION share/applications)
install(FILES assets/snowoverlay.svg DESTINATION share/icons/hicolor/scalable/apps)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.4 QUIET COMPONENTS Test)
    if(TARGET Qt6::Test)
        add_executable(SnowSceneTest tests/SnowSceneTest.cpp src/SnowScene.cpp src/SnowCrystal.cpp src/SnowProps.cpp)
        target_include_directories(SnowSceneTest PRIVATE src)
        target_link_libraries(SnowSceneTest PRIVATE Qt6::Gui Qt6::Test)
        add_test(NAME SnowSceneTest COMMAND SnowSceneTest)

        add_executable(SnowWindowTest tests/SnowWindowTest.cpp src/SnowWindow.cpp src/SnowScene.cpp
                       src/SnowCrystal.cpp src/SnowProps.cpp src/SeasonalWeather.cpp src/SeasonalScenery.cpp)
        target_include_directories(SnowWindowTest PRIVATE src)
        target_link_libraries(SnowWindowTest PRIVATE Qt6::Widgets Qt6::Test)
        add_test(NAME SnowWindowTest COMMAND SnowWindowTest)
        set_tests_properties(SnowWindowTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
