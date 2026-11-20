#include "AutumnWeather.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRandomGenerator>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace {

constexpr int rainCount = 240;
constexpr int leafCount = 36;
constexpr std::size_t maxSplashes = 80;
constexpr double splashLife = 0.35;
constexpr double splashGravity = 420;
constexpr double dropMargin = 40;    // за краем экрана капли «зациклены» по горизонтали
constexpr double leafMargin = 110;   // больше амплитуды покачивания: перенос листа не виден
constexpr double pi = 3.141592653589793;
constexpr double twoPi = 6.283185307179586;

double rnd(double a, double b)
{
    return a + (b - a) * QRandomGenerator::global()->generateDouble();
}

// ---------------------------------------------------------------------------
// Дождь: четыре «слоя глубины». Дальние капли тоньше, короче, медленнее и прозрачнее.
// Перо на слой создаётся один раз, а не на каплю.
// ---------------------------------------------------------------------------
struct RainBand {
    double width;
    int alpha;
    double minLength, maxLength;
    double minSpeed, maxSpeed;
};

constexpr std::array<RainBand, 4> rainBands{{
    {0.8, 55, 6, 9, 480, 560},
    {1.0, 85, 9, 12, 560, 660},
    {1.2, 120, 12, 15, 660, 780},
    {1.5, 160, 15, 19, 780, 900},
}};

constexpr int rainColorR = 165;
constexpr int rainColorG = 196;
constexpr int rainColorB = 236;

const std::array<QPen, rainBands.size()> &rainPens()
{
    static const std::array<QPen, rainBands.size()> pens = [] {
        std::array<QPen, rainBands.size()> result;
        for (std::size_t i = 0; i < result.size(); ++i) {
            result[i] = QPen(QColor(rainColorR, rainColorG, rainColorB, rainBands[i].alpha),
                             rainBands[i].width, Qt::SolidLine, Qt::RoundCap);
        }
        return result;
    }();
    return pens;
}

// ---------------------------------------------------------------------------
// Листья. Контуры строятся один раз в «единичных» координатах: кончик листа в
// (0, -1), основание в (0, 1), черешок уходит чуть ниже. При рисовании контур
// просто масштабируется и поворачивается — спрайты не нужны, а края остаются чёткими.
// ---------------------------------------------------------------------------
constexpr int leafShapeCount = 3;
constexpr int leafColorCount = 6;

struct LeafShape {
    QPainterPath outline;
    QPainterPath veins;   // жилки и черешок
};

struct LeafColor {
    QColor fill;
    QColor edge;
};

// right — правая половина контура от кончика к основанию (обе крайние точки на оси).
QPolygonF mirrored(const std::vector<QPointF> &right)
{
    QPolygonF polygon;
    for (const QPointF &point : right)
        polygon << point;
    for (auto it = right.rbegin() + 1; it != right.rend() - 1; ++it)
        polygon << QPointF(-it->x(), it->y());
    return polygon;
}

void addSideVeins(QPainterPath &veins, std::initializer_list<double> heights, double reach, double rise)
{
    for (const double y : heights) {
        for (const double side : {-1.0, 1.0}) {
            veins.moveTo(0, y);
            veins.lineTo(side * reach, y - rise);
        }
    }
}

// Гладкий листок (берёза, липа).
LeafShape makeSmoothLeaf()
{
    constexpr int steps = 28;
    std::vector<QPointF> right;
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        right.emplace_back(0.62 * std::sin(pi * std::pow(t, 1.25)), -1 + 2 * t);
    }
    LeafShape shape;
    shape.outline.addPolygon(mirrored(right));
    shape.outline.closeSubpath();
    shape.veins.moveTo(0, 1.28);
    shape.veins.lineTo(0, -0.75);
    addSideVeins(shape.veins, {0.55, 0.15, -0.25}, 0.33, 0.28);
    return shape;
}

// Лопастной лист дуба.
LeafShape makeOakLeaf()
{
    constexpr int steps = 40;
    std::vector<QPointF> right;
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        const double envelope = std::pow(std::max(0.0, std::sin(pi * std::pow(t, 1.1))), 0.85);
        const double lobes = 0.72 + 0.28 * std::abs(std::sin(pi * 3.5 * t));
        right.emplace_back(0.64 * envelope * lobes, -1 + 2 * t);
    }
    LeafShape shape;
    shape.outline.addPolygon(mirrored(right));
    shape.outline.closeSubpath();
    shape.veins.moveTo(0, 1.28);
    shape.veins.lineTo(0, -0.7);
    addSideVeins(shape.veins, {0.5, 0.0, -0.45}, 0.34, 0.22);
    return shape;
}

