#include "SnowWindow.h"
#include "Constants.h"

#include <QColor>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRandomGenerator>
#include <QScreen>
#include <QShowEvent>
#include <QWindow>

#include <cmath>

#ifdef SNOW_HAS_XSHAPE
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#endif

namespace {

constexpr int    kFlakeCount = 180;     // 150–200 по ТЗ
constexpr int    kFrameMs    = 16;      // ~60 FPS
constexpr double kTwoPi      = 6.283185307179586;

bool isWaylandPlatform()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

#if defined(SNOW_HAS_XSHAPE) && QT_CONFIG(xcb)
Display *x11Display()
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11 ? x11->display() : nullptr;
}
#endif

// Случайное число в [a, b)
double rnd(double a, double b)
{
    return a + (b - a) * QRandomGenerator::global()->generateDouble();
}

} // namespace

// ===========================================================================

SnowWindow::SnowWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QString::fromLatin1(SnowIds::kWindowTitle));
    m_debug = qEnvironmentVariableIsSet("SNOW_DEBUG");

    // Флаги окна нужно выставлять первыми: setWindowFlags() пересоздаёт окно.
    configureWindowFlags();

    setAttribute(Qt::WA_TranslucentBackground, true);   // прозрачный фон (нужен композитинг — в KWin он есть)
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);   // не забирать фокус при показе
    setAttribute(Qt::WA_TransparentForMouseEvents, !m_interactive);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(m_interactive);

    m_layoutTimer.setSingleShot(true);
    connect(&m_layoutTimer, &QTimer::timeout, this, [this] {
        applyHole();
        updateSceneLayout();
    });
    adoptScreen(QGuiApplication::primaryScreen());
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &SnowWindow::adoptScreen);

    SnowCrystal::prepareSprites();
    initFlakes();

    m_clock.start();
    m_gestureClock.start();
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(kFrameMs);
    connect(&m_timer, &QTimer::timeout, this, &SnowWindow::tick);
    m_timer.start();
}

// ---------------------------------------------------------------------------
// Флаги окна — самое платформо-зависимое место программы.
// ---------------------------------------------------------------------------
void SnowWindow::configureWindowFlags()
{
    Qt::WindowFlags flags = Qt::FramelessWindowHint        // без рамки и заголовка
                          | Qt::WindowStaysOnTopHint       // поверх всех
                          | Qt::WindowDoesNotAcceptFocus   // фокус клавиатуры не берёт
                          | Qt::NoDropShadowWindowHint;

    const QString platform = QGuiApplication::platformName();

    if (platform.startsWith(QLatin1String("wayland"))) {
        // Wayland: клиент не управляет ни положением, ни слоем окна, а WindowStaysOnTopHint
        // игнорируется. Всё это (keepAbove, «без панели задач», геометрию «на весь экран»)
        // делает KWin-скрипт из KWinTracker — он находит наше окно по app_id и заголовку.
        m_interactive = true;
    } else if (platform == QLatin1String("xcb")) {
        // X11 / XWayland: BypassWindowManagerHint = override-redirect. Окно полностью вне
        // власти WM: нет рамки, нет записи в панели задач, не получает фокус, остаётся
        // сверху, а позицию мы задаём сами. В KWin такие окна рисуются поверх остальных.
        flags |= Qt::Tool | Qt::BypassWindowManagerHint;
#if defined(SNOW_HAS_XSHAPE) && QT_CONFIG(xcb)
        if (Display *display = x11Display()) {
            int major = 0, minor = 0;
            m_interactive = XShapeQueryVersion(display, &major, &minor) && (major > 1 || (major == 1 && minor >= 1));
        }
#endif
    } else {
        // Windows / macOS: адаптации появятся позже.
        flags |= Qt::Tool;
    }

    if (!m_interactive)
        flags |= Qt::WindowTransparentForInput;
    setWindowFlags(flags);
}

