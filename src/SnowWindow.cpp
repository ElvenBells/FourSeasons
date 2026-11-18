#include "SnowWindow.h"
#include "Constants.h"

#include <QColor>
#include <QGuiApplication>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRandomGenerator>
#include <QScreen>
#include <QShowEvent>
#include <QWindow>

#include <cmath>

namespace {

constexpr int    kFlakeCount = 180;     // 150–200 по ТЗ
constexpr int    kFrameMs    = 16;      // ~60 FPS
constexpr double kTwoPi      = 6.283185307179586;

bool isWaylandPlatform()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

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
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);

    adoptScreen(QGuiApplication::primaryScreen());
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &SnowWindow::adoptScreen);

    initFlakes();

    m_clock.start();
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
                          | Qt::WindowTransparentForInput  // клики и мышь проходят сквозь окно
                          | Qt::WindowDoesNotAcceptFocus   // фокус клавиатуры не берёт
                          | Qt::NoDropShadowWindowHint;

    const QString platform = QGuiApplication::platformName();

    if (platform.startsWith(QLatin1String("wayland"))) {
        // Wayland: клиент не управляет ни положением, ни слоем окна, а WindowStaysOnTopHint
        // игнорируется. Всё это (keepAbove, «без панели задач», геометрию «на весь экран»)
        // делает KWin-скрипт из KWinTracker — он находит наше окно по app_id и заголовку.
    } else if (platform == QLatin1String("xcb")) {
        // X11 / XWayland: BypassWindowManagerHint = override-redirect. Окно полностью вне
        // власти WM: нет рамки, нет записи в панели задач, не получает фокус, остаётся
        // сверху, а позицию мы задаём сами. В KWin такие окна рисуются поверх остальных.
        flags |= Qt::Tool | Qt::BypassWindowManagerHint;
    } else {
        // Windows / macOS: адаптации появятся позже.
        flags |= Qt::Tool;
    }

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
        if (QWindow *wh = windowHandle())
            wh->setScreen(m_screen);
    }
    updateScreenGeometry();
}

void SnowWindow::updateScreenGeometry()
{
    m_screenRect = m_screen ? m_screen->geometry() : QRect(0, 0, 1280, 720);
    setGeometry(m_screenRect);   // под Wayland позицию игнорирует Qt, её выставит KWin-скрипт
    applyHole();
    emit overlayGeometryChanged(m_screenRect);
}

void SnowWindow::setActiveRect(const QRect &globalRect)
{
    m_activeGlobal = globalRect;
    applyHole();
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

    // Перерисовываем и старую, и новую «дырку»: там, где активное окно уехало,
    // снег появляется; там, где приехало — пропадает (clip в paintEvent).
    update(m_hole + newHole);
    m_hole = newHole;
}

// ---------------------------------------------------------------------------
// События окна
// ---------------------------------------------------------------------------
void SnowWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyHole();
}

void SnowWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (isWaylandPlatform())
        QTimer::singleShot(0, this, &SnowWindow::applyWaylandInputRegion);
}

// Qt::WindowTransparentForInput под Wayland поддерживается не всеми версиями Qt.
// Подстраховка: задаём окну маску, лежащую ЗА пределами окна. Для Wayland маска —
// это только input-region (картинка не обрезается), т.е. окно получает «пустую»
// область ввода и все клики идут в нижележащие окна. На xcb так делать нельзя:
// там маска обрезала бы и саму картинку, поэтому вызывается только для Wayland.
void SnowWindow::applyWaylandInputRegion()
{
    if (QWindow *wh = windowHandle())
        wh->setMask(QRegion(-10, -10, 1, 1));
}

// ---------------------------------------------------------------------------
// Снежинки
// ---------------------------------------------------------------------------
QRect SnowWindow::flakeRect(const Flake &f)
{
    const double m = f.radius * 1.8 + 1.5;   // с запасом на ореол и антиалиасинг
    return QRectF(f.x - m, f.y - m, 2 * m, 2 * m).toAlignedRect();
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
    f.radius     = 1.2 + 2.8 * u * u;                              // мелких больше, крупных меньше
    f.speed      = 22.0 + f.radius * 16.0 + rnd(0.0, 28.0);        // крупные («ближние») падают быстрее
    f.alpha      = qBound(0.30, 0.35 + 0.12 * f.radius + rnd(-0.10, 0.15), 0.95);
    f.swayAmp    = rnd(6.0, 28.0);                                 // амплитуда покачивания
    f.swayFreq   = rnd(0.6, 1.8);                                  // частота покачивания
    f.phase      = rnd(0.0, kTwoPi);
    f.windFactor = 0.4 + f.radius * 0.25;
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

    // Общий «ветер»: медленно меняющийся снос в px/с (положительный — вправо).
    const double wind = 14.0 * std::sin(m_time * 0.25) + 7.0 * std::sin(m_time * 0.73 + 1.3);
    const double margin = 30.0;

    QRegion dirty;   // обновляем только те места, где снежинки были и где стали
    for (Flake &f : m_flakes) {
        dirty += f.rect;

        f.y     += f.speed * dt;
        f.baseX += wind * f.windFactor * dt;
        f.phase += f.swayFreq * dt;

        // Упала за нижний край → снова сверху, в случайной точке по X.
        if (f.y - f.radius > h)
            respawn(f, false);

        // По горизонтали экран «зациклен» (ветер уносит за край — вернётся с другого).
        if (f.baseX < -margin)
            f.baseX += w + 2 * margin;
        else if (f.baseX > w + margin)
            f.baseX -= w + 2 * margin;

        f.x = f.baseX + std::sin(f.phase) * f.swayAmp;   // покачивание по синусоиде
        f.rect = flakeRect(f);
        dirty += f.rect;
    }
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

    if (clip.isEmpty())
        return;

    p.setClipRegion(clip);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    const QRect bounds = clip.boundingRect();
    for (const Flake &f : m_flakes) {
        if (!f.rect.intersects(bounds))
            continue;

        // Мягкий ореол у крупных снежинок
        if (f.radius > 2.4) {
            QColor halo(200, 225, 255);
            halo.setAlphaF(f.alpha * 0.25);
            p.setBrush(halo);
            const double hr = f.radius * 1.7;
            p.drawEllipse(QPointF(f.x, f.y), hr, hr);
        }

        QColor core(240, 247, 255);
        core.setAlphaF(f.alpha);
        p.setBrush(core);
        p.drawEllipse(QPointF(f.x, f.y), f.radius, f.radius);
    }
}