// Пятилопастной кленовый лист: «звезда» в полярных координатах с широкими лопастями.
LeafShape makeMapleLeaf()
{
    struct Lobe {
        double angle;   // градусы от «вверх» по часовой стрелке
        double reach;
        double width;   // полуширина лопасти, градусы
    };
    constexpr std::array<Lobe, 5> lobes{{
        {0, 1.0, 31}, {64, 0.88, 29}, {-64, 0.88, 29}, {128, 0.58, 24}, {-128, 0.58, 24},
    }};
    constexpr double hub = 0.34;
    constexpr int steps = 160;

    QPolygonF polygon;
    for (int i = 0; i < steps; ++i) {
        const double degrees = -180.0 + 360.0 * i / steps;
        double radius = hub;
        for (const Lobe &lobe : lobes) {
            const double distance = std::abs(std::fmod(degrees - lobe.angle + 540.0, 360.0) - 180.0);
            if (distance >= lobe.width)
                continue;
            double length = hub + (lobe.reach - hub) * std::pow(1 - distance / lobe.width, 0.85);
            length *= 1 + 0.05 * std::cos(distance / lobe.width * 3 * pi);   // зубчики по краю
            radius = std::max(radius, length);
        }
        const double angle = degrees * pi / 180;
        polygon << QPointF(radius * std::sin(angle), -radius * std::cos(angle));
    }
    LeafShape shape;
    shape.outline.addPolygon(polygon);
    shape.outline.closeSubpath();
    shape.veins.moveTo(0, 1.28);
    shape.veins.lineTo(0, -0.6);
    for (std::size_t i = 1; i < lobes.size(); ++i) {   // жилки в боковые лопасти
        const double angle = lobes[i].angle * pi / 180;
        const double reach = lobes[i].reach * 0.62;
        shape.veins.moveTo(0, 0.05);
        shape.veins.lineTo(reach * std::sin(angle), -reach * std::cos(angle));
    }
    return shape;
}

const std::array<LeafShape, leafShapeCount> &leafShapes()
{
    static const std::array<LeafShape, leafShapeCount> shapes{{
        makeSmoothLeaf(), makeMapleLeaf(), makeOakLeaf(),
    }};
    return shapes;
}

const std::array<LeafColor, leafColorCount> &leafPalette()
{
    static const std::array<LeafColor, leafColorCount> palette = [] {
        const std::array<QColor, leafColorCount> fills{{
            QColor(196, 52, 32),    // красный
            QColor(222, 98, 28),    // оранжевый
            QColor(236, 158, 36),   // янтарный
            QColor(214, 178, 52),   // жёлтый
            QColor(150, 72, 32),    // коричневый
            QColor(168, 38, 40),    // багровый
        }};
        std::array<LeafColor, leafColorCount> result;
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = {fills[i], fills[i].darker(170)};
        return result;
    }();
    return palette;
}

} // namespace

// ===========================================================================

void AutumnWeather::setSize(const QSize &size)
{
    if (m_size == size)
        return;
    m_size = size;
    m_splashes.clear();
    populateRain();
    populateLeaves();
}

void AutumnWeather::setGround(const QRect &dock)
{
    m_dock = dock;
}

void AutumnWeather::setOptions(const AutumnOptions &options)
{
    const AutumnOptions previous = m_options;
    m_options = options;
    // Перезаселяем только то, что изменилось: при переключении дождя листья не «прыгают».
    if (previous.rain != options.rain) {
        m_splashes.clear();
        populateRain();
    }
    if (previous.leaves != options.leaves)
        populateLeaves();
}

void AutumnWeather::populateRain()
{
    m_drops.clear();
    if (!m_options.rain || m_size.isEmpty())
        return;
    m_drops.assign(rainCount, Drop());
    for (Drop &drop : m_drops)
        respawn(drop, true);
}

void AutumnWeather::populateLeaves()
{
    m_leaves.clear();
    if (!m_options.leaves || m_size.isEmpty())
        return;
    m_leaves.assign(leafCount, Leaf());
    for (Leaf &leaf : m_leaves)
        respawn(leaf, true);
}

void AutumnWeather::respawn(Drop &drop, bool initial)
{
    const double w = qMax(1, m_size.width());
    const double h = qMax(1, m_size.height());

    const double u = rnd(0, 1);
    drop.band = u < 0.4 ? 0 : u < 0.7 ? 1 : u < 0.9 ? 2 : 3;   // дальних капель больше
    const RainBand &band = rainBands[drop.band];
    drop.speed = rnd(band.minSpeed, band.maxSpeed);
    drop.length = rnd(band.minLength, band.maxLength);
    drop.windFactor = rnd(0.85, 1.15);
    drop.x = rnd(-dropMargin, w + dropMargin);
    drop.y = initial ? rnd(0, h) : -rnd(drop.length, 120);
    drop.rect = dropRect(streak(drop));
}