// ---------------------------------------------------------------------------
// Экран и «дырка» под активным окном
// ---------------------------------------------------------------------------
void SnowWindow::adoptScreen(QScreen *screen)
{
    if (m_screen)
        m_screen->disconnect(this);

    m_screen = screen;
    if (m_screen) {
        connect(m_screen, &QScreen::geometryChanged, this, [this](const QRect &) { updateScreenGeometry(); });
        connect(m_screen, &QScreen::availableGeometryChanged, this, &SnowWindow::updateSceneLayout);
        if (QWindow *wh = windowHandle())
            wh->setScreen(m_screen);
    }
    updateScreenGeometry();
}

void SnowWindow::updateScreenGeometry()
{
    m_inputRegionSet = false;
    m_screenRect = m_screen ? m_screen->geometry() : QRect(0, 0, 1280, 720);
    setGeometry(m_screenRect);   // под Wayland позицию игнорирует Qt, её выставит KWin-скрипт
    applyHole();
    updateSceneLayout();
    emit overlayGeometryChanged(m_screenRect);
}

void SnowWindow::setActiveRect(const QRect &globalRect)
{
    if (m_activeGlobal == globalRect)
        return;
    m_activeGlobal = globalRect;
    m_layoutTimer.start(0);
}

void SnowWindow::setSurfaces(const QList<SnowSurface> &surfaces)
{
    m_surfaces = surfaces;
    m_layoutTimer.start(0);
}

void SnowWindow::setGlobalCursorPos(const QPoint &globalPosition)
{
    const QPoint local = globalPosition - m_screenRect.topLeft();
    if (m_season == Season::Winter && m_interactive && rect().contains(local) && !m_inputRegion.contains(local))
        m_scene.hoverWindows(local);
    else
        m_scene.leaveWindows();
}

void SnowWindow::pollWindowHover()
{
#if defined(SNOW_HAS_XSHAPE) && QT_CONFIG(xcb)
    QWindow *window = windowHandle();
    if (m_surfaces.isEmpty() || !window || m_scene.dragging() || m_scene.aiming())
        return;
    if (Display *display = x11Display()) {
        Window root = 0, child = 0;
        int rootX = 0, rootY = 0, x = 0, y = 0;
        unsigned int buttons = 0;
        if (XQueryPointer(display, window->winId(), &root, &child, &rootX, &rootY, &x, &y, &buttons) &&
            !(buttons & (Button1Mask | Button2Mask | Button3Mask))) {
            const double scale = window->devicePixelRatio();
            const QPoint local(qRound(x / scale), qRound(y / scale));
            setGlobalCursorPos(local + m_screenRect.topLeft());
        } else {
            m_scene.leaveWindows();
        }
    }
#endif
}

void SnowWindow::updateSceneLayout()
{
    if (size().isEmpty())
        return;
    QRect dock;
    QList<SnowSurface> localSurfaces;
    for (const SnowSurface &surface : m_surfaces) {
        const QRect frame = surface.rect.translated(-m_screenRect.topLeft());
        const QRect local = frame.intersected(rect());
        if (!surface.dock) {
            if (!local.isEmpty())
                localSurfaces.append({surface.id, frame});
            continue;
        }
        if (!local.isEmpty() && local.y() >= height() * .72 &&
            local.width() >= local.height() * 2 && local.width() > dock.width())
            dock = local;
    }
    if (dock.isEmpty() && m_screen) {
        const QRect available = m_screen->availableGeometry().translated(-m_screenRect.topLeft());
        const int y = available.bottom() + 1;
        if (y > height() * .8 && y <= height() - 4)
            dock = QRect(0, y, width(), height() - y);
    }
    // Осенний слой не использует мышь, поэтому ему нужна раскладка и без интерактивного режима.
    m_autumn.setSize(size());
    m_autumn.setGround(dock);
    if (!m_interactive)
        return;
    m_scene.setLayout(size(), dock, m_activeGlobal.translated(-m_screenRect.topLeft()), localSurfaces);
}

