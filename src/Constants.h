#pragma once

// Общие идентификаторы. По ним KWin-скрипт узнаёт окно оверлея среди остальных
// окон системы, поэтому значения должны быть уникальными.
namespace SnowIds {

// Идентификатор приложения для Wayland, X11 и настроек.
inline constexpr char kAppId[] = "snowoverlay";

// Заголовок окна оверлея (в панели задач его не видно — окно скрыто скриптом).
inline constexpr char kWindowTitle[] = "SnowOverlay (overlay surface)";

} // namespace SnowIds

namespace SnowSettings {
inline constexpr int maxIntensity = 500;
inline constexpr int minSpeed = 25;
inline constexpr int maxSpeed = 400;
inline constexpr int maxWind = 100;
} // namespace SnowSettings