void AutumnWeather::respawn(Leaf &leaf, bool initial)
{
    const double w = qMax(1, m_size.width());
    const double h = qMax(1, m_size.height());

    leaf.size = rnd(7, 15);
    leaf.speed = rnd(34, 70) + leaf.size * 0.8;   // крупные листья чуть быстрее
    leaf.swayAmp = rnd(24, 70);
    leaf.swayFreq = rnd(0.6, 1.5);
    leaf.phase = rnd(0, twoPi);
    leaf.windFactor = rnd(1.3, 2.6);
    leaf.angle = rnd(0, twoPi);
    leaf.spin = rnd(-0.6, 0.6);
    leaf.flip = rnd(0, twoPi);
    leaf.flipSpeed = rnd(1.4, 3.6);
    leaf.opacity = rnd(0.82, 1.0);
    leaf.shape = QRandomGenerator::global()->bounded(leafShapeCount);
    leaf.color = QRandomGenerator::global()->bounded(leafColorCount);
    leaf.baseX = rnd(0, w);
    leaf.y = initial ? rnd(0, h) : -rnd(leaf.size * 1.5, 90);
    leaf.x = leaf.baseX + std::sin(leaf.phase) * leaf.swayAmp;
    leaf.rect = leafRect(leaf);
}

double AutumnWeather::groundAt(double x) const
{
    if (!m_dock.isEmpty() && x >= m_dock.left() && x <= m_dock.right() + 1)
        return m_dock.top();
    return m_size.height();
}

QLineF AutumnWeather::streak(const Drop &drop) const
{
    const double vx = m_rainSlant * drop.windFactor;
    const double norm = std::hypot(vx, drop.speed);
    return QLineF(drop.x - vx / norm * drop.length, drop.y - drop.speed / norm * drop.length, drop.x, drop.y);
}

QRect AutumnWeather::dropRect(const QLineF &line)
{
    return QRectF(line.p1(), line.p2()).normalized().adjusted(-2, -2, 2, 2).toAlignedRect();
}

QRect AutumnWeather::leafRect(const Leaf &leaf)
{
    // Самая дальняя от центра точка — конец черешка (1.28 размера); радиус не зависит от поворота.
    const double radius = leaf.size * 1.3 + 2;
    return QRectF(leaf.x - radius, leaf.y - radius, radius * 2, radius * 2).toAlignedRect();
}

QRect AutumnWeather::splashRect(const Splash &splash)
{
    return QRectF(splash.origin.x() - 16, splash.origin.y() - 15, 32, 19).toAlignedRect();
}

QRegion AutumnWeather::advance(double dt, const QRect &hole)
{
    QRegion dirty;
    if (m_size.isEmpty())
        return dirty;

    m_time += dt;
    const double w = m_size.width();
    // Тот же «ветер», что и у снега, но дождь только слегка наклоняется, а листья сносит сильнее.
    const double wind = 14.0 * std::sin(m_time * 0.25) + 7.0 * std::sin(m_time * 0.73 + 1.3);
    m_rainSlant = 45 + wind * 1.6;

    const auto mark = [&](const QRect &damage) {
        if (!hole.contains(damage))
            dirty += damage;
    };
    const auto markMove = [&](const QRect &before, const QRect &after) {
        if (before.intersects(after)) {
            mark(before.united(after));
        } else {
            mark(before);
            mark(after);
        }
    };

    for (Drop &drop : m_drops) {
        const QRect before = drop.rect;
        drop.x += m_rainSlant * drop.windFactor * dt;
        drop.y += drop.speed * dt;
        if (drop.x > w + dropMargin)
            drop.x -= w + 2 * dropMargin;
        else if (drop.x < -dropMargin)
            drop.x += w + 2 * dropMargin;

        const double ground = groundAt(drop.x);
        if (drop.y >= ground) {
            if (m_splashes.size() < maxSplashes && drop.band >= 1 && drop.x >= 0 && drop.x <= w &&
                rnd(0, 1) < 0.55) {
                m_splashes.push_back({QPointF(drop.x, ground - rnd(0, 2)), 0, rnd(14, 30), rnd(55, 95)});
            }
            respawn(drop, false);
        }
        drop.rect = dropRect(streak(drop));
        markMove(before, drop.rect);
    }

    for (Splash &splash : m_splashes) {
        splash.age += dt;
        mark(splashRect(splash));   // и кадр, в котором всплеск исчезает: его надо стереть
    }
    m_splashes.erase(std::remove_if(m_splashes.begin(), m_splashes.end(),
                                    [](const Splash &splash) { return splash.age >= splashLife; }),
                     m_splashes.end());

    const double span = w + 2 * leafMargin;
    for (Leaf &leaf : m_leaves) {
        const QRect before = leaf.rect;
        leaf.y += leaf.speed * dt;
        leaf.baseX += wind * leaf.windFactor * dt;
        leaf.phase += leaf.swayFreq * dt;
        leaf.angle += leaf.spin * dt;
        leaf.flip += leaf.flipSpeed * dt;

        if (leaf.baseX < -leafMargin || leaf.baseX > w + leafMargin) {
            leaf.baseX = std::fmod(leaf.baseX + leafMargin, span);
            if (leaf.baseX < 0)
                leaf.baseX += span;
            leaf.baseX -= leafMargin;
        }
        leaf.x = leaf.baseX + std::sin(leaf.phase) * leaf.swayAmp;

        // К этому моменту лист уже полностью растворился (см. paint()), поэтому «приземление» не видно.
        if (leaf.y >= groundAt(leaf.x))
            respawn(leaf, false);
        leaf.rect = leafRect(leaf);
        markMove(before, leaf.rect);
    }
    return dirty;
}