void SnowWindow::applyHole()
{
    QRegion newHole;
    if (!m_activeGlobal.isEmpty()) {
        // глобальные координаты -> координаты окна оверлея
        const QRect local = m_activeGlobal.translated(-m_screenRect.topLeft()).intersected(rect());
        if (!local.isEmpty())
            newHole = local;
    }
    if (newHole == m_hole)
        return;

    // При смене полноэкранного окна меняется вся маска, но только снежинки
    // могут менять пиксели в фоне. Игровые объекты обновляют свои области сами.
    const QRegion changed = m_hole.xored(newHole);
    QRegion snow;
    if (m_season == Season::Autumn) {
        snow = m_autumn.overlapping(changed);
    } else {
        for (const Flake &flake : m_flakes) {
            if (changed.intersects(flake.rect))
                snow += flake.rect;
        }
    }
    QRegion dirty = snow & changed;
    if (m_debug) {
        const auto outline = [](const QRegion &hole) {
            const QRect bounds = hole.boundingRect();
            return QRegion(bounds) - bounds.adjusted(3, 3, -3, -3);
        };
        dirty += outline(m_hole) + outline(newHole);
    }
    m_hole = newHole;
    if (!dirty.isEmpty())
        update(dirty);
}

// ---------------------------------------------------------------------------
// События окна
// ---------------------------------------------------------------------------
void SnowWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyHole();
    updateSceneLayout();
}

void SnowWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_inputRegionSet = false;
    applyInputRegion();
}

void SnowWindow::applyInputRegion()
{
    QWindow *window = windowHandle();
    if (!m_interactive || !window)
        return;
    // Осенью интерактивных объектов нет: пустая область ввода = окно прозрачно для мыши.
    const QRegion region = m_season == Season::Winter ? m_scene.inputRegion().intersected(rect()) : QRegion();
    if (m_inputRegionSet && region == m_inputRegion)
        return;
    m_inputRegion = region;
    m_inputRegionSet = true;

    if (isWaylandPlatform()) {
        // Пустой QWindow::mask означает весь экран. Маска вне окна не ловит ввод.
        window->setMask(region.isEmpty() ? QRegion(-10, -10, 1, 1) : region);
        return;
    }
#if defined(SNOW_HAS_XSHAPE) && QT_CONFIG(xcb)
    if (Display *display = x11Display()) {
        std::vector<XRectangle> rectangles;
        rectangles.reserve(region.rectCount());
        const double scale = window->devicePixelRatio();
        for (const QRect &part : region) {
            const int left = qRound(part.x() * scale);
            const int top = qRound(part.y() * scale);
            const int right = qRound((part.x() + part.width()) * scale);
            const int bottom = qRound((part.y() + part.height()) * scale);
            rectangles.push_back({static_cast<short>(left), static_cast<short>(top),
                                  static_cast<unsigned short>(right - left), static_cast<unsigned short>(bottom - top)});
        }
        XShapeCombineRectangles(display, window->winId(), ShapeInput, 0, 0,
                                rectangles.data(), static_cast<int>(rectangles.size()), ShapeSet, Unsorted);
        XFlush(display);
    }
#endif
}

void SnowWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        if (m_scene.aiming()) {
            m_scene.cancelAim();
            unsetCursor();
            applyInputRegion();
            event->accept();
            return;
        }
        if (m_scene.breakSnowman(event->position())) {
            applyInputRegion();
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && !m_scene.dragging()) {
        if (m_scene.fireAt(event->position())) {
            unsetCursor();
            applyInputRegion();
            event->accept();
            return;
        }
        m_pressTimestamp = static_cast<quint32>(event->timestamp());
        m_gestureClock.restart();
        if (m_scene.press(event->position(), 0)) {
            event->accept(); // Qt сохраняет захват до отпускания кнопки даже вне области ввода.
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void SnowWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!event->buttons().testFlag(Qt::LeftButton))
        m_scene.cancelDrag();
    if (m_scene.dragging()) {
        m_scene.move(event->position(), pointerTime(event));
        event->accept();
    } else if (event->buttons() == Qt::NoButton) {
        m_scene.hover(event->position());
        event->accept();
    } else {
        QWidget::mouseMoveEvent(event);
    }
}

void SnowWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_scene.release(event->position(), pointerTime(event))) {
        if (m_scene.aiming())
            setCursor(Qt::CrossCursor);
        applyInputRegion();
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}

double SnowWindow::pointerTime(const QMouseEvent *event) const
{
    // Разность без знака сохраняет интервал и при переполнении счётчика X11/Wayland.
    if (m_pressTimestamp != 0)
        return (static_cast<quint32>(event->timestamp()) - m_pressTimestamp) / 1000.0;
    return m_gestureClock.nsecsElapsed() / 1e9;
}

bool SnowWindow::event(QEvent *event)
{
    if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide)
        m_scene.cancelDrag();
    if (event->type() == QEvent::Hide && m_scene.aiming()) {
        m_scene.cancelAim();
        unsetCursor();
    }
    if (event->type() == QEvent::Leave)
        m_scene.leave();
    return QWidget::event(event);
}

void SnowWindow::startPlow()
{
    if (m_interactive && m_season == Season::Winter)
        m_scene.startPlow();
}

// ---------------------------------------------------------------------------
// Сезоны
// ---------------------------------------------------------------------------
void SnowWindow::setSeason(Season season)
{
    if (m_season == season)
        return;
    m_season = season;

    // Зимние жесты (перетаскивание кома, прицел) не должны «зависнуть» в другом сезоне.
    m_scene.cancelDrag();
    if (m_scene.aiming()) {
        m_scene.cancelAim();
        unsetCursor();
    }
    m_scene.leave();
    m_scene.leaveWindows();

    m_inputRegionSet = false;
    applyInputRegion();
    update();   // полная перерисовка: старый сезон должен исчезнуть, новый — появиться
}

void SnowWindow::setAutumnOptions(const AutumnOptions &options)
{
    if ((!options.rain && !options.leaves) || options == m_autumn.options())
        return;
    m_autumn.setOptions(options);
    if (m_season == Season::Autumn)
        update();
}

// ---------------------------------------------------------------------------
// Снежинки
// ---------------------------------------------------------------------------
QRect SnowWindow::flakeRect(const Flake &f)
{
    return SnowCrystal::bounds(QPointF(f.x, f.y), f.radius);
}

void SnowWindow::initFlakes()
{
    m_flakes.assign(kFlakeCount, Flake());
    for (Flake &f : m_flakes) {
        respawn(f, true);
        f.x = f.baseX + std::sin(f.phase) * f.swayAmp;
        f.rect = flakeRect(f);
    }
}

void SnowWindow::respawn(Flake &f, bool initial)
{
    const double w = qMax(1, width());
    const double h = qMax(1, height());

    const double u = rnd(0.0, 1.0);
    f.radius     = 1.0 + 8.1 * u * u * u;                          // крупные символы встречаются реже
    f.speed      = 19.0 + 17.0 * std::sqrt(f.radius) + rnd(0.0, 22.0);
    f.alpha      = qBound(0.28, 0.32 + 0.09 * f.radius + rnd(-0.10, 0.12), 0.96);
    f.swayAmp    = rnd(6.0, 28.0);                                 // амплитуда покачивания
    f.swayFreq   = rnd(0.6, 1.8);                                  // частота покачивания
    f.phase      = rnd(0.0, kTwoPi);
    f.windFactor = 0.4 + f.radius * 0.18;
    f.glyph      = static_cast<SnowCrystal::Glyph>(QRandomGenerator::global()->bounded(SnowCrystal::glyphCount));
    f.baseX      = rnd(0.0, w);                                    // случайная точка по оси X
    f.y          = initial ? rnd(0.0, h)                           // старт: по всему экрану
                           : -rnd(f.radius * 2.0, 60.0);           // респаун: чуть выше верхней границы
}

