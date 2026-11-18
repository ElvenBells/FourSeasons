#include "ActiveWindowTracker.h"

#include <QDebug>
#include <QGuiApplication>
#include <QList>
#include <utility>

#ifdef SNOW_HAS_KWIN
#include "KWinTracker.h"
#endif
#ifdef SNOW_HAS_X11
#include "X11Tracker.h"
#endif

ActiveWindowTracker *ActiveWindowTracker::create(QObject *parent, const QRect &overlayRect)
{
    const QString platform = QGuiApplication::platformName();

    // Порядок важен: сначала самый точный бэкенд.
    QList<ActiveWindowTracker *> candidates;

#ifdef SNOW_HAS_KWIN
    // KWin видит ВСЕ окна (и нативные Wayland, и XWayland), поэтому он
    // предпочтительнее Xlib даже при запуске через QT_QPA_PLATFORM=xcb.
    candidates << new KWinTracker(parent);
#endif
#ifdef SNOW_HAS_X11
    // Xlib имеет смысл только если мы сами — X11-клиент. Под Wayland он увидит
    // лишь XWayland-окна, но это лучше, чем ничего.
    if (platform == QLatin1String("xcb"))
        candidates << new X11Tracker(parent);
#endif

    ActiveWindowTracker *chosen = nullptr;
    for (ActiveWindowTracker *t : std::as_const(candidates)) {
        if (!chosen) {
            t->setOverlayGeometry(overlayRect);
            if (t->start()) {
                qInfo().noquote() << "[SnowOverlay] Трекер активного окна:" << t->name();
                chosen = t;
                continue;
            }
            qInfo().noquote() << "[SnowOverlay] Бэкенд недоступен:" << t->name();
        }
        delete t; // неудачные и лишние кандидаты
    }

    if (!chosen)
        qWarning() << "[SnowOverlay] Активное окно определить не удаётся (платформа:"
                   << platform << ") — снег будет падать по всему экрану.";
    return chosen;
}