QRegion AutumnWeather::overlapping(const QRegion &area) const
{
    QRegion result;
    for (const Drop &drop : m_drops) {
        if (area.intersects(drop.rect))
            result += drop.rect;
    }
    for (const Splash &splash : m_splashes) {
        const QRect rect = splashRect(splash);
        if (area.intersects(rect))
            result += rect;
    }
    for (const Leaf &leaf : m_leaves) {
        if (area.intersects(leaf.rect))
            result += leaf.rect;
    }
    return result;
}

void AutumnWeather::paint(QPainter &p, const QRegion &clip) const
{
    const QRect bounds = clip.boundingRect();
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // --- Дождь -------------------------------------------------------------
    if (!m_drops.empty()) {
        const auto &pens = rainPens();
        p.setBrush(Qt::NoBrush);
        for (const Drop &drop : m_drops) {
            if (!drop.rect.intersects(bounds) || !clip.intersects(drop.rect))
                continue;
            p.setPen(pens[drop.band]);
            p.drawLine(streak(drop));
        }
    }

    // --- Всплески: дуга-«корона» и две капельки ---------------------------------
    for (const Splash &splash : m_splashes) {
        const QRect rect = splashRect(splash);
        if (!rect.intersects(bounds) || !clip.intersects(rect))
            continue;
        const double t = qBound(0.0, splash.age / splashLife, 1.0);
        const QColor color(rainColorR, rainColorG, rainColorB, qRound(150 * (1 - t)));

        const double rx = 2 + 9 * t;
        const double ry = rx * 0.4;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(color, 1.0));
        p.drawArc(QRectF(splash.origin.x() - rx, splash.origin.y() - ry, rx * 2, ry * 2), 0, 180 * 16);

        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (const double side : {-1.0, 1.0}) {
            const QPointF droplet(splash.origin.x() + side * splash.sideSpeed * splash.age,
                                  splash.origin.y() - splash.upSpeed * splash.age +
                                      0.5 * splashGravity * splash.age * splash.age);
            p.drawEllipse(droplet, 1.0, 1.0);
        }
    }

    // --- Листья ------------------------------------------------------------
    const auto &shapes = leafShapes();
    const auto &palette = leafPalette();
    for (const Leaf &leaf : m_leaves) {
        if (!leaf.rect.intersects(bounds) || !clip.intersects(leaf.rect))
            continue;
        // У земли (низ экрана или док) лист плавно растворяется.
        const double fade = qBound(0.0, (groundAt(leaf.x) - leaf.y) / (leaf.size * 2.5), 1.0);
        if (fade <= 0)
            continue;

        const LeafShape &shape = shapes[leaf.shape];
        const LeafColor &color = palette[leaf.color];
        const double turn = std::cos(leaf.flip);   // «переворот»: лист то плашмя, то ребром
        const double width = leaf.size * std::max(0.16, std::abs(turn));
        const QColor fill = turn >= 0 ? color.fill : color.fill.darker(118);   // обратная сторона темнее

        p.save();
        p.setOpacity(leaf.opacity * fade);
        p.translate(leaf.x, leaf.y);
        p.rotate((leaf.angle + 0.55 * std::sin(leaf.phase)) * 180.0 / pi);
        p.scale(width, leaf.size);

        QPen veinPen(color.edge, 0.09);   // толщина задана в единичных координатах
        veinPen.setCapStyle(Qt::RoundCap);
        veinPen.setJoinStyle(Qt::RoundJoin);
        p.setPen(veinPen);
        p.setBrush(fill);
        p.drawPath(shape.outline);
        p.setBrush(Qt::NoBrush);
        p.drawPath(shape.veins);
        p.restore();
    }

    p.restore();
}
