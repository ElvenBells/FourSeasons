#include "X11Tracker.h"

#include <QDebug>
#include <QGuiApplication>
#include <QList>
#include <utility>
#include <QScreen>

// Xlib подключаем ПОСЛЕДНИМ: он определяет макросы (None, Bool, Status, ...),
// которые конфликтуют с идентификаторами Qt.
#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace {

// --- Защита от X-ошибок -----------------------------------------------------
// Окно может исчезнуть между «узнали ID» и «запросили геометрию». Стандартный
// обработчик Xlib в этом случае вызывает exit() — программа бы просто умерла.
// Поэтому на время запросов ставим свой обработчик, который ошибку запоминает.
bool g_xError = false;

int trapHandler(Display *, XErrorEvent *)
{
    g_xError = true;
    return 0;
}

class ErrorTrap
{
public:
    explicit ErrorTrap(Display *dpy) : m_dpy(dpy)
    {
        g_xError = false;
        m_old = XSetErrorHandler(trapHandler);
    }
    ~ErrorTrap()
    {
        XSync(m_dpy, False);
        XSetErrorHandler(m_old);
    }
    bool failed() const
    {
        XSync(m_dpy, False);
        return g_xError;
    }

private:
    Display *m_dpy;
    XErrorHandler m_old;
};

// Читает свойство окна формата 32 бита (для Xlib это массив long).
bool getLongs(Display *dpy, Window win, Atom prop, Atom type, long count, QList<long> &out)
{
    Atom actualType = 0;
    int actualFormat = 0;
    unsigned long nItems = 0;
    unsigned long bytesAfter = 0;
    unsigned char *data = nullptr;

    const int rc = XGetWindowProperty(dpy, win, prop, 0, count, False, type, &actualType,
                                      &actualFormat, &nItems, &bytesAfter, &data);
    if (rc != Success || !data)
        return false;

    if (actualFormat == 32) {
        const long *arr = reinterpret_cast<const long *>(data);
        for (unsigned long i = 0; i < nItems; ++i)
            out.append(arr[i]);
    }
    XFree(data);
    return !out.isEmpty();
}

QRect logicalRect(int x, int y, int width, int height)
{
    const QScreen *primary = QGuiApplication::primaryScreen();
    const qreal scale = primary ? primary->devicePixelRatio() : 1.0;
    const QPoint origin = primary ? primary->geometry().topLeft() : QPoint();
    return QRect(origin.x() + qRound((x - origin.x()) / scale),
                 origin.y() + qRound((y - origin.y()) / scale),
                 qRound(width / scale), qRound(height / scale));
}

} // namespace

// ===========================================================================

X11Tracker::X11Tracker(QObject *parent)
    : ActiveWindowTracker(parent)
{
    m_timer.setInterval(30);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &X11Tracker::poll);
    m_surfaceTimer.setInterval(100);
    connect(&m_surfaceTimer, &QTimer::timeout, this, &X11Tracker::pollSurfaces);
}

X11Tracker::~X11Tracker()
{
    stop();
}

