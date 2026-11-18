#pragma once

#include "ActiveWindowTracker.h"

#include <QRect>
#include <QTimer>

struct _XDisplay; // = Display из Xlib; сам Xlib.h в заголовок не тянем (он засоряет макросами)

// Запасной бэкенд: чистый X11 через Xlib (_NET_ACTIVE_WINDOW + _NET_FRAME_EXTENTS).
// Нужен для не-KDE окружений на X11. В Wayland-сессии под XWayland он видит
// только X11-окна, поэтому там приоритетнее KWinTracker.
//
// MVP: опрос по таймеру (30 мс). Позже можно заменить на подписку на
// PropertyNotify корневого окна через QAbstractNativeEventFilter.
class X11Tracker : public ActiveWindowTracker
{
    Q_OBJECT
public:
    explicit X11Tracker(QObject *parent = nullptr);
    ~X11Tracker() override;

    QString name() const override { return QStringLiteral("X11 (Xlib, _NET_ACTIVE_WINDOW)"); }
    bool start() override;
    void stop() override;

private Q_SLOTS:
    void poll();

private:
    QRect queryActiveFrame() const;

    _XDisplay *m_dpy = nullptr;
    unsigned long m_root = 0;
    unsigned long m_atomActive = 0;
    unsigned long m_atomExtents = 0;
    unsigned long m_atomType = 0;
    unsigned long m_atomTypeDesktop = 0;
    unsigned long m_atomTypeDock = 0;
    QTimer m_timer;
    QRect m_last;
};
