#pragma once

// Общие идентификаторы. По ним KWin-скрипт узнаёт окно оверлея среди остальных
// окон системы, поэтому значения должны быть уникальными.
namespace SnowIds {

// app_id (Wayland) / WM_CLASS (X11). KWin сравнивает без учёта регистра.
inline constexpr char kAppId[] = "snowoverlay";

// Заголовок окна оверлея (в панели задач его не видно — окно скрыто скриптом).
inline constexpr char kWindowTitle[] = "SnowOverlay (overlay surface)";

} // namespace SnowIds