bool X11Tracker::start()
{
    // Отдельное соединение с X-сервером (не мешаем соединению Qt).
    // В чистом Wayland без XWayland вернёт nullptr — это нормальный «нет».
    m_dpy = XOpenDisplay(nullptr);
    if (!m_dpy)
        return false;

    m_root = DefaultRootWindow(m_dpy);
    m_atomActive      = XInternAtom(m_dpy, "_NET_ACTIVE_WINDOW", False);
    m_atomExtents     = XInternAtom(m_dpy, "_NET_FRAME_EXTENTS", False);
    m_atomType        = XInternAtom(m_dpy, "_NET_WM_WINDOW_TYPE", False);
    m_atomTypeDesktop = XInternAtom(m_dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    m_atomTypeDock    = XInternAtom(m_dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    m_atomClientList  = XInternAtom(m_dpy, "_NET_CLIENT_LIST", False);
    m_atomStacking    = XInternAtom(m_dpy, "_NET_CLIENT_LIST_STACKING", False);
    m_atomState       = XInternAtom(m_dpy, "_NET_WM_STATE", False);
    m_atomHidden      = XInternAtom(m_dpy, "_NET_WM_STATE_HIDDEN", False);
    m_atomFullscreen  = XInternAtom(m_dpy, "_NET_WM_STATE_FULLSCREEN", False);
    m_atomMaximizedHorz = XInternAtom(m_dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    m_atomMaximizedVert = XInternAtom(m_dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    m_atomDesktop     = XInternAtom(m_dpy, "_NET_WM_DESKTOP", False);
    m_atomCurrentDesktop = XInternAtom(m_dpy, "_NET_CURRENT_DESKTOP", False);
    m_atomShowingDesktop = XInternAtom(m_dpy, "_NET_SHOWING_DESKTOP", False);
    m_atomAppletPopup = XInternAtom(m_dpy, "_KDE_NET_WM_WINDOW_TYPE_APPLET_POPUP", False);
    m_windowTypes = {static_cast<long>(XInternAtom(m_dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False)),
                     static_cast<long>(XInternAtom(m_dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False)),
                     static_cast<long>(XInternAtom(m_dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False))};

    m_timer.start();
    m_surfaceTimer.start();
    QTimer::singleShot(0, this, &X11Tracker::pollSurfaces);
    return true;
}

void X11Tracker::stop()
{
    m_timer.stop();
    m_surfaceTimer.stop();
    if (m_dpy) {
        XCloseDisplay(m_dpy);
        m_dpy = nullptr;
    }
}

void X11Tracker::poll()
{
    if (!m_dpy)
        return;

    QRect result;
    bool fullscreen = false;
    if (!m_showingDesktop) {
        ErrorTrap trap(m_dpy);
        result = queryActiveFrame(fullscreen);
        if (trap.failed())
            result = QRect(); // окно успело исчезнуть — считаем, что активного нет
    }

    fullscreen = fullscreen && !result.isEmpty();
    if (result != m_last || fullscreen != m_lastFullscreen) {
        m_last = result;
        m_lastFullscreen = fullscreen;
        emit activeRectChanged(result, fullscreen);
    }
}

void X11Tracker::pollSurfaces()
{
    if (!m_dpy)
        return;

    QList<SnowSurface> surfaces;
    bool showingDesktop = false;
    {
        ErrorTrap trap(m_dpy);
        QList<long> desktopMode;
        showingDesktop = getLongs(m_dpy, m_root, m_atomShowingDesktop, XA_CARDINAL, 1, desktopMode)
                         && desktopMode.first() != 0;
        QList<long> windows;
        if (!getLongs(m_dpy, m_root, m_atomStacking, XA_WINDOW, 4096, windows))
            getLongs(m_dpy, m_root, m_atomClientList, XA_WINDOW, 4096, windows);
        QList<long> currentDesktop;
        getLongs(m_dpy, m_root, m_atomCurrentDesktop, XA_CARDINAL, 1, currentDesktop);
        // AppletPopup в Plasma может быть override-redirect и отсутствовать в _NET_CLIENT_LIST.
        Window root = 0, parent = 0, *children = nullptr;
        unsigned int childCount = 0;
        if (XQueryTree(m_dpy, m_root, &root, &parent, &children, &childCount)) {
            for (unsigned int i = 0; i < childCount; ++i) {
                if (windows.contains(children[i]))
                    continue;
                QList<long> types;
                if (getLongs(m_dpy, children[i], m_atomType, XA_ATOM, 8, types) &&
                    types.contains(static_cast<long>(m_atomAppletPopup)))
                    windows.append(children[i]);
            }
            if (children)
                XFree(children);
        }
        for (const Window window : windows) {
            QList<long> types;
            getLongs(m_dpy, window, m_atomType, XA_ATOM, 8, types);
            const bool dock = types.contains(static_cast<long>(m_atomTypeDock));
            const bool popup = types.contains(static_cast<long>(m_atomAppletPopup));
            if (!dock && !popup && (showingDesktop || (!types.isEmpty() && !m_windowTypes.contains(types.first()))))
                continue;
            QList<long> states;
            getLongs(m_dpy, window, m_atomState, XA_ATOM, 32, states);
            if (!dock && !currentDesktop.isEmpty()) {
                QList<long> desktop;
                if (getLongs(m_dpy, window, m_atomDesktop, XA_CARDINAL, 1, desktop) &&
                    static_cast<quint32>(desktop.first()) != 0xffffffffu && desktop.first() != currentDesktop.first())
                    continue;
            }
            if (states.contains(static_cast<long>(m_atomHidden)))
                continue;
            const QString id = QString::number(window);
            const QRect frame = queryFrame(window, dock);
            if (!frame.isEmpty()) {
                const bool maximized = states.contains(static_cast<long>(m_atomMaximizedHorz)) &&
                                       states.contains(static_cast<long>(m_atomMaximizedVert));
                surfaces.append({id, frame, dock, states.contains(static_cast<long>(m_atomFullscreen)), maximized, popup});
            }
        }
    }
    if (surfaces != m_surfaces) {
        m_surfaces = surfaces;
        emit surfacesChanged(surfaces);
    }
    if (m_showingDesktop != showingDesktop) {
        m_showingDesktop = showingDesktop;
        poll();
    }
}

QRect X11Tracker::queryActiveFrame(bool &fullscreen) const
{
    // 1) Какое окно активно? (_NET_ACTIVE_WINDOW на корневом окне)
    QList<long> v;
    if (!getLongs(m_dpy, m_root, m_atomActive, XA_WINDOW, 1, v) || v.first() == 0)
        return QRect();
    const Window win = static_cast<Window>(v.first());

    // 2) Рабочий стол и панели не считаем «окном», которое надо вырезать.
    QList<long> types;
    if (getLongs(m_dpy, win, m_atomType, XA_ATOM, 8, types)) {
        for (long t : std::as_const(types)) {
            const Atom a = static_cast<Atom>(t);
            if (a == m_atomTypeDesktop || a == m_atomTypeDock)
                return QRect();
        }
    }

    QList<long> states;
    getLongs(m_dpy, win, m_atomState, XA_ATOM, 32, states);
    if (states.contains(static_cast<long>(m_atomHidden)))
        return {};
    fullscreen = states.contains(static_cast<long>(m_atomFullscreen));
    return queryFrame(win, false);
}

QRect X11Tracker::queryFrame(unsigned long win, bool dock) const
{
    XWindowAttributes attributes{};
    if (!XGetWindowAttributes(m_dpy, win, &attributes) || attributes.map_state != IsViewable)
        return {};
    int rx = 0, ry = 0;
    Window child = 0;
    if (!XTranslateCoordinates(m_dpy, win, m_root, 0, 0, &rx, &ry, &child))
        return QRect();

    // 4) Рамка и заголовок, которые рисует оконный менеджер (EWMH):
    //    _NET_FRAME_EXTENTS = left, right, top, bottom.
    long left = 0, right = 0, top = 0, bottom = 0;
    QList<long> ext;
    if (!dock && getLongs(m_dpy, win, m_atomExtents, XA_CARDINAL, 4, ext) && ext.size() == 4) {
        left = ext[0];
        right = ext[1];
        top = ext[2];
        bottom = ext[3];
    }

    const int x = rx - static_cast<int>(left);
    const int y = ry - static_cast<int>(top);
    const int w = attributes.width + static_cast<int>(left + right);
    const int h = attributes.height + static_cast<int>(top + bottom);

    return logicalRect(x, y, w, h);
}
