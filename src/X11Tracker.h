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
    void pollSurfaces();

private:
    QRect queryActiveFrame(bool &fullscreen) const;
    QRect queryFrame(unsigned long window, bool dock) const;

    _XDisplay *m_dpy = nullptr;
    unsigned long m_root = 0;
    unsigned long m_atomActive = 0;
    unsigned long m_atomExtents = 0;
    unsigned long m_atomType = 0;
    unsigned long m_atomTypeDesktop = 0;
    unsigned long m_atomTypeDock = 0;
    unsigned long m_atomClientList = 0;
    unsigned long m_atomStacking = 0;
    unsigned long m_atomState = 0;
    unsigned long m_atomHidden = 0;
    unsigned long m_atomFullscreen = 0;
    unsigned long m_atomMaximizedHorz = 0;
    unsigned long m_atomMaximizedVert = 0;
    unsigned long m_atomDesktop = 0;
    unsigned long m_atomCurrentDesktop = 0;
    unsigned long m_atomShowingDesktop = 0;
    bool m_showingDesktop = false;
    unsigned long m_atomAppletPopup = 0;
    QList<long> m_windowTypes;
    QTimer m_timer;
    QTimer m_surfaceTimer;
    QRect m_last;
    bool m_lastFullscreen = false;
    QList<SnowSurface> m_surfaces;
};
