#include "SnowWindow.h"
#include "Constants.h"
#include "SnowProps.h"

#include <QColor>
#include <QDataStream>
#include <QIODevice>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRandomGenerator>
#include <QScreen>
#include <QShowEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>

#ifdef SNOW_HAS_XLIB
#include <X11/Xlib.h>
#endif
#ifdef SNOW_HAS_XSHAPE
#include <X11/extensions/shape.h>
#endif

namespace {

constexpr int    kFrameMs    = 16;      // ~60 FPS
constexpr int    kResumeMs   = 180;
constexpr double kTwoPi      = 6.283185307179586;

QRect popupGarland(const QRect &frame)
{
    return frame.isEmpty() ? QRect() : QRect(frame.topLeft(), QSize(frame.width(), 24));
}

bool isWaylandPlatform()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

#if defined(SNOW_HAS_XLIB) && QT_CONFIG(xcb)
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

QRegion holeOutline(const QRegion &hole)
{
    const QRect bounds = hole.boundingRect();
    return QRegion(bounds) - bounds.adjusted(3, 3, -3, -3);
}

QRegion groupX11Damage(const QRegion &damage, const QRect &viewport)
{
    constexpr int bandHeight = 24;
    constexpr int maxGap = 128;
    const qint64 extraBudget = qMin<qint64>(200000, qint64(viewport.width()) * viewport.height() / 16);
    qint64 area = 0;
    std::vector<QRect> slices;
    slices.reserve(damage.rectCount());
    // Одинаковая высота внутри полосы не даёт QRegion снова дробить прямоугольники.
    for (const QRect &part : damage) {
        area += qint64(part.width()) * part.height();
        for (int y = part.top() / bandHeight * bandHeight; y <= part.bottom(); y += bandHeight) {
            const int height = qMin(bandHeight, viewport.height() - y);
            slices.emplace_back(part.x(), y, part.width(), height);
        }
    }
    std::sort(slices.begin(), slices.end(), [](const QRect &a, const QRect &b) {
        return a.y() == b.y() ? a.x() < b.x() : a.y() < b.y();
    });

    QRegion grouped;
    qint64 groupedArea = 0;
    QRect run;
    const auto append = [&](const QRect &part) {
        grouped += part;
        groupedArea += qint64(part.width()) * part.height();
    };
    for (const QRect &slice : slices) {
        if (run.isValid() && run.y() == slice.y() && slice.left() - run.right() - 1 <= maxGap) {
            run.setRight(qMax(run.right(), slice.right()));
        } else {
            if (run.isValid())
                append(run);
            run = slice;
        }
    }
    if (run.isValid())
        append(run);
    if (groupedArea > area + extraBudget || grouped.rectCount() >= damage.rectCount())
        return damage;
    return grouped;
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
    connect(&m_layoutTimer, &QTimer::timeout, this, &SnowWindow::refreshLayout);
    m_resumeTimer.setSingleShot(true);
    m_resumeTimer.setTimerType(Qt::PreciseTimer);
    m_resumeTimer.setInterval(kResumeMs);
    connect(&m_resumeTimer, &QTimer::timeout, this, &SnowWindow::resumeWeather);
    m_popupTimer.setSingleShot(true);
    m_popupTimer.setInterval(90);
    connect(&m_popupTimer, &QTimer::timeout, this, [this] {
        refreshGarlandOverlay(popupGarland(m_popupFrame));
    });
#ifdef SNOW_HAS_LAYER_SHELL
    connect(&m_garlandLayer, &QProcess::started, this, &SnowWindow::sendLayerGarlands);
#endif
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

SnowWindow::~SnowWindow()
{
#ifdef SNOW_HAS_LAYER_SHELL
    if (m_garlandLayer.state() != QProcess::NotRunning) {
        m_garlandLayer.closeWriteChannel();
        m_garlandLayer.waitForFinished(300);
    }
#endif
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
    m_x11 = platform == QLatin1String("xcb");

    if (platform.startsWith(QLatin1String("wayland"))) {
        // Wayland: клиент не управляет ни положением, ни слоем окна, а WindowStaysOnTopHint
        // игнорируется. Всё это (keepAbove, «без панели задач», геометрию «на весь экран»)
        // делает KWin-скрипт из KWinTracker — он находит оверлей по заголовку.
        m_interactive = true;
    } else if (m_x11) {
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
        connect(m_screen, &QScreen::availableGeometryChanged, this, &SnowWindow::refreshLayout);
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
    refreshLayout();
    emit overlayGeometryChanged(m_screenRect);
}

void SnowWindow::setActiveRect(const QRect &globalRect, bool fullscreen)
{
    if (m_activeGlobal == globalRect && m_activeFullscreen == fullscreen)
        return;
    m_activeGlobal = globalRect;
    m_activeFullscreen = fullscreen;
    if (m_resumeTimer.isActive())
        m_resumeTimer.start();
    m_layoutTimer.start(0);
}

void SnowWindow::setSurfaces(const QList<SnowSurface> &surfaces)
{
    m_receivedSurfaces = true;
    m_surfaces = surfaces;
    m_layoutTimer.start(0);
}

void SnowWindow::setGlobalCursorPos(const QPoint &globalPosition)
{
    const QPoint local = globalPosition - m_screenRect.topLeft();
    if (m_season != Season::Winter) {
        if (!m_suspended && rect().contains(local))
            m_weather.movePointer(local, false);
        else
            m_weather.leavePointer();
        return;
    }
    if (!m_suspended && rect().contains(local) && !m_inputRegion.contains(local))
        m_scene.hoverWindows(local);
    else
        m_scene.leaveWindows();
}

void SnowWindow::pollWindowHover()
{
#if defined(SNOW_HAS_XLIB) && QT_CONFIG(xcb)
    QWindow *window = windowHandle();
    if (!window || m_scene.dragging() || m_scene.aiming())
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
            m_weather.leavePointer();
        }
    }
#endif
}

QRect SnowWindow::winterWorkArea() const
{
    QRect area = m_screen ? m_screen->availableGeometry().intersected(m_screenRect) : m_screenRect;
    if (area.isEmpty())
        area = m_screenRect;
    // Некоторые X11-конфигурации возвращают весь монитор даже при видимой панели.
    for (const SnowSurface &surface : m_surfaces) {
        if (!surface.dock)
            continue;
        const QRect panel = surface.rect & m_screenRect;
        if (panel.isEmpty())
            continue;
        if (panel.width() >= m_screenRect.width() * .8 && panel.height() < m_screenRect.height() / 4) {
            if (panel.bottom() == m_screenRect.bottom())
                area.setBottom(qMin(area.bottom(), panel.top() - 1));
            if (panel.top() == m_screenRect.top())
                area.setTop(qMax(area.top(), panel.bottom() + 1));
        } else if (panel.height() >= m_screenRect.height() * .8 && panel.width() < m_screenRect.width() / 4) {
            if (panel.right() == m_screenRect.right())
                area.setRight(qMin(area.right(), panel.left() - 1));
            if (panel.left() == m_screenRect.left())
                area.setLeft(qMax(area.left(), panel.right() + 1));
        }
    }
    return area;
}

const SnowSurface *SnowWindow::fullscreenSurface() const
{
    const QRect work = m_season == Season::Winter ? winterWorkArea().adjusted(1, 1, -1, -1) : QRect();
    for (auto it = m_surfaces.crbegin(); it != m_surfaces.crend(); ++it) {
        if (it->dock || it->panelPopup)
            continue;
        if ((it->fullscreen && it->rect.contains(m_screenRect)) ||
            (work.isValid() && it->rect.contains(work)) ||
            (m_season == Season::Winter && it->maximized && it->rect.contains(m_screenRect.center())))
            return &*it;
    }
    return nullptr;
}

bool SnowWindow::coveredByFullscreen() const
{
    const SnowSurface *fullscreen = fullscreenSurface();
    if (!fullscreen) {
        const QRect work = m_season == Season::Winter ? winterWorkArea().adjusted(1, 1, -1, -1) : QRect();
        return (m_activeFullscreen && m_activeGlobal.contains(m_screenRect)) ||
               (work.isValid() && m_activeGlobal.contains(work));
    }

    QRegion visible(m_screenRect & fullscreen->rect);
    for (auto above = m_surfaces.crbegin(); &*above != fullscreen; ++above) {
        visible -= above->rect;
        if (visible.isEmpty())
            return false;
    }
    return true;
}

void SnowWindow::refreshLayout()
{
    if (size().isEmpty())
        return;
    updatePanelPopup();
    const bool fullscreen = coveredByFullscreen();
    if (m_suspended) {
        if (fullscreen)
            m_resumeTimer.stop();
        else if (!m_resumeTimer.isActive())
            m_resumeTimer.start();
        if (m_season == Season::Winter)
            updateSceneLayout();
        updatePausedGarlands();
        return;
    }
    if (!fullscreen) {
        updateDamage(updateHole());
        updateSceneLayout();
        return;
    }

    m_suspended = true;
    emit seasonalCursorTrackingChanged(false);
    m_weather.setTool(SeasonTool::Passive);
    m_timer.stop();
    m_scene.cancelDrag();
    m_scene.cancelAim();
    m_scene.leave();
    m_scene.leaveWindows();
    unsetCursor();
    if (m_season == Season::Winter)
        updateSceneLayout();
    updatePausedGarlands();
    updateHole();
    update();
    applyInputRegion();
}

void SnowWindow::updatePausedGarlands()
{
    SnowScene::Garlands next;
    if (m_suspended && m_season == Season::Winter) {
        if (const SnowSurface *cover = fullscreenSurface())
            next = m_scene.garlandsAbove(cover->id);
        if (m_scene.garlandsPending() && !m_timer.isActive()) {
            m_clock.restart();
            m_timer.start();
        } else if (!m_scene.garlandsPending()) {
            m_timer.stop();
        }
    } else if (m_suspended) {
        m_timer.stop();
    }

    QRegion damage;
    if (next != m_pausedGarlands) {
        for (const auto &garland : m_pausedGarlands)
            damage += garland.second;
        for (const auto &garland : next)
            damage += garland.second;
        m_pausedGarlands = std::move(next);
        refreshGarlandOverlay(damage);
    } else if (m_suspended && m_x11 && m_pausedGarlands.isEmpty() && m_popupFrame.isEmpty()) {
        hide();
    }
}

void SnowWindow::updatePanelPopup()
{
    QRect next;
    if (m_season == Season::Winter && m_showGarlands) {
        for (auto it = m_surfaces.crbegin(); it != m_surfaces.crend(); ++it) {
            if (it->panelPopup && it->rect.width() >= 56 && it->rect.height() >= 36 &&
                it->rect.top() >= m_screenRect.top() && it->rect.intersects(m_screenRect)) {
                next = it->rect.translated(-m_screenRect.topLeft());
                break;
            }
        }
    }
    if (next == m_popupFrame)
        return;
    const QRect previous = popupGarland(m_popupFrame);
    m_popupFrame = next;
    m_popupTimer.stop();
    if (!next.isEmpty())
        m_popupTimer.start();
    refreshGarlandOverlay(previous);
}

void SnowWindow::refreshGarlandOverlay(const QRegion &damage)
{
    const bool decorations = !m_pausedGarlands.isEmpty() || (!m_popupFrame.isEmpty() && !m_popupTimer.isActive());
#ifdef SNOW_HAS_LAYER_SHELL
    if (useGarlandLayer()) {
        if (m_garlandLayer.state() == QProcess::Running)
            sendLayerGarlands();
        else if (m_garlandLayer.state() == QProcess::NotRunning && decorations) {
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert(QStringLiteral("QT_WAYLAND_SHELL_INTEGRATION"), QStringLiteral("layer-shell"));
            m_garlandLayer.setProcessEnvironment(environment);
            m_garlandLayer.setProcessChannelMode(QProcess::ForwardedChannels);
            m_garlandLayer.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--panel-garland")});
        }
        return;
    }
#endif
    if (m_suspended && m_x11) {
        if (!decorations) {
            hide();
            return;
        }
        if (!isVisible())
            show();
    }
    if (m_x11 && !m_popupFrame.isEmpty() && !m_popupTimer.isActive())
        raise();
    updateDamage(damage);
}

bool SnowWindow::useGarlandLayer() const
{
#ifdef SNOW_HAS_LAYER_SHELL
    return isWaylandPlatform();
#else
    return false;
#endif
}

#ifdef SNOW_HAS_LAYER_SHELL
void SnowWindow::sendLayerGarlands()
{
    auto garlands = m_pausedGarlands;
    if (!m_popupFrame.isEmpty() && !m_popupTimer.isActive())
        garlands.append({m_popupFrame, QRegion(popupGarland(m_popupFrame))});
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << QGuiApplication::screens().indexOf(m_screen) << garlands;
    m_garlandLayer.write(payload.toBase64() + '\n');
}
#endif

void SnowWindow::resumeWeather()
{
    if (!m_suspended || coveredByFullscreen() || size().isEmpty())
        return;
    m_suspended = false;
    updatePausedGarlands();
    emit seasonalCursorTrackingChanged(m_season != Season::Winter);
    if (m_season == Season::Winter)
        initFlakes();
    else
        m_weather.restart();
    QRegion damage = updateHole();
    updateSceneLayout();
    m_scene.clearShedSnow();
    if (m_x11) {
        show(); // showEvent восстанавливает регион ввода перед первым кадром.
    } else {
        if (m_season == Season::Winter)
            damage += m_scene.visualRegion();
        if (m_debug)
            damage += holeOutline(m_hole);
        updateDamage(damage);
        applyInputRegion();
    }
    m_clock.restart();
    m_timer.start();
}

void SnowWindow::updateSceneLayout()
{
    QRect dock;
    const QRect winterArea = m_season == Season::Winter ? winterWorkArea().adjusted(1, 1, -1, -1) : QRect();
    QList<SnowSurface> localSurfaces;
    QList<SnowSurface> weatherWindows;
    for (const SnowSurface &surface : m_surfaces) {
        const QRect frame = surface.rect.translated(-m_screenRect.topLeft());
        const QRect local = frame.intersected(rect());
        if (!surface.dock) {
            if (m_season == Season::Winter && !local.isEmpty())
                localSurfaces.append({surface.id, frame, false, surface.fullscreen ||
                                      (winterArea.isValid() && surface.rect.contains(winterArea)), surface.maximized, surface.panelPopup});
            else if (m_season != Season::Winter && !local.isEmpty())
                weatherWindows.append({surface.id, frame, false, surface.fullscreen, surface.maximized, surface.panelPopup});
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
    if (m_season == Season::Winter) {
        m_scene.setLayout(size(), dock, m_receivedSurfaces ? QRect() : m_activeGlobal.translated(-m_screenRect.topLeft()),
                          localSurfaces, m_activeFullscreen);
    } else {
        const QRect active = m_activeGlobal.translated(-m_screenRect.topLeft());
        if (!m_receivedSurfaces && !m_activeFullscreen && active.intersects(rect()))
            weatherWindows.append({QStringLiteral("active-fallback"), active});
        m_weather.setGround(dock);
        m_weather.setSize(size());
        m_weather.setWindows(weatherWindows);
        applyInputRegion();
    }
}

QRegion SnowWindow::updateHole()
{
    QRegion newHole;
    if (m_suspended) {
        newHole = rect();
    } else if (!m_activeGlobal.isEmpty()) {
        // глобальные координаты -> координаты окна оверлея
        const QRect local = m_activeGlobal.translated(-m_screenRect.topLeft()).intersected(rect());
        if (!local.isEmpty())
            newHole = local;
    }
    if (newHole == m_hole)
        return {};

    // При смене полноэкранного окна меняется вся маска, но только частицы погоды
    // могут менять пиксели в фоне. Игровые объекты обновляют свои области сами.
    const QRegion changed = m_hole.xored(newHole);
    QRegion weather;
    if (m_season == Season::Winter) {
        for (const Flake &flake : m_flakes) {
            if (changed.intersects(flake.rect))
                weather += flake.rect;
        }
    } else {
        weather = m_weather.overlapping(changed);
    }
    QRegion dirty = weather & changed;
    if (m_debug)
        dirty += holeOutline(m_hole) + holeOutline(newHole);
    m_hole = newHole;
    return dirty;
}

void SnowWindow::updateDamage(const QRegion &damage)
{
    if (damage.isEmpty())
        return;
    if (m_x11 && damage.rectCount() > 32) {
        const QRect viewport = rect();
        update(groupX11Damage(damage & viewport, viewport));
    } else {
        update(damage);
    }
}

// ---------------------------------------------------------------------------
// События окна
// ---------------------------------------------------------------------------
void SnowWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    refreshLayout();
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
    QRegion region;
    if (!m_suspended) {
        if (m_season == Season::Winter) {
            region = m_scene.inputRegion() & rect();
        } else {
            region = m_weather.inputRegion();
            if (m_weather.tool() != SeasonTool::Acorn)
                region = (region - m_hole) + m_weather.pileInputRegion();
        }
    }
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
    if (m_suspended)
        return QWidget::mousePressEvent(event);
    if (m_season != Season::Winter) {
        if (event->button() == Qt::RightButton) {
            if (m_weather.breakPile(event->position())) {
                applyInputRegion();
                event->accept();
                return;
            }
            if (m_weather.tool() != SeasonTool::Passive) {
                setSeasonTool(SeasonTool::Passive);
                event->accept();
                return;
            }
        }
        if (event->button() == Qt::LeftButton && m_weather.press(event->position())) {
            if (m_weather.tool() == SeasonTool::Passive)
                unsetCursor();
            applyInputRegion();
            event->accept();
            return;
        }
        return QWidget::mousePressEvent(event);
    }
    if (event->button() == Qt::RightButton) {
        if (m_scene.aiming()) {
            m_scene.cancelAim();
            unsetCursor();
            applyInputRegion();
            event->accept();
            return;
        }
        if (m_scene.breakSnow(event->position())) {
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
    if (m_suspended)
        return QWidget::mouseMoveEvent(event);
    if (m_season != Season::Winter) {
        m_weather.movePointer(event->position(), event->buttons().testFlag(Qt::LeftButton));
        return;
    }
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
    if (!m_suspended && m_season == Season::Winter && event->button() == Qt::LeftButton &&
        m_scene.release(event->position(), pointerTime(event))) {
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
    if (m_season == Season::Winter && !m_suspended)
        m_scene.startPlow();
}

void SnowWindow::setSeason(Season season)
{
    if (m_season == season)
        return;

    m_scene.cancelDrag();
    m_scene.cancelAim();
    m_scene.leave();
    m_scene.leaveWindows();
    unsetCursor();

    m_season = season;
    m_weather.setSeason(season);
    emit seasonalCursorTrackingChanged(!m_suspended && season != Season::Winter);
    if (season == Season::Winter)
        initFlakes();
    refreshLayout();
    if (season == Season::Winter)
        m_scene.clearShedSnow();
    applyInputRegion();
    update();
}

void SnowWindow::setAutumnOptions(const AutumnOptions &options)
{
    if (!options.rain && !options.leaves)
        return;
    const AutumnOptions previous = m_weather.options();
    if (previous.rain == options.rain && previous.leaves == options.leaves)
        return;

    m_weather.setOptions(options);
}

void SnowWindow::setSpringRain(bool enabled)
{
    m_weather.setSpringRain(enabled);
}

void SnowWindow::setBubbles(bool enabled)
{
    m_weather.setBubbles(enabled);
    applyInputRegion();
}

void SnowWindow::setSeasonTool(SeasonTool tool)
{
    if (m_season == Season::Winter || m_suspended || !m_interactive)
        return;
    m_weather.setTool(tool);
    if (tool == SeasonTool::Passive)
        unsetCursor();
    else
        setCursor(Qt::CrossCursor);
    applyInputRegion();
}

void SnowWindow::setSeasonFan(bool enabled)
{
    m_weather.setFan(enabled);
}

void SnowWindow::setFireflyJar(bool enabled)
{
    m_weather.setJar(enabled);
}

void SnowWindow::startLeafBlower()
{
    if (!m_suspended)
        m_weather.startBlower();
}

bool SnowWindow::launchPaperBoat()
{
    return !m_suspended && m_weather.launchBoat();
}

// ---------------------------------------------------------------------------
// Снежинки
// ---------------------------------------------------------------------------
QRect SnowWindow::flakeRect(const Flake &f)
{
    return SnowCrystal::bounds(QPointF(f.x, f.y), f.radius);
}

void SnowWindow::setGarlandsEnabled(bool enabled)
{
    m_showGarlands = enabled;
    updatePanelPopup();
    const QRegion damage = m_scene.setGarlandsEnabled(enabled);
    if (m_suspended)
        updatePausedGarlands();
    else if (m_season == Season::Winter)
        updateDamage(damage);
}

void SnowWindow::setSnowIntensity(int percent)
{
    m_scene.setSecondPlowEnabled(percent > 300);
    const int count = qRound(kNormalFlakeCount * qBound(0, percent, SnowSettings::maxIntensity) / 100.0);
    const int oldCount = static_cast<int>(m_flakes.size());
    if (count == oldCount)
        return;

    QRegion damage;
    if (!m_suspended && m_season == Season::Winter) {
        for (int i = count; i < oldCount; ++i) {
            const QRect &bounds = m_flakes[i].rect;
            if (rect().intersects(bounds))
                damage += bounds;
        }
    }
    m_flakeCount = count;
    m_flakes.resize(count);
    spawnFlakesFromTop(oldCount);
    updateDamage(damage - m_hole);
}

void SnowWindow::setSnowSpeed(int percent)
{
    m_snowSpeed = qBound(SnowSettings::minSpeed, percent, SnowSettings::maxSpeed) / 100.0;
    m_scene.setPlowCadence(m_snowSpeed);
}

void SnowWindow::setSnowWind(int percent)
{
    m_snowWind = qBound(-SnowSettings::maxWind, percent, SnowSettings::maxWind) * Precipitation::windScale;
}

void SnowWindow::setPrecipitation(const Precipitation::Settings &settings)
{
    setSnowIntensity(settings.intensity);
    setSnowSpeed(settings.speed);
    setSnowWind(settings.wind);
    m_weather.setPrecipitation(settings);
}

void SnowWindow::initFlakes()
{
    m_flakes.resize(m_flakeCount);
    spawnFlakesFromTop(0);
}

void SnowWindow::spawnFlakesFromTop(int first)
{
    // Разносим первые снежинки по времени, чтобы они не выходили из-за края одной полосой.
    for (int i = first; i < m_flakeCount; ++i) {
        Flake &f = m_flakes[i];
        respawn(f);
        Precipitation::stage(f, qMax(8.0, f.radius), height(), i - first, m_flakeCount - first);
        f.rect = flakeRect(f);
    }
}

void SnowWindow::respawn(Flake &f)
{
    const double w = qMax(1, width());

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
    f.y          = -rnd(f.radius * 2.0, 60.0);
    f.x          = f.baseX + std::sin(f.phase) * f.swayAmp;
}

void SnowWindow::tick()
{
    const double dt = qMin(0.05, m_clock.nsecsElapsed() / 1e9);   // не даём «прыгнуть» после лагов
    m_clock.restart();
    if (m_suspended) {
        m_scene.advanceGarlands(dt);
        updatePausedGarlands();
        return;
    }
    if (m_season != Season::Winter) {
        if (m_x11) {
            m_hoverRefresh += dt;
            if (m_hoverRefresh >= .04) {
                pollWindowHover();
                m_hoverRefresh = 0;
            }
        }
        const QRegion damage = m_weather.advance(dt, m_hole.boundingRect());
        if (m_weather.densePrecipitation())
            update(damage);
        else
            updateDamage(damage);
        if (m_interactive) {
            m_inputRefresh += dt;
            if (m_inputRefresh >= .032) {
                applyInputRegion();
                m_inputRefresh = 0;
            }
        }
        return;
    }
    m_time += dt;

    const double w = width();
    const double h = height();
    if (w < 1 || h < 1)
        return;

    // Общий «ветер»: медленно меняющийся снос в px/с (положительный — вправо).
    const double wind = Precipitation::windAt(m_snowWind, m_time);
    const double margin = 30.0;

    const bool dense = m_flakes.size() >= Precipitation::denseCount;
    Precipitation::DamageTiles tiles(size(), dense);
    QRegion dirty;   // обновляем только те места, где снежинки были и где стали
    const QRect hole = m_hole.boundingRect();
    const QRect viewport = rect();
    const auto mark = [&](const QRect &damage) {
        if (!viewport.intersects(damage) || hole.contains(damage))
            return;
        if (dense)
            tiles.mark(damage.intersected(viewport));
        else
            dirty += damage;
    };
    for (Flake &f : m_flakes) {
        const QRect oldRect = f.rect;

        const double lowerEdge = f.radius * .75;
        const QPointF previous(f.x, f.y + lowerEdge);
        const bool wrapped = Precipitation::fall(f, dt, m_snowSpeed, wind, w, margin);
        const bool landed = !wrapped && f.y + lowerEdge >= 0 &&
                            m_scene.settleSnow(previous, QPointF(f.x, f.y + lowerEdge), f.radius * .32, true, true);
        const bool respawned = landed || f.y - f.radius > h;
        if (respawned)
            respawn(f);
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
    if (m_x11) {
        m_hoverRefresh += dt;
        if (m_hoverRefresh >= .042) {
            pollWindowHover();
            m_hoverRefresh = 0;
        }
    }
    dirty += m_scene.advance(dt, wind);
    if (m_interactive) {
        m_inputRefresh += dt;
        if (m_inputRefresh >= .032) {
            applyInputRegion();
            m_inputRefresh = 0;
        }
    }
    if (dense)
        update(dirty + tiles.region());
    else
        updateDamage(dirty);
}

// ---------------------------------------------------------------------------
// Отрисовка
// ---------------------------------------------------------------------------
void SnowWindow::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_suspended) {
        if (!useGarlandLayer()) {
            for (const auto &garland : m_pausedGarlands) {
                const QRegion exposed = garland.second & event->region();
                if (!exposed.isEmpty())
                    SnowProps::drawGarland(p, garland.first, exposed);
            }
        }
        if (!useGarlandLayer() && !m_popupTimer.isActive() && event->region().intersects(popupGarland(m_popupFrame)))
            SnowProps::drawGarland(p, m_popupFrame, event->region() & popupGarland(m_popupFrame));
        return;
    }

    // Снег разрешено рисовать только вне активного окна.
    // (перед paintEvent Qt сам очищает перерисовываемую область до прозрачной,
    //  поэтому в «дырке» остаётся прозрачность и активное окно видно целиком)
    const QRegion clip = event->region() - m_hole;

    if (m_debug && !m_hole.isEmpty()) {
        p.setPen(QPen(QColor(255, 0, 0, 220), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(m_hole.boundingRect().adjusted(1, 1, -1, -1));
    }

    p.setPen(Qt::NoPen);

    if (!clip.isEmpty()) {
        p.save();
        p.setClipRegion(QRegion(rect()) - m_hole);
        if (m_season == Season::Winter) {
            const bool dense = m_flakes.size() >= Precipitation::denseCount;
            const QRect bounds = clip.boundingRect();
            const QRect hole = m_hole.boundingRect();
            for (const Flake &f : m_flakes) {
                if (!f.rect.intersects(bounds) || hole.contains(f.rect) || (!dense && !clip.intersects(f.rect)))
                    continue;
                SnowCrystal::paint(p, QPointF(f.x, f.y), f.radius, f.alpha, f.glyph);
            }
        } else {
            m_weather.paint(p, clip);
        }
        p.restore();
    }

    if (m_season == Season::Winter)
        m_scene.paint(p, event->region());
    else
        m_weather.paintLeafOverlays(p, event->region());
    if (!useGarlandLayer() && !m_popupTimer.isActive() && event->region().intersects(popupGarland(m_popupFrame)))
        SnowProps::drawGarland(p, m_popupFrame, event->region() & popupGarland(m_popupFrame));
}
