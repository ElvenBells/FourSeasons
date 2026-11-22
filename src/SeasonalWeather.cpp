#include "SeasonalWeather.h"
#include "Constants.h"

#include <QCache>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRandomGenerator>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <vector>
#include <utility>

namespace {

constexpr int rainCount = 240;
constexpr int leafCount = 36;
constexpr int petalCount = 48;
constexpr int fireflyCount = 8;
constexpr int glowRadius = 9;
constexpr std::size_t maxGroundLeaves = 210;
constexpr std::size_t maxGroundPiles = 72;
constexpr std::size_t maxPileLeaves = 16;
constexpr int pileThreshold = 5;
constexpr double pileSpacing = 56;
constexpr std::size_t maxScatteredLeaves = 96;
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

constexpr std::array<RainBand, 5> rainBands{{
    {0.8, 55, 6, 9, 480, 560},
    {1.0, 85, 9, 12, 560, 660},
    {1.2, 120, 12, 15, 660, 780},
    {1.5, 160, 15, 19, 780, 900},
    {2.1, 178, 18, 29, 875, 1050},
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

QRect rainBounds(const QPoint &head, const QPoint &streak)
{
    return QRect(head, head - streak).normalized().adjusted(-2, -2, 2, 2);
}

const QImage &rainSprite(int band, const QPoint &streak)
{
    static QCache<int, QImage> sprites(1024);
    const int key = band * 16384 + (streak.x() + 64) * 128 + streak.y();
    if (const QImage *image = sprites.object(key))
        return *image;

    const QRect bounds = rainBounds({}, streak);
    auto image = std::make_unique<QImage>(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    image->fill(Qt::transparent);
    QPainter painter(image.get());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(-bounds.topLeft());
    painter.setPen(rainPens()[band]);
    painter.drawLine(-streak, QPoint());
    painter.end();
    QImage *saved = image.release();
    sprites.insert(key, saved);
    return *saved;
}

// ---------------------------------------------------------------------------
// Листья. Контуры строятся один раз в «единичных» координатах: кончик листа в
// (0, -1), основание в (0, 1), черешок уходит чуть ниже. При рисовании контур
// сохраняется в спрайт с запасом разрешения для поворота и масштабирования.
// ---------------------------------------------------------------------------
constexpr int leafShapeCount = 6;
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
    static const auto shapes = [] {
        std::array<LeafShape, leafShapeCount> result{{
            makeSmoothLeaf(), makeMapleLeaf(), makeOakLeaf(), {}, {}, {},
        }};
        LeafShape &rowan = result[3];
        rowan.outline.addEllipse(QRectF(-.16, -1, .32, .54));
        rowan.veins.moveTo(0, -.76);
        rowan.veins.lineTo(0, 1.25);
        for (int i = 0; i < 4; ++i) {
            for (const int side : {-1, 1}) {
                const double y = -.54 + i * .39;
                rowan.outline.addEllipse(QRectF(side < 0 ? -.64 : .06, y - .14, .58, .27));
                rowan.veins.moveTo(0, y + .11);
                rowan.veins.lineTo(side * .54, y);
            }
        }
        result[4].outline.moveTo(0, -1);
        result[4].outline.quadTo(.91, -.04, .32, .62);
        result[4].outline.quadTo(0, 1.04, -.35, .62);
        result[4].outline.quadTo(-.77, -.13, 0, -1);
        result[4].veins.moveTo(0, 1.09);
        result[4].veins.quadTo(.1, .3, 0, -.75);
        result[5].outline.moveTo(.45, -.93);
        result[5].outline.cubicTo(.96, -.66, -.5, -.29, -.45, .12);
        result[5].outline.cubicTo(-.4, .66, .47, .58, .24, 1.1);
        result[5].outline.cubicTo(-.56, .74, -.89, .31, -.64, -.23);
        result[5].outline.quadTo(-.39, -.64, .45, -.93);
        result[5].veins.moveTo(.15, .85);
        result[5].veins.quadTo(-.87, .28, .18, -.65);
        return result;
    }();
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

const std::array<LeafShape, 3> &petalShapes()
{
    static const auto shapes = [] {
        std::array<LeafShape, 3> result{};
        LeafShape &sakura = result[0];
        sakura.outline.moveTo(0, 1);
        sakura.outline.cubicTo(-.24, .55, -.92, -.1, -.72, -.66);
        sakura.outline.cubicTo(-.56, -1.08, -.16, -1.07, 0, -.82);
        sakura.outline.cubicTo(.16, -1.07, .56, -1.08, .72, -.66);
        sakura.outline.cubicTo(.92, -.1, .24, .55, 0, 1);
        sakura.outline.closeSubpath();
        result[1].outline.addEllipse(QRectF(-.69, -1, 1.38, 1.78));
        result[2].outline.moveTo(-.14, 1);
        result[2].outline.cubicTo(-.92, .02, -.21, -.83, .51, -1);
        result[2].outline.cubicTo(.8, -.29, .42, .56, -.14, 1);
        for (LeafShape &shape : result) {
            shape.veins.moveTo(0, .73);
            shape.veins.quadTo(-.18, .1, -.28, -.32);
        }
        return result;
    }();
    return shapes;
}

const std::array<LeafColor, 3> &petalPalette()
{
    static const std::array<LeafColor, 3> palette{{
        {QColor(255, 183, 208), QColor(196, 88, 135)},
        {QColor(255, 222, 234), QColor(212, 123, 161)},
        {QColor(246, 155, 193), QColor(167, 78, 127)},
    }};
    return palette;
}

template <std::size_t shapes, std::size_t colors>
auto makeLeafSprites(const std::array<LeafShape, shapes> &forms, const std::array<LeafColor, colors> &palette)
{
    std::array<std::array<std::array<QImage, 2>, colors>, shapes> sprites;
    constexpr int size = 72;
    for (std::size_t shape = 0; shape < shapes; ++shape) {
        for (std::size_t color = 0; color < colors; ++color) {
            for (int dark = 0; dark < 2; ++dark) {
                QImage &image = sprites[shape][color][dark];
                image = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                QPainter p(&image);
                p.setRenderHint(QPainter::Antialiasing);
                p.translate(size / 2.0, size / 2.0);
                p.scale(size / 2.8, size / 2.8);
                p.setPen(QPen(palette[color].edge, .085, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(dark ? palette[color].fill.darker(122) : palette[color].fill);
                p.drawPath(forms[shape].outline);
                p.setBrush(Qt::NoBrush);
                p.drawPath(forms[shape].veins);
            }
        }
    }
    return sprites;
}

const QImage &leafSprite(bool spring, int shape, int color, bool dark)
{
    if (spring) {
        static const auto petals = makeLeafSprites(petalShapes(), petalPalette());
        return petals[shape][color][dark];
    }
    static const auto leaves = makeLeafSprites(leafShapes(), leafPalette());
    return leaves[shape][color][dark];
}

const QImage &fireflyGlow()
{
    static const QImage sprite = [] {
        QImage image(glowRadius * 2, glowRadius * 2, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        const QPointF center(glowRadius, glowRadius);
        QRadialGradient glow(center, glowRadius);
        glow.setColorAt(0, QColor(255, 255, 196, 255));
        glow.setColorAt(.2, QColor(227, 255, 127, 210));
        glow.setColorAt(.48, QColor(165, 223, 70, 80));
        glow.setColorAt(1, QColor(140, 210, 55, 0));
        painter.fillRect(image.rect(), glow);
        painter.end();
        return image;
    }();
    return sprite;
}

} // namespace

// ===========================================================================

void SeasonalWeather::setSize(const QSize &size)
{
    if (m_size == size)
        return;
    m_size = size;
    m_scenery.setLayout(size, m_dock);
    m_groundLeaves.clear();
    m_groundPiles.clear();
    for (WindowLedge &window : m_windows)
        window.leaves.clear();
    m_scattered.clear();
    restart();
}

void SeasonalWeather::setSeason(Season season)
{
    if (m_season == season)
        return;
    m_season = season;
    m_scenery.setSeason(season);
    m_tool = SeasonTool::Passive;
    m_jar = false;
    m_caught = 0;
    m_afterRain = 0;
    m_groundLeaves.clear();
    m_groundPiles.clear();
    for (WindowLedge &window : m_windows)
        window.leaves.clear();
    m_scattered.clear();
    restart();
}

void SeasonalWeather::restart()
{
    m_splashes.clear();
    m_bubbles.clear();
    m_bubbleDelay = 0;
    m_impacts.clear();
    m_flight.reset();
    m_blower.reset();
    m_pointer.reset();
    for (const Drop &drop : m_drops)
        m_damage += drop.rect;
    for (const Leaf &leaf : m_leaves)
        m_damage += leaf.rect;
    m_drops.clear();
    m_leaves.clear();
    populateRain();
    populateLeaves();
    populateFireflies();
}

void SeasonalWeather::setGround(const QRect &dock)
{
    if (m_dock == dock)
        return;
    m_damage += extraVisualRegion();
    const QRect previousDock = m_dock;
    m_dock = dock;
    m_scenery.setLayout(m_size, dock);
    for (Leaf &leaf : m_groundLeaves) {
        const bool wasOnDock = !previousDock.isEmpty() && leaf.x >= previousDock.left() &&
                               leaf.x < previousDock.right() + 1;
        leaf.y += groundAt(leaf.x) - (wasOnDock ? previousDock.top() : m_size.height());
        leaf.rect = leafRect(leaf);
    }
    for (GroundPile &pile : m_groundPiles) {
        pile.base.setY(groundAt(pile.base.x()));
        layoutPile(pile);
    }
    m_damage += extraVisualRegion();
}

void SeasonalWeather::setOptions(const AutumnOptions &options)
{
    const AutumnOptions previous = m_options;
    if (m_season == Season::Autumn)
        m_damage += overlapping(QRect(QPoint(), m_size));
    m_options = options;
    if (m_season != Season::Autumn)
        return;
    // Перезаселяем только то, что изменилось: при переключении дождя листья не «прыгают».
    if (previous.rain != options.rain) {
        m_damage += mistRect();
        m_splashes.clear();
        populateRain();
    }
    if (previous.leaves != options.leaves) {
        populateLeaves();
        m_groundLeaves.clear();
        m_groundPiles.clear();
        for (WindowLedge &window : m_windows) {
            if (!window.leaves.empty())
                m_damage += window.exposed;
            window.leaves.clear();
        }

        m_scattered.clear();
    }
}

void SeasonalWeather::setSpringRain(bool enabled)
{
    if (m_springRain == enabled)
        return;
    if (m_season == Season::Spring)
        m_damage += overlapping(QRect(QPoint(), m_size));
    m_springRain = enabled;
    if (m_season == Season::Spring) {
        m_damage += mistRect();
        m_splashes.clear();
        populateRain();
    }
}

double SeasonalWeather::rainStrength() const
{
    const double intensity = m_precipitation.intensity / 100.0;
    if (m_season == Season::Spring && m_springRain)
        return qMin(1.0, .36 * intensity);
    if (m_season == Season::Autumn && m_options.rain)
        return qMin(1.0, intensity * (.67 + .33 * std::sin(m_time * .115 + 1.1)));
    return 0;
}

void SeasonalWeather::setPrecipitation(const Precipitation::Settings &settings)
{
    const int intensity = m_precipitation.intensity;
    m_precipitation = {qBound(0, settings.intensity, SnowSettings::maxIntensity),
                       qBound(SnowSettings::minSpeed, settings.speed, SnowSettings::maxSpeed),
                       qBound(-SnowSettings::maxWind, settings.wind, SnowSettings::maxWind)};
    if (intensity != m_precipitation.intensity) {
        populateRain();
        populateLeaves();
    }
}

void SeasonalWeather::populateRain()
{
    const int count = rainStrength() > 0 && !m_size.isEmpty()
        ? qRound((m_season == Season::Spring ? rainCount / 3 : rainCount) * m_precipitation.intensity / 100.0) : 0;
    const int first = static_cast<int>(m_drops.size());
    for (int i = count; i < first; ++i)
        m_damage += m_drops[i].rect;
    m_drops.resize(count);
    for (int i = first; i < count; ++i) {
        Drop &drop = m_drops[i];
        respawn(drop);
        Precipitation::stage(drop, drop.length, m_size.height(), i - first, count - first);
        updateDropBounds(drop);
    }
}

void SeasonalWeather::populateLeaves()
{
    const bool spring = m_season == Season::Spring;
    const bool enabled = spring || (m_season == Season::Autumn && m_options.leaves);
    const int count = enabled && !m_size.isEmpty()
        ? qRound((spring ? petalCount : leafCount) * m_precipitation.intensity / 100.0) : 0;
    const int first = static_cast<int>(m_leaves.size());
    for (int i = count; i < first; ++i)
        m_damage += m_leaves[i].rect;
    m_leaves.resize(count);
    for (int i = first; i < count; ++i) {
        Leaf &leaf = m_leaves[i];
        respawn(leaf);
        Precipitation::stage(leaf, leaf.size + 3, m_size.height(), i - first, count - first);
        leaf.rect = leafRect(leaf);
    }
}

void SeasonalWeather::populateFireflies()
{
    m_fireflies.clear();
    if (m_season != Season::Summer || !m_scenery.night()) {
        m_caught = 0;
        return;
    }
    if (m_size.isEmpty())
        return;
    m_fireflies.resize(fireflyCount);
    for (Firefly &fly : m_fireflies)
        respawn(fly);
    // При возобновлении ночной сцены уже пойманные остаются в банке.
    for (int i = 0; i < m_caught; ++i)
        m_fireflies[i].boost = -1;
}

void SeasonalWeather::respawn(Drop &drop)
{
    const double w = qMax(1, m_size.width());

    const double u = rnd(0, 1);
    drop.band = u < .4 ? 0 : u < .7 ? 1 : u < .89 ? 2 : u < .98 ? 3 : 4;
    const RainBand &band = rainBands[drop.band];
    drop.speed = rnd(band.minSpeed, band.maxSpeed);
    drop.length = rnd(band.minLength, band.maxLength);
    drop.windFactor = rnd(0.85, 1.15);
    drop.x = drop.baseX = rnd(0, w);
    drop.y = -rnd(drop.length, 60);
    updateDropBounds(drop);
}

void SeasonalWeather::respawn(Leaf &leaf)
{
    const double w = qMax(1, m_size.width());
    const bool spring = m_season == Season::Spring;

    leaf.shape = QRandomGenerator::global()->bounded(spring ? int(petalShapes().size()) : leafShapeCount);
    leaf.wet = !spring && rnd(0, 1) < rainStrength() * .72;
    leaf.dry = !spring && !leaf.wet && (leaf.shape == 5 || rnd(0, 1) < .3);
    leaf.size = spring ? rnd(3, 7.5) : leaf.shape == 4 ? rnd(4, 8) : rnd(7, 15);
    leaf.speed = (spring ? rnd(11, 23) : rnd(25, 47)) + leaf.size * 1.8 + (leaf.wet ? 33 : 0);
    leaf.swayAmp = leaf.wet ? rnd(10, 23) : rnd(25, 70);
    leaf.swayFreq = leaf.wet ? rnd(.35, .75) : rnd(.65, 1.85);
    leaf.phase = rnd(0, twoPi);
    leaf.windFactor = rnd(1.3, 2.6);
    leaf.angle = rnd(0, twoPi);
    leaf.spin = rnd(-0.6, 0.6);
    leaf.flip = rnd(0, twoPi);
    leaf.flipSpeed = rnd(1.4, 3.6);
    leaf.opacity = rnd(0.82, 1.0);
    leaf.color = QRandomGenerator::global()->bounded(spring ? int(petalPalette().size()) : leafColorCount);
    leaf.baseX = rnd(0, w);
    leaf.y = -rnd(leaf.size * 1.3 + 3, 60);
    leaf.x = leaf.baseX + std::sin(leaf.phase) * leaf.swayAmp;
    leaf.rect = leafRect(leaf);
}

void SeasonalWeather::respawn(Firefly &fly)
{
    fly.x = rnd(0, m_size.width());
    fly.y = rnd(m_size.height() * .3, m_size.height() * .85);
    fly.speed = rnd(13, 29);
    fly.phase = rnd(0, twoPi);
    fly.boost = 0;
    fly.impulse = {};
}

QPointF SeasonalWeather::fireflyPosition(const Firefly &fly) const
{
    return {fly.x + std::sin(fly.phase) * 30, fly.y + std::cos(fly.phase * 1.4) * 12};
}

QRect SeasonalWeather::fireflyRect(const Firefly &fly) const
{
    return QRectF(fireflyPosition(fly) - QPointF(glowRadius, glowRadius),
                  QSizeF(glowRadius * 2, glowRadius * 2)).toAlignedRect();
}

double SeasonalWeather::groundAt(double x) const
{
    return m_scenery.groundAt(x);
}

void SeasonalWeather::updateDropBounds(Drop &drop) const
{
    const double vx = m_rainSlant * drop.windFactor;
    const double vy = drop.speed * m_precipitation.speed / 100.0;
    const double scale = drop.length / std::hypot(vx, vy);
    drop.streak = QPoint(qRound(vx * scale), qRound(vy * scale));
    drop.rect = rainBounds(QPoint(qRound(drop.x), qRound(drop.y)), drop.streak);
}

QRect SeasonalWeather::leafRect(const Leaf &leaf)
{
    // Самая дальняя от центра точка — конец черешка (1.28 размера); радиус не зависит от поворота.
    const double radius = leaf.size * 1.3 + 2;
    return QRectF(leaf.x - radius, leaf.y - radius, radius * 2, radius * 2).toAlignedRect();
}

QRect SeasonalWeather::splashRect(const Splash &splash)
{
    return QRectF(splash.origin.x() - 16, splash.origin.y() - 15, 32, 25).toAlignedRect();
}

QRegion SeasonalWeather::advance(double dt, const QRect &hole)
{
    QRegion dirty = std::exchange(m_damage, {});
    if (m_size.isEmpty())
        return dirty;

    m_time += dt;
    const double w = m_size.width();
    const double wind = Precipitation::windAt(m_precipitation.wind * Precipitation::windScale, m_time);
    const double speed = m_precipitation.speed / 100.0;
    const double rain = rainStrength();
    m_rainSlant = wind;
    const double sceneryWind = wind + 14 * std::sin(m_time * .25) + 7 * std::sin(m_time * .73 + 1.3);
    dirty += m_scenery.advance(dt, sceneryWind, rain);
    if (rain > 0) {
        m_afterRain = .85;
    } else if (m_afterRain > 0) {
        const int opacity = qRound(46 * m_afterRain);
        m_afterRain = qMax(0.0, m_afterRain - dt * .095);
        if (qRound(46 * m_afterRain) != opacity)
            dirty += mistRect();
    }

    const QRect viewport(QPoint(0, 0), m_size);
    const bool dense = densePrecipitation();
    Precipitation::DamageTiles tiles(m_size, dense);
    const auto mark = [&](const QRect &damage) {
        const QRect within = damage & viewport;
        if (within.isEmpty() || hole.contains(within))
            return;
        if (dense)
            tiles.mark(within);
        else
            dirty += within;
    };
    const auto markMove = [&](const QRect &before, const QRect &after) {
        if (before.intersects(after)) {
            mark(before.united(after));
        } else {
            mark(before);
            mark(after);
        }
    };

    const auto makeSplash = [&](const Drop &drop, const QPointF &point, bool puddle) {
        if (m_splashes.size() >= maxSplashes || drop.band < 1 || rnd(0, 1) >= .35 + rain * .28)
            return;
        const bool ring = puddle && m_scenery.inPuddle(point.x());
        m_splashes.push_back({point - QPointF(0, ring ? 4 : rnd(0, 2)), 0,
                              rnd(14, 30), rnd(55, 95), ring});
    };
    for (Drop &drop : m_drops) {
        const QRect before = drop.rect;
        const QPointF from(drop.x, drop.y);
        const bool wrapped = Precipitation::fall(drop, dt, speed, m_rainSlant, w, dropMargin);
        const QPointF to(drop.x, drop.y);
        if (!wrapped) {
            for (const WindowLedge &window : m_windows) {
                const auto point = Precipitation::crossing(from, to, window.top);
                if (point && window.exposed.contains(QPoint(qFloor(point->x()), window.top.y() - 1)))
                    makeSplash(drop, *point, false);
            }
            if (const auto point = groundContact(from, to)) {
                makeSplash(drop, *point, true);
                respawn(drop);
            }
        }
        if (drop.y > m_size.height() + drop.length)
            respawn(drop);
        updateDropBounds(drop);
        markMove(before, drop.rect);
    }

    for (Splash &splash : m_splashes) {
        splash.age += dt;
        mark(splashRect(splash));   // и кадр, в котором всплеск исчезает: его надо стереть
    }
    m_splashes.erase(std::remove_if(m_splashes.begin(), m_splashes.end(),
                                    [](const Splash &splash) { return splash.age >= splashLife; }),
                     m_splashes.end());

    for (Leaf &leaf : m_leaves) {
        const QRect before = leaf.rect;
        const double lowerEdge = leaf.size * .36;
        const QPointF from(leaf.x, leaf.y + lowerEdge);
        const bool wrapped = Precipitation::fall(leaf, dt, speed, wind * (1 - rain * .48), w, leafMargin);
        const QPointF to(leaf.x, leaf.y + lowerEdge);
        leaf.angle += leaf.spin * dt;
        leaf.flip += leaf.flipSpeed * dt;

        if (!wrapped) {
            for (WindowLedge &window : m_windows) {
                if (const auto point = Precipitation::crossing(from, to, window.top)) {
                    Leaf resting = leaf;
                    resting.x = point->x();
                    settle(resting, window.leaves, point->y(), maxGroundLeaves / 2, false);
                }
            }
            if (const auto point = groundContact(from, to)) {
                Leaf resting = leaf;
                resting.x = point->x();
                settle(resting);
                respawn(leaf);
            }
        }
        if (leaf.y > m_size.height() + leaf.size)
            respawn(leaf);
        leaf.rect = leafRect(leaf);
        markMove(before, leaf.rect);
    }
    if (dense)
        dirty += tiles.region();
    dirty += advanceGround(dt, wind);
    dirty += advanceExtras(dt);
    dirty += std::exchange(m_damage, {});
    return dirty & viewport;
}

QRegion SeasonalWeather::overlapping(const QRegion &area) const
{
    QRegion result = extraVisualRegion() & area;
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

void SeasonalWeather::paint(QPainter &p, const QRegion &clip) const
{
    const QRect bounds = clip.boundingRect();
    const bool dense = densePrecipitation();
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    m_scenery.paint(p, clip);

    // --- Дождь -------------------------------------------------------------
    if (!m_drops.empty()) {
        for (const Drop &drop : m_drops) {
            if (!drop.rect.intersects(bounds) || (!dense && !clip.intersects(drop.rect)))
                continue;
            p.drawImage(drop.rect.topLeft(), rainSprite(drop.band, drop.streak));
        }
    }

    // --- Всплески: дуга-«корона» и две капельки ---------------------------------
    for (const Splash &splash : m_splashes) {
        const QRect rect = splashRect(splash);
        if (!rect.intersects(bounds) || !clip.intersects(rect))
            continue;
        const double t = qBound(0.0, splash.age / splashLife, 1.0);
        const QColor color(rainColorR, rainColorG, rainColorB, qRound(150 * (1 - t)));

        if (splash.ring) {
            p.setPen(QPen(color, .8));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(splash.origin, 2 + 12 * t, .7 + 2.9 * t);
            continue;
        }
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

    for (const Leaf &leaf : m_groundLeaves) {
        if (clip.intersects(leaf.rect))
            paintLeaf(p, leaf, true);
    }
    for (const Leaf &leaf : m_leaves) {
        if (bounds.intersects(leaf.rect) && (dense || clip.intersects(leaf.rect)))
            paintLeaf(p, leaf, false);
    }
    paintExtras(p, clip);

    p.restore();
}

namespace {
QRect around(const QPointF &p, double r)
{
    return QRectF(p - QPointF(r, r), QSizeF(2 * r, 2 * r)).toAlignedRect();
}

double distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double square = QPointF::dotProduct(ab, ab);
    const double t = square > 0 ? qBound(0.0, QPointF::dotProduct(p - a, ab) / square, 1.0) : 0;
    return QLineF(p, a + ab * t).length();
}

std::optional<QPointF> edgeHit(const QLineF &path, const QRectF &frame)
{
    if (!frame.intersects(QRectF(path.p1(), path.p2()).normalized().adjusted(-.5, -.5, .5, .5)))
        return {};
    const std::array<QLineF, 4> edges{{
        {frame.topLeft(), frame.topRight()}, {frame.topRight(), frame.bottomRight()},
        {frame.bottomRight(), frame.bottomLeft()}, {frame.bottomLeft(), frame.topLeft()},
    }};
    std::optional<QPointF> nearest;
    for (const QLineF &edge : edges) {
        QPointF p;
        if (path.intersects(edge, &p) == QLineF::BoundedIntersection &&
            (!nearest || QLineF(path.p1(), p).length() < QLineF(path.p1(), *nearest).length()))
            nearest = p;
    }
    return nearest;
}
}

void SeasonalWeather::setWindows(const QList<SnowSurface> &windows)
{
    if (windows == m_windowLayout)
        return;
    m_windowLayout = windows;
    std::vector<WindowLedge> next;
    next.reserve(windows.size());
    QRegion occlusion;
    for (auto it = windows.crbegin(); it != windows.crend(); ++it) {
        const QRect frame = it->rect;
        const QRect top = frame.top() >= 0 && !it->fullscreen && !it->maximized && !it->panelPopup
            ? QRect(frame.x(), qMax(5, frame.y()), frame.width(), 1) : QRect();
        const QRegion exposed = top.isEmpty() ? QRegion() : QRegion(top.adjusted(-20, -36, 20, 12)) - occlusion;
        WindowLedge ledge{*it, top, exposed, {}};
        const auto old = std::find_if(m_windows.begin(), m_windows.end(), [&](const WindowLedge &window) {
            return window.surface.id == it->id;
        });
        if (old != m_windows.end()) {
            if (!old->leaves.empty()) {
                if (old->top != top)
                    m_damage += old->exposed + exposed;
                else if (old->exposed != exposed)
                    m_damage += old->exposed.xored(exposed);
                if (top.isEmpty() || old->top.width() != top.width()) {
                    disperseWindowLeaves(*old);
                } else {
                    ledge.leaves = std::move(old->leaves);
                    const QPoint motion = top.topLeft() - old->top.topLeft();
                    if (!motion.isNull()) {
                        for (Leaf &leaf : ledge.leaves) {
                            leaf.x += motion.x();
                            leaf.y += motion.y();
                            leaf.rect.translate(motion);
                        }
                    }
                }
            }
            old->leaves.clear();
        }
        next.push_back(std::move(ledge));
        occlusion += frame;
    }
    for (const WindowLedge &old : m_windows) {
        if (old.leaves.empty())
            continue;
        m_damage += old.exposed;
        disperseWindowLeaves(old);
    }
    std::reverse(next.begin(), next.end());
    m_windows = std::move(next);
}

void SeasonalWeather::disperseWindowLeaves(const WindowLedge &window)
{
    for (const Leaf &leaf : window.leaves) {
        if (m_scattered.size() >= maxScatteredLeaves)
            break;
        m_scattered.push_back({leaf, QPointF(rnd(-28, 28), rnd(24, 58)), 8, false});
    }
}

std::optional<QPointF> SeasonalWeather::groundContact(const QPointF &from, const QPointF &to) const
{
    if (const auto dock = Precipitation::crossing(from, to, m_dock))
        return dock;
    return Precipitation::crossing(from, to, QRect(0, m_size.height(), m_size.width(), 1));
}

void SeasonalWeather::paintWindowLeaves(QPainter &p, const QRegion &clip) const
{
    for (const WindowLedge &window : m_windows) {
        if (window.leaves.empty())
            continue;
        const QRegion visible = window.exposed & clip;
        if (visible.isEmpty())
            continue;
        p.save();
        p.setClipRegion(visible, Qt::IntersectClip);
        for (const Leaf &leaf : window.leaves) {
            if (visible.intersects(leaf.rect))
                paintLeaf(p, leaf, true);
        }
        p.restore();
    }
}

void SeasonalWeather::paintLeafOverlays(QPainter &p, const QRegion &clip) const
{
    paintWindowLeaves(p, clip);
    for (const GroundPile &pile : m_groundPiles) {
        if (!clip.intersects(pile.bounds))
            continue;
        const bool petals = m_season == Season::Spring;
        const double width = 11 + pile.leaves.size() * .78;
        const double height = 4 + pile.leaves.size() * .84;
        QPainterPath dome;
        dome.moveTo(pile.base.x() - width, pile.base.y());
        dome.cubicTo(pile.base + QPointF(-width * .65, -height), pile.base + QPointF(width * .47, -height),
                     pile.base + QPointF(width, 0));
        dome.closeSubpath();
        QLinearGradient shade(pile.base - QPointF(0, height), pile.base);
        shade.setColorAt(0, petals ? QColor(179, 106, 135, 206) : QColor(172, 91, 47, 215));
        shade.setColorAt(1, petals ? QColor(124, 68, 94, 221) : QColor(104, 57, 32, 225));
        p.fillPath(dome, shade);
        for (const Leaf &leaf : pile.leaves)
            paintLeaf(p, leaf, true);
    }
    for (const ScatteredLeaf &particle : m_scattered) {
        if (clip.intersects(particle.leaf.rect))
            paintLeaf(p, particle.leaf, false);
    }
}

void SeasonalWeather::setBubbles(bool enabled)
{
    if (m_bubblesEnabled == enabled)
        return;
    m_bubblesEnabled = enabled;
    if (!enabled) {
        for (const Bubble &bubble : m_bubbles)
            m_damage += around(bubble.pos, bubble.radius + 3);
        m_bubbles.clear();
    }
    m_bubbleDelay = 0;
}

QPointF SeasonalWeather::launcher() const
{
    const double x = qMin(54, m_size.width() / 2);
    return {x, groundAt(x) - 29};
}

QPointF SeasonalWeather::jarPosition() const
{
    const double x = qMin(34, m_size.width() / 2);
    return {x, groundAt(x) - 23};
}

QRect SeasonalWeather::mistRect() const
{
    return {0, m_size.height() - 70, m_size.width(), 64};
}

void SeasonalWeather::setJar(bool enabled)
{
    if (m_season != Season::Summer || m_jar == enabled)
        return;
    m_damage += around(jarPosition(), 21);
    if (!enabled && m_scenery.night()) {
        for (Firefly &fly : m_fireflies) {
            if (m_caught <= 0)
                break;
            if (fly.boost >= 0)
                continue;
            fly.x = jarPosition().x() + rnd(-9, 9);
            fly.y = jarPosition().y() - 8;
            fly.impulse = QPointF(rnd(8, 38), rnd(-68, -38));
            fly.boost = 1;
            --m_caught;
        }
    }
    m_jar = enabled;
    m_caught = enabled ? m_caught : 0;
}

void SeasonalWeather::startBlower()
{
    if (m_season == Season::Autumn && !m_blower)
        m_blower = -37;
}

bool SeasonalWeather::launchBoat()
{
    return m_scenery.launchBoat();
}

void SeasonalWeather::settle(Leaf leaf)
{
    const double ground = groundAt(leaf.x);
    const bool floats = m_season == Season::Spring && m_scenery.inPuddle(leaf.x);
    if (!floats) {
        const int bin = qFloor(leaf.x / pileSpacing);
        const auto pileAt = [&](const GroundPile &pile) {
            return qFloor(pile.base.x() / pileSpacing) == bin && pile.base.y() == ground;
        };
        const auto neighbors = [&](const Leaf &other) {
            return qFloor(other.x / pileSpacing) == bin && groundAt(other.x) == ground &&
                   !(m_season == Season::Spring && m_scenery.inPuddle(other.x));
        };
        auto pile = std::find_if(m_groundPiles.begin(), m_groundPiles.end(), pileAt);
        if (pile != m_groundPiles.end()) {
            if (pile->leaves.size() < maxPileLeaves) {
                pile->leaves.push_back(leaf);
                layoutPile(*pile);
            }
            return;
        }
        if (std::count_if(m_groundLeaves.cbegin(), m_groundLeaves.cend(), neighbors) >= pileThreshold - 1) {
            if (m_groundPiles.size() >= maxGroundPiles) {
                m_damage += m_groundPiles.front().bounds;
                m_groundPiles.erase(m_groundPiles.begin());
            }
            const double center = (bin + .5) * pileSpacing + (bin % 3 - 1) * 4;
            m_groundPiles.push_back({QPointF(groundAt(center) == ground ? center : leaf.x, ground), {}, {}});
            GroundPile &formed = m_groundPiles.back();
            for (auto it = m_groundLeaves.begin(); it != m_groundLeaves.end();) {
                if (neighbors(*it)) {
                    m_damage += it->rect;
                    if (formed.leaves.size() < maxPileLeaves - 1)
                        formed.leaves.push_back(*it);
                    it = m_groundLeaves.erase(it);
                } else {
                    ++it;
                }
            }
            formed.leaves.push_back(leaf);
            layoutPile(formed);
            return;
        }
    }
    if (m_groundLeaves.size() >= maxGroundLeaves) {
        m_damage += m_groundLeaves.front().rect;
        m_groundLeaves.erase(m_groundLeaves.begin());
    }
    leaf.y = ground - (floats ? 2.6 : qMax(1.6, leaf.size * .23));
    leaf.rect = leafRect(leaf);
    m_damage += leaf.rect;
    m_groundLeaves.push_back(leaf);
}

void SeasonalWeather::layoutPile(GroundPile &pile)
{
    m_damage += pile.bounds;
    pile.bounds = {};
    const double height = qMin(24.0, 5 + pile.leaves.size() * 1.25);
    const double width = 10 + pile.leaves.size() * .76;
    for (std::size_t i = 0; i < pile.leaves.size(); ++i) {
        Leaf &leaf = pile.leaves[i];
        // Разные уровни и округлый гребень. Уже лежащие листья сохраняют своё место в куче.
        const double u = 2 * std::fmod(i * .61803398875 + .50, 1.0) - 1;
        const double x = qBound(0.0, pile.base.x() + width * u, qMax(0, m_size.width() - 1) * 1.0);
        leaf.x = groundAt(x) == pile.base.y() ? x : pile.base.x();
        const double tier = .27 + .73 * std::fmod(i * .371 + .98, 1.0);
        leaf.y = pile.base.y() - qMax(leaf.size * .23, height * std::sqrt(1 - u * u) * tier);
        leaf.rect = leafRect(leaf);
        pile.bounds |= leaf.rect;
    }
    m_damage += pile.bounds;
}

void SeasonalWeather::burstPile(GroundPile &pile, bool clear)
{
    m_damage += pile.bounds;
    const std::size_t overflow = m_scattered.size() + pile.leaves.size();
    if (overflow > maxScatteredLeaves) {
        const auto end = m_scattered.begin() + (overflow - maxScatteredLeaves);
        for (auto it = m_scattered.begin(); it != end; ++it)
            m_damage += it->leaf.rect;
        m_scattered.erase(m_scattered.begin(), end);
    }
    for (const Leaf &leaf : pile.leaves) {
        const double dx = leaf.x - pile.base.x();
        const QPointF velocity(dx * 4.7 + rnd(-86, 86), -rnd(108, 188));
        m_scattered.push_back({leaf, velocity, rnd(1.15, 1.8), clear});
    }
}

bool SeasonalWeather::breakPile(const QPointF &position)
{
    const auto pile = std::find_if(m_groundPiles.begin(), m_groundPiles.end(), [&](const GroundPile &candidate) {
        return candidate.bounds.contains(position.toPoint()) && position.y() <= candidate.base.y();
    });
    if (pile == m_groundPiles.end())
        return false;
    burstPile(*pile, false);
    m_groundPiles.erase(pile);
    return true;
}

void SeasonalWeather::clearPilesAlong(const QPointF &from, const QPointF &to, double radius)
{
    for (auto it = m_groundPiles.begin(); it != m_groundPiles.end();) {
        if (distanceToSegment(it->base - QPointF(0, 9), from, to) <= radius) {
            burstPile(*it, true);
            it = m_groundPiles.erase(it);
        } else {
            ++it;
        }
    }
}

void SeasonalWeather::settle(Leaf leaf, std::vector<Leaf> &pile, double ground, std::size_t limit, bool floats)
{
    if (pile.size() >= limit) {
        m_damage += pile.front().rect;
        pile.erase(pile.begin());
    }
    const double bin = std::floor(leaf.x / 34);
    int count = 0;
    auto oldest = pile.end();
    for (auto it = pile.begin(); it != pile.end(); ++it) {
        if (std::floor(it->x / 34) != bin)
            continue;
        if (oldest == pile.end())
            oldest = it;
        ++count;
    }
    if (count >= 8) {
        m_damage += oldest->rect;
        pile.erase(oldest);
    }
    const double pileHeight = floats ? 2.6 : qMin(10.0, count * .95 + (leaf.wet ? 0 : rnd(0, 2)));
    leaf.y = ground - qMax(1.6, leaf.size * .23) - pileHeight;
    leaf.rect = leafRect(leaf);
    m_damage += leaf.rect;
    pile.push_back(leaf);
}

bool SeasonalWeather::scatterNear(std::vector<Leaf> &leaves, const QPointF &from, const QPointF &to,
                                  double radius, double push, bool clear, const QRegion *exposed)
{
    bool scattered = false;
    const double direction = to.x() >= from.x() ? 1 : -1;
    for (auto it = leaves.begin(); it != leaves.end() && m_scattered.size() < maxScatteredLeaves;) {
        const QPointF center(it->x, it->y);
        if ((exposed && !exposed->intersects(it->rect)) ||
            distanceToSegment(center, from, to) > radius || (!exposed && it->wet && push < 72)) {
            ++it;
            continue;
        }
        const double vx = direction * (push + rnd(0, 78)) + rnd(-46, 46);
        const double vy = -rnd(32, it->wet ? 72 : 135);
        m_scattered.push_back({*it, QPointF(vx, vy), rnd(.75, 1.25), clear});
        m_damage += it->rect;
        it = leaves.erase(it);
        scattered = true;
    }
    return scattered;
}

void SeasonalWeather::applyTool(const QPointF &position)
{
    if (std::abs(groundAt(position.x()) - position.y()) > 60 || m_dock.contains(position.toPoint()))
        return;
    if (m_tool == SeasonTool::Broom) {
        const QPointF from = position - QPointF(20, 7);
        scatterNear(m_groundLeaves, from, position, 50, 140, true);
        clearPilesAlong(from, position, 50);
    } else if (m_tool == SeasonTool::Rake) {
        std::vector<Leaf> gathered;
        for (auto it = m_groundLeaves.begin(); it != m_groundLeaves.end() && gathered.size() < 10;) {
            if (std::abs(it->x - position.x()) > 67 || std::abs(it->x - position.x()) < 13) {
                ++it;
                continue;
            }
            m_damage += it->rect;
            gathered.push_back(*it);
            it = m_groundLeaves.erase(it);
        }
        for (Leaf &leaf : gathered) {
            leaf.x = qBound(1.0, position.x() + rnd(-10, 10), qMax(1.0, m_size.width() - 1.0));
            settle(leaf);
        }
    } else if (m_tool == SeasonTool::Plant) {
        m_scenery.plant(position.x());
    } else if (m_tool == SeasonTool::Water) {
        m_scenery.water(position.x());
    }
}

bool SeasonalWeather::press(const QPointF &position)
{
    if (m_tool == SeasonTool::Acorn) {
        m_flight = Flight{launcher(), position, launcher()};
        m_tool = SeasonTool::Passive;
        return true;
    }
    if (m_tool != SeasonTool::Passive) {
        applyTool(position);
        return true;
    }
    for (auto it = m_bubbles.rbegin(); it != m_bubbles.rend(); ++it) {
        if (QLineF(it->pos, position).length() <= it->radius) {
            m_damage += around(it->pos, it->radius + 2);
            if (m_impacts.size() < 24)
                m_impacts.push_back({it->pos, 0, true});
            m_bubbles.erase(std::next(it).base());
            return true;
        }
    }
    for (Firefly &fly : m_fireflies) {
        if (fly.boost >= 0 && QLineF(fireflyPosition(fly), position).length() <= 10) {
            if (m_jar && m_caught < 5) {
                m_damage += fireflyRect(fly);
                fly.boost = -1;
                ++m_caught;
            } else {
                fly.boost = 1;
            }
            return true;
        }
    }
    for (const Leaf &leaf : m_groundLeaves) {
        if (QLineF(QPointF(leaf.x, leaf.y), position).length() <= qMax(5.0, leaf.size * .75))
            return scatterNear(m_groundLeaves, position - QPointF(1, 0), position, 34, 126);
    }
    return false;
}

void SeasonalWeather::movePointer(const QPointF &position, bool held)
{
    if (held && m_tool != SeasonTool::Passive && m_tool != SeasonTool::Acorn)
        applyTool(position);
    if (!m_pointer) {
        m_pointer = position;
        return;
    }
    const QPointF from = *m_pointer;
    m_pointer = position;
    const QPointF motion = position - from;
    const double speed = std::hypot(motion.x(), motion.y());
    if (speed < 3 || speed > 245)
        return;
    m_scenery.steerInsects(position, motion);
    if (m_tool == SeasonTool::Passive) {
        const double force = qBound(30.0, speed * 3.4, 190.0);
        if (qMin(std::abs(groundAt(from.x()) - from.y()), std::abs(groundAt(position.x()) - position.y())) < 65)
            scatterNear(m_groundLeaves, from, position, 24, force);
        const QRect sweep = QRectF(from, position).normalized().adjusted(-22, -22, 22, 22).toAlignedRect();
        for (auto window = m_windows.rbegin(); window != m_windows.rend(); ++window) {
            if (window->exposed.intersects(sweep) &&
                scatterNear(window->leaves, from, position, 22, force, false, &window->exposed))
                break;
        }
        for (Leaf &leaf : m_leaves) {
            if (distanceToSegment(QPointF(leaf.x, leaf.y), from, position) < 37) {
                leaf.baseX += qBound(-19.0, motion.x() * .48, 19.0) * (leaf.wet ? .4 : 1);
                leaf.phase += .24;
            }
        }
    }
    if (speed > 10)
        for (Firefly &fly : m_fireflies) {
            const QPointF away = fireflyPosition(fly) - position;
            if (QLineF(QPointF(), away).length() < 88)
                fly.impulse += away * 2.7 + motion * .7;
        }
}

QRegion SeasonalWeather::inputRegion() const
{
    const QRect viewport(QPoint(0, 0), m_size);
    if (m_tool == SeasonTool::Acorn)
        return QRegion(viewport);
    QRegion input;
    if (m_tool != SeasonTool::Passive) {
        input += QRect(0, m_size.height() - 60, m_size.width(), 60);
        if (!m_dock.isEmpty())
            input += QRect(m_dock.left(), m_dock.top() - 60, m_dock.width(), 60);
    }
    for (const Leaf &leaf : m_groundLeaves)
        input += QRegion(around(QPointF(leaf.x, leaf.y), qMax(5.0, leaf.size * .75)), QRegion::Ellipse);
    input += pileInputRegion();
    for (const Bubble &bubble : m_bubbles)
        input += QRegion(around(bubble.pos, bubble.radius), QRegion::Ellipse);
    for (const Firefly &fly : m_fireflies)
        if (fly.boost >= 0)
            input += QRegion(around(fireflyPosition(fly), 10), QRegion::Ellipse);
    return (input & viewport) - m_dock;
}

QRegion SeasonalWeather::pileInputRegion() const
{
    QRegion input;
    for (const GroundPile &pile : m_groundPiles)
        input += pile.bounds & QRect(0, 0, m_size.width(), qCeil(pile.base.y()));
    return input - m_dock;
}

QRegion SeasonalWeather::advanceGround(double dt, double wind)
{
    QRegion dirty;
    if (m_blower) {
        const double x = *m_blower;
        dirty += around(QPointF(x, groundAt(x) - 15), 44);
        *m_blower += dt * 160;
        const double y = groundAt(*m_blower);
        const QPointF from(x, y - 7);
        const QPointF to(*m_blower + 26, y - 7);
        scatterNear(m_groundLeaves, from, to, 38, 200, true);
        clearPilesAlong(from, to, 38);
        dirty += around(QPointF(*m_blower, y - 15), 44);
        if (*m_blower > m_size.width() + 47)
            m_blower.reset();
    }

    for (Leaf &leaf : m_groundLeaves) {
        const bool floating = m_season == Season::Spring && m_scenery.inPuddle(leaf.x);
        const double threshold = leaf.wet ? 120 : leaf.dry ? 27 : 58;
        if (!floating && std::abs(wind) < threshold)
            continue;
        dirty += leaf.rect;
        const double depth = groundAt(leaf.x) - leaf.y;
        const double drift = wind * (floating ? .065 : leaf.dry ? .63 : .26);
        leaf.x += drift * dt;
        leaf.y = groundAt(leaf.x) - (floating ? qMax(2.7, leaf.size * .23 + 2) : depth);
        leaf.angle += drift * dt * .05;
        leaf.rect = leafRect(leaf);
        dirty += leaf.rect;
    }
    m_groundLeaves.erase(std::remove_if(m_groundLeaves.begin(), m_groundLeaves.end(), [this](const Leaf &leaf) {
        return leaf.x < -15 || leaf.x > m_size.width() + 15;
    }), m_groundLeaves.end());

    for (auto it = m_scattered.begin(); it != m_scattered.end();) {
        Leaf &leaf = it->leaf;
        dirty += leaf.rect;
        leaf.x += it->velocity.x() * dt;
        leaf.y += it->velocity.y() * dt;
        leaf.angle += dt * it->velocity.x() * .047;
        it->velocity.ry() += 245 * dt;
        it->velocity.rx() *= std::exp(-dt * 1.42);
        it->life -= dt;
        const bool landed = leaf.y >= groundAt(leaf.x) - leaf.size * .24 && it->velocity.y() > 0;
        if (!it->clear && landed && leaf.x >= 0 && leaf.x < m_size.width())
            settle(leaf);
        if (landed || it->life <= 0 || leaf.x < -25 || leaf.x > m_size.width() + 25) {
            it = m_scattered.erase(it);
        } else {
            if (it->clear)
                leaf.opacity = qMin(leaf.opacity, it->life * 2);
            leaf.rect = leafRect(leaf);
            dirty += leaf.rect;
            ++it;
        }
    }
    return dirty;
}

QRegion SeasonalWeather::extraVisualRegion() const
{
    QRegion ahead = m_scenery.visualRegion();
    for (const Leaf &leaf : m_groundLeaves)
        ahead += leaf.rect;
    for (const GroundPile &pile : m_groundPiles)
        ahead += pile.bounds;
    for (const ScatteredLeaf &leaf : m_scattered)
        ahead += leaf.leaf.rect;
    for (const Firefly &fly : m_fireflies)
        if (fly.boost >= 0)
            ahead += fireflyRect(fly);
    for (const Bubble &bubble : m_bubbles)
        ahead += around(bubble.pos, bubble.radius + 3);
    for (const Impact &impact : m_impacts)
        ahead += around(impact.pos, 36);
    if (m_flight)
        ahead += around(m_flight->pos, 11);
    if (m_blower)
        ahead += around(QPointF(*m_blower, groundAt(*m_blower) - 15), 44);
    if (m_season == Season::Autumn)
        ahead += around(launcher(), 18);
    if (m_jar)
        ahead += around(jarPosition(), 21);
    if (m_afterRain > 0 && rainStrength() <= 0)
        ahead += mistRect();
    return ahead;
}

QRegion SeasonalWeather::advanceExtras(double dt)
{
    QRegion dirty;
    if (m_season == Season::Summer) {
        if (m_scenery.night() && m_fireflies.empty()) {
            populateFireflies();
        } else if (!m_scenery.night() && !m_fireflies.empty()) {
            for (const Firefly &fly : m_fireflies)
                dirty += fireflyRect(fly);
            m_fireflies.clear();
            m_caught = 0;
            if (m_jar)
                dirty += around(jarPosition(), 21);
        }
        for (Firefly &fly : m_fireflies) {
            if (fly.boost < 0)
                continue;
            dirty += fireflyRect(fly);
            const QPointF pos = fireflyPosition(fly);
            const double pause = .24 + .76 * std::pow(std::sin(fly.phase * .52), 2);
            fly.phase += dt * .71;
            fly.x += (std::sin(fly.phase * .76) * fly.speed * pause + fly.impulse.x()) * dt;
            fly.y += (std::cos(fly.phase * .83) * fly.speed * .67 * pause + fly.impulse.y()) * dt;
            fly.impulse *= std::exp(-dt * 3.2);
            if (m_pointer) {
                const QPointF delta = *m_pointer - pos;
                const double distance = QLineF(QPointF(), delta).length();
                if (distance > 25 && distance < 145 && QLineF(QPointF(), fly.impulse).length() < 25) {
                    fly.x += delta.x() * dt * .18;
                    fly.y += delta.y() * dt * .18;
                }
            }
            fly.x = qBound(5.0, fly.x, qMax(5.0, m_size.width() - 5.0));
            fly.y = qBound(15.0, fly.y, qMax(15.0, groundAt(fly.x) - 12));
            fly.boost = qMax(0.0, fly.boost - dt * .8);
            dirty += fireflyRect(fly);
        }
        if (m_jar && m_caught)
            dirty += around(jarPosition(), 21);

        if (m_bubblesEnabled && (m_bubbleDelay -= dt) <= 0 && m_bubbles.size() < 14) {
            const double x = rnd(15, qMax(16.0, m_size.width() - 15.0));
            const double radius = rnd(9, 24);
            m_bubbles.push_back({QPointF(x, groundAt(x) + radius + 2), radius, rnd(41, 84), rnd(0, twoPi)});
            m_bubbleDelay = rnd(.44, .92);
        }
        for (auto it = m_bubbles.begin(); it != m_bubbles.end();) {
            Bubble &bubble = *it;
            const QPointF before = bubble.pos;
            dirty += around(before, bubble.radius + 3);
            bubble.pos += QPointF(18 * std::sin(bubble.phase) * dt, -bubble.speed * dt);
            bubble.phase += dt * 1.2;
            std::optional<QPointF> hit;
            for (const WindowLedge &window : m_windows) {
                const QRectF edge = QRectF(window.surface.rect).adjusted(-bubble.radius, -bubble.radius,
                                                            bubble.radius, bubble.radius);
                hit = edgeHit(QLineF(before, bubble.pos), edge);
                if (hit)
                    break;
            }
            if (hit || bubble.pos.y() < -bubble.radius - 2) {
                if (hit && m_impacts.size() < 24)
                    m_impacts.push_back({*hit, 0, true});
                it = m_bubbles.erase(it);
            } else {
                dirty += around(bubble.pos, bubble.radius + 3);
                ++it;
            }
        }
    }

    if (m_flight) {
        Flight &flight = *m_flight;
        const QPointF previous = flight.pos;
        dirty += around(previous, 11);
        const double distance = QLineF(flight.start, flight.target).length();
        const double t = qMin(1.0, (flight.age += dt) / qBound(.29, distance / 840, .85));
        flight.pos = flight.start + (flight.target - flight.start) * t;
        flight.pos.ry() -= 4 * qMin(100.0, 18 + distance * .10) * t * (1 - t);
        std::optional<QPointF> hit;
        if (t > .12) {
            for (const WindowLedge &window : m_windows) {
                const auto impact = edgeHit(QLineF(previous, flight.pos), QRectF(window.surface.rect));
                if (impact && (!hit || QLineF(previous, *impact).length() < QLineF(previous, *hit).length()))
                    hit = impact;
            }
        }
        if (hit || t >= 1) {
            if (m_impacts.size() < 24)
                m_impacts.push_back({hit.value_or(flight.pos), 0, false});
            m_flight.reset();
        } else {
            dirty += around(flight.pos, 11);
        }
    }
    for (Impact &impact : m_impacts) {
        impact.age += dt;
        dirty += around(impact.pos, 36);
    }
    m_impacts.erase(std::remove_if(m_impacts.begin(), m_impacts.end(),
                                   [](const Impact &impact) { return impact.age >= .42; }), m_impacts.end());
    return dirty;
}

void SeasonalWeather::paintLeaf(QPainter &p, const Leaf &leaf, bool onGround) const
{
    const bool spring = m_season == Season::Spring;
    const double turn = onGround ? .84 : std::cos(leaf.flip);
    const double width = leaf.size * qMax(.16, std::abs(turn));
    const QImage &sprite = leafSprite(spring, leaf.shape, leaf.color, turn < 0 || leaf.wet);
    p.save();
    p.setOpacity(leaf.opacity);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(leaf.x, leaf.y);
    p.rotate((leaf.angle + .55 * std::sin(leaf.phase)) * 180.0 / pi);
    p.scale(width, leaf.size * (onGround ? .64 : 1));
    p.drawImage(QRectF(-1.4, -1.4, 2.8, 2.8), sprite);
    p.restore();
}

void SeasonalWeather::paintExtras(QPainter &p, const QRegion &clip) const
{
    p.save();
    if (m_afterRain > 0 && rainStrength() <= 0) {
        const QRect mist = mistRect();
        if (clip.intersects(mist)) {
            QLinearGradient fade(0, mist.top(), 0, mist.bottom());
            fade.setColorAt(0, QColor(183, 204, 215, 0));
            fade.setColorAt(.65, QColor(183, 204, 215, qRound(46 * m_afterRain)));
            fade.setColorAt(1, QColor(183, 204, 215, 0));
            p.fillRect(mist, fade);
        }
    }
    if (m_season == Season::Autumn && clip.intersects(around(launcher(), 18))) {
        const QPointF a = launcher();
        p.setPen(QPen(QColor(122, 77, 49), 4, Qt::SolidLine, Qt::RoundCap));
        p.drawPolyline(QPolygonF{a + QPointF(-10, -10), a, a + QPointF(10, -10)});
        p.drawLine(a, a + QPointF(0, 14));
        p.setPen(QPen(QColor(194, 143, 74), 1.5));
        p.drawPolyline(QPolygonF{a + QPointF(-10, -10), a + QPointF(0, -6), a + QPointF(10, -10)});
    }
    if (m_blower) {
        const QPointF b(*m_blower, groundAt(*m_blower) - 15);
        p.setPen(QPen(QColor(90, 47, 42), 1.2));
        p.setBrush(QColor(229, 120, 58));
        p.drawRoundedRect(QRectF(b - QPointF(21, 12), QSizeF(29, 22)), 5, 5);
        p.setBrush(QColor(79, 77, 83));
        p.drawPolygon(QPolygonF{b + QPointF(1, -6), b + QPointF(34, 1),
                                 b + QPointF(34, 7), b + QPointF(2, 6)});
        p.drawEllipse(b + QPointF(-14, 9), 5, 5);
        p.drawEllipse(b + QPointF(3, 9), 5, 5);
        p.setPen(QPen(QColor(241, 209, 156, 145), 1));
        for (int i = 0; i < 3; ++i)
            p.drawLine(b + QPointF(35, i * 4 - 1), b + QPointF(42, i * 5 - 4));
    }
    if (m_flight && clip.intersects(around(m_flight->pos, 11))) {
        p.save();
        p.translate(m_flight->pos);
        p.rotate(m_flight->age * 480);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(181, 110, 56));
        p.drawEllipse(QPointF(0, 2), 4, 6);
        p.setBrush(QColor(100, 66, 43));
        p.drawEllipse(QPointF(0, -2), 5, 3.5);
        p.drawLine(QPointF(1, -5), QPointF(1, -7));
        p.restore();
    }
    const QRegion lensScene = m_bubbles.empty() ? QRegion() : m_scenery.visualRegion();
    for (const Bubble &bubble : m_bubbles) {
        if (!clip.intersects(around(bubble.pos, bubble.radius + 3)))
            continue;
        const double r = bubble.radius;
        if (lensScene.intersects(around(bubble.pos, r))) {
            p.save();
            QPainterPath lens;
            lens.addEllipse(bubble.pos, r - 1, r - 1);
            p.setClipPath(lens, Qt::IntersectClip);
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.fillRect(around(bubble.pos, r), Qt::transparent);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.translate(bubble.pos);
            p.scale(1.16, 1.21);
            p.translate(-bubble.pos + QPointF(1.6, -1.3));
            m_scenery.paint(p, QRegion(around(bubble.pos, r + 2)));
            p.restore();
        }
        QRadialGradient film(bubble.pos, r, bubble.pos - QPointF(r * .22, r * .28));
        film.setColorAt(0, QColor(218, 249, 255, 3));
        film.setColorAt(.72, QColor(179, 225, 255, 14));
        film.setColorAt(.94, QColor(137, 221, 251, 94));
        film.setColorAt(1, QColor(236, 199, 249, 124));
        p.setPen(QPen(QColor(194, 229, 255, 142), .8));
        p.setBrush(film);
        p.drawEllipse(bubble.pos, r, r);
        const QRectF ellipse(bubble.pos - QPointF(r * .86, r * .86), QSizeF(r * 1.72, r * 1.72));
        p.setPen(QPen(QColor(255, 250, 253, 206), 1.25, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(ellipse, 78 * 16, 69 * 16);
        p.setPen(QPen(QColor(255, 163, 222, 160), .9, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(ellipse, 225 * 16, 79 * 16);
    }

    for (const Impact &impact : m_impacts) {
        if (!clip.intersects(around(impact.pos, 36)))
            continue;
        const double t = impact.age / .42;
        const double r = 3 + (impact.bubble ? 17 : 22) * t;
        QColor color = impact.bubble ? QColor(198, 238, 255) : QColor(197, 133, 66);
        color.setAlpha(qRound(218 * (1 - t)));
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 8; ++i) {
            const double phase = twoPi * i / 8;
            const QPointF part = impact.pos + QPointF(std::sin(phase) * r, std::cos(phase) * r + 8 * t * t);
            p.drawEllipse(part, impact.bubble ? 1.35 : 1.9, 1.3);
        }
    }

    for (const Firefly &fly : m_fireflies) {
        if (fly.boost < 0 || !clip.intersects(fireflyRect(fly)))
            continue;
        const QPointF position = fireflyPosition(fly);
        const double pulse = .18 + .67 * std::pow(.5 + .5 * std::sin(fly.phase * 3.7), 2);
        p.setOpacity(qMin(1.0, pulse + fly.boost * .65));
        p.drawImage(position - QPointF(glowRadius, glowRadius), fireflyGlow());
    }
    p.setOpacity(1);
    if (m_jar && clip.intersects(around(jarPosition(), 21))) {
        const QPointF j = jarPosition();
        p.setPen(QPen(QColor(146, 211, 218, 194), 1.2));
        p.setBrush(QColor(157, 221, 211, 68));
        p.drawRoundedRect(QRectF(j - QPointF(13, 13), QSizeF(26, 32)), 4, 4);
        for (int i = 0; i < m_caught; ++i) {
            const QPointF offset(5 * std::sin(m_time * 1.7 + i * 2), 8 * std::cos(m_time * 1.4 + i));
            p.drawImage(j + offset - QPointF(glowRadius, glowRadius), fireflyGlow());
        }
    }
    p.restore();
}