void SnowWindow::tick()
{
    const double dt = qMin(0.05, m_clock.nsecsElapsed() / 1e9);   // не даём «прыгнуть» после лагов
    m_clock.restart();
    m_time += dt;

    const double w = width();
    const double h = height();
    if (w < 1 || h < 1)
        return;

    if (m_season == Season::Autumn) {
        tickAutumn(dt);
        return;
    }

    // Общий «ветер»: медленно меняющийся снос в px/с (положительный — вправо).
    const double wind = 14.0 * std::sin(m_time * 0.25) + 7.0 * std::sin(m_time * 0.73 + 1.3);
    const double margin = 30.0;

    QRegion dirty;   // обновляем только те места, где снежинки были и где стали
    const QRect hole = m_hole.boundingRect();
    const auto mark = [&](const QRect &damage) {
        if (!hole.contains(damage))
            dirty += damage;
    };
    for (Flake &f : m_flakes) {
        const QRect oldRect = f.rect;

        const double lowerEdge = f.radius * .75;
        const QPointF previous(f.x, f.y + lowerEdge);
        f.y     += f.speed * dt;
        f.baseX += wind * f.windFactor * dt;
        f.phase += f.swayFreq * dt;

        // По горизонтали экран «зациклен» (ветер уносит за край — вернётся с другого).
        const bool wrapped = f.baseX < -margin || f.baseX > w + margin;
        if (wrapped) {
            const double span = w + 2 * margin;
            f.baseX = std::fmod(f.baseX + margin, span);
            if (f.baseX < 0)
                f.baseX += span;
            f.baseX -= margin;
        }

        f.x = f.baseX + std::sin(f.phase) * f.swayAmp;   // покачивание по синусоиде
        const bool landed = m_interactive && !wrapped &&
                            m_scene.settleSnow(previous, QPointF(f.x, f.y + lowerEdge), f.radius * .32);
        const bool respawned = landed || f.y - f.radius > h;
        if (respawned) {
            respawn(f, false);
            f.x = f.baseX + std::sin(f.phase) * f.swayAmp;
        }
        f.rect = flakeRect(f);
        if (!respawned && oldRect == f.rect)
            continue;
        if (oldRect.intersects(f.rect)) {
            mark(oldRect.united(f.rect));
        } else {
            mark(oldRect);
            mark(f.rect);
        }
    }
    if (m_interactive) {
        if (!isWaylandPlatform()) {
            m_hoverRefresh += dt;
            if (m_hoverRefresh >= .042) {
                pollWindowHover();
                m_hoverRefresh = 0;
            }
        }
        dirty += m_scene.advance(dt);
        m_inputRefresh += dt;
        if (m_inputRefresh >= .032) {
            applyInputRegion();
            m_inputRefresh = 0;
        }
    }
    if (!dirty.isEmpty())
        update(dirty);
}

void SnowWindow::tickAutumn(double dt)
{
    const QRegion dirty = m_autumn.advance(dt, m_hole.boundingRect());
    if (!dirty.isEmpty())
        update(dirty);
}

// ---------------------------------------------------------------------------
// Отрисовка
// ---------------------------------------------------------------------------
void SnowWindow::paintEvent(QPaintEvent *event)
{
    QPainter p(this);

    // Снег разрешено рисовать только вне активного окна.
    // (перед paintEvent Qt сам очищает перерисовываемую область до прозрачной,
    //  поэтому в «дырке» остаётся прозрачность и активное окно видно целиком)
    const QRegion clip = event->region() - m_hole;

    if (m_debug && !m_hole.isEmpty()) {
        p.setPen(QPen(QColor(255, 0, 0, 220), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(m_hole.boundingRect().adjusted(1, 1, -1, -1));
    }

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    if (!clip.isEmpty()) {
        p.save();
        p.setClipRegion(clip);
        if (m_season == Season::Autumn) {
            m_autumn.paint(p, clip);
        } else {
            const QRect bounds = clip.boundingRect();
            for (const Flake &f : m_flakes) {
                if (!f.rect.intersects(bounds) || !clip.intersects(f.rect))
                    continue;

                SnowCrystal::paint(p, QPointF(f.x, f.y), f.radius, f.alpha, f.glyph);
            }
        }
        p.restore();
    }

    if (m_interactive && m_season == Season::Winter)
        m_scene.paint(p, event->region());
}
