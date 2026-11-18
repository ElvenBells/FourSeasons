# SnowOverlay — снежный оверлей для рабочего стола (Qt 6)
#
# Зависимости Debian 12:
#   sudo apt install build-essential cmake pkg-config qt6-base-dev qt6-wayland \
#                    libgl1-mesa-dev libx11-dev
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
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

set(SNOW_SOURCES
    src/main.cpp
    src/Constants.h
    src/SnowWindow.h
    src/SnowWindow.cpp
    src/ActiveWindowTracker.h
    src/ActiveWindowTracker.cpp
)
set(SNOW_LIBS Qt6::Widgets)
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

    # Чистый X11 (Xlib, _NET_ACTIVE_WINDOW) — запасной бэкенд для не-KDE окружений
    option(SNOW_WITH_X11 "Собрать X11-бэкенд (Xlib)" ON)
    if(SNOW_WITH_X11)
        find_package(X11)
        if(X11_FOUND)
            list(APPEND SNOW_SOURCES src/X11Tracker.h src/X11Tracker.cpp)
            list(APPEND SNOW_LIBS X11::X11)
            list(APPEND SNOW_DEFS SNOW_HAS_X11)
        else()
            message(WARNING "Xlib не найден (libx11-dev) — X11-бэкенд отключён")
        endif()
    endif()
endif()

add_executable(SnowOverlay ${SNOW_SOURCES})
target_link_libraries(SnowOverlay PRIVATE ${SNOW_LIBS})
target_compile_definitions(SnowOverlay PRIVATE ${SNOW_DEFS})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(SnowOverlay PRIVATE -Wall -Wextra)
endif()

install(TARGETS SnowOverlay RUNTIME DESTINATION bin)
