#pragma once

#include "SnowSurface.h"

#include <QObject>
#include <QList>
#include <QRect>
#include <QString>

// Абстрактный «определитель активного окна».
//
// Зачем интерфейс: способ узнать геометрию активного окна зависит от платформы.
//   * KDE Plasma (Wayland и X11) — KWin-скрипт + D-Bus   -> KWinTracker
//   * чистый X11 (любой WM)      — Xlib, _NET_ACTIVE_WINDOW -> X11Tracker
//   * в будущем: Windows (WinAPI), macOS (CGWindowList), GNOME, wlroots и т.д.
// SnowWindow ничего об этом не знает — ему нужен только сигнал activeRectChanged.
class ActiveWindowTracker : public QObject
{
    Q_OBJECT
public:
    explicit ActiveWindowTracker(QObject *parent = nullptr) : QObject(parent) {}
    ~ActiveWindowTracker() override = default;

    // Человекочитаемое имя бэкенда (для лога).
    virtual QString name() const = 0;

    // Запуск слежения. false — бэкенд на этой системе недоступен.
    virtual bool start() = 0;

    // Корректное завершение (например, выгрузить скрипт из KWin).
    virtual void stop() {}

    // Сообщить бэкенду, где находится оверлей (глобальные логические координаты).
    // Нужно тем бэкендам, которым приходится самим «прижимать» окно оверлея.
    virtual void setOverlayGeometry(const QRect &globalRect) { Q_UNUSED(globalRect); }
    virtual void setSeasonalCursorTracking(bool enabled) { Q_UNUSED(enabled); }

    // Фабрика: перебирает бэкенды и возвращает первый, который запустился.
    // nullptr — активное окно определить нечем (тогда снег рисуется на весь экран).
    static ActiveWindowTracker *create(QObject *parent, const QRect &overlayRect);

Q_SIGNALS:
    // Геометрия активного окна ВМЕСТЕ с рамкой и заголовком, в глобальных
    // логических координатах экрана. Пустой QRect — активного окна нет
    // (рабочий стол, панель, окно оверлея и т.п.).
    void activeRectChanged(const QRect &globalRect, bool fullscreen);
    // Видимые окна и панели текущего рабочего стола, снизу вверх.
    void surfacesChanged(const QList<SnowSurface> &surfaces);
    void cursorPosChanged(const QPoint &globalPosition);
};
