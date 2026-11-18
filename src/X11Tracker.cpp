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

} // namespace

// ===========================================================================

X11Tracker::X11Tracker(QObject *parent)
    : ActiveWindowTracker(parent)
{
    m_timer.setInterval(30);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &X11Tracker::poll);
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

    m_timer.start();
    return true;
}

void X11Tracker::stop()
{
    m_timer.stop();
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
    {
        ErrorTrap trap(m_dpy);
        result = queryActiveFrame();
        if (trap.failed())
            result = QRect(); // окно успело исчезнуть — считаем, что активного нет
    }

    if (result != m_last) {
        m_last = result;
        emit activeRectChanged(result);
    }
}

QRect X11Tracker::queryActiveFrame() const
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

    // 3) Размер клиентской области и её позиция в координатах корневого окна.
    Window rootRet = 0;
    int gx = 0, gy = 0;
    unsigned int gw = 0, gh = 0, gborder = 0, gdepth = 0;
    if (!XGetGeometry(m_dpy, win, &rootRet, &gx, &gy, &gw, &gh, &gborder, &gdepth))
        return QRect();

    int rx = 0, ry = 0;
    Window child = 0;
    if (!XTranslateCoordinates(m_dpy, win, m_root, 0, 0, &rx, &ry, &child))
        return QRect();

    // 4) Рамка и заголовок, которые рисует оконный менеджер (EWMH):
    //    _NET_FRAME_EXTENTS = left, right, top, bottom.
    long left = 0, right = 0, top = 0, bottom = 0;
    QList<long> ext;
    if (getLongs(m_dpy, win, m_atomExtents, XA_CARDINAL, 4, ext) && ext.size() == 4) {
        left = ext[0];
        right = ext[1];
        top = ext[2];
        bottom = ext[3];
    }

    const int x = rx - static_cast<int>(left);
    const int y = ry - static_cast<int>(top);
    const int w = static_cast<int>(gw) + static_cast<int>(left + right);
    const int h = static_cast<int>(gh) + static_cast<int>(top + bottom);

    // 5) Xlib отдаёт физические пиксели, Qt работает в логических (HiDPI).
    qreal dpr = 1.0;
    if (const QScreen *s = QGuiApplication::primaryScreen())
        dpr = s->devicePixelRatio();

    return QRect(qRound(x / dpr), qRound(y / dpr), qRound(w / dpr), qRound(h / dpr));
}
