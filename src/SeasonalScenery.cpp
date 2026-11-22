#include "SeasonalScenery.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QTime>

#include <array>
#include <cmath>

namespace {
double randomBetween(double low, double high)
{
    return low + (high - low) * QRandomGenerator::global()->generateDouble();
}

QRectF around(const QPointF &point, double radius)
{
    return {point.x() - radius, point.y() - radius, 2 * radius, 2 * radius};
}

bool isNight()
{
    const int hour = QTime::currentTime().hour();
    return hour >= 19 || hour < 6;
}

QPointF dustPosition(const QRectF &r, int i, double time)
{
    return r.topLeft() + QPointF(r.width() * (.19 + .7 * std::fmod(.158 * i + time * .018, 1.0)),
                                r.height() * (.12 + .74 * std::fmod(.297 * i + time * .025, 1.0)));
}

QPointF mothPosition(const QRectF &r, int i, double time)
{
    const double phase = time * (1.9 + i * .23) + i * 2;
    return r.topLeft() + QPointF(r.width() * .5 + std::cos(phase) * (12 + i * 3),
                                25 + std::sin(phase * 1.4) * (9 + i * 2));
}
}

void SeasonalScenery::setLayout(const QSize &size, const QRect &dock)
{
    m_dock = dock;
    if (m_size != size) {
        m_size = size;
        populate();
    }
}

void SeasonalScenery::setSeason(Season season)
{
    m_season = season;
    m_fan = false;
    m_night = isNight();
    populate();
}

void SeasonalScenery::populate()
{
    m_flowers.clear();
    m_bees.clear();
    m_butterfly.reset();
    m_hedgehog.reset();
    m_wetness = 0;
    m_canopy = 0;
    m_flash = 0;
    m_boatPuddle = -1;
    m_butterflyDelay = randomBetween(5, 9);
    m_hedgehogDelay = randomBetween(8, 19);
    m_flashDelay = randomBetween(28, 58);
    m_pollination = 0;
    if (m_size.isEmpty() || (m_season != Season::Spring && m_season != Season::Summer))
        return;
    for (int i = 0; i < 8; ++i) {
        const double center = m_size.width() * (i < 4 ? .12 : .86);
        const double x = qBound(4.0, center + (i % 4 - 1.5) * 13, qMax(4, m_size.width() - 5) * 1.0);
        m_flowers.push_back({x, .06 + .10 * (i % 3), i % 6});
    }
    m_bees.push_back({flowerTip(m_flowers[0]) - QPointF(0, 12), 0, 1});
    if (m_season == Season::Summer)
        m_bees.push_back({flowerTip(m_flowers[4]) - QPointF(0, 12), 4, 1});
}

double SeasonalScenery::groundAt(double x) const
{
    return !m_dock.isEmpty() && x >= m_dock.left() && x < m_dock.right() + 1 ? m_dock.top() : m_size.height();
}

QRectF SeasonalScenery::puddle(int index) const
{
    const double x = m_size.width() * (.23 + index * .25);
    const double half = qMin(48.0, m_size.width() * .10) * std::sqrt(m_wetness);
    return {x - half, groundAt(x) - 3 - 6 * m_wetness, half * 2, 2 + 6 * m_wetness};
}

bool SeasonalScenery::inPuddle(double x) const
{
    if (m_wetness < .15)
        return false;
    for (int i = 0; i < 3; ++i) {
        const QRectF water = puddle(i);
        if (x >= water.left() && x <= water.right() && std::abs(groundAt(x) - water.bottom()) < 5)
            return true;
    }
    return false;
}

QRectF SeasonalScenery::umbrellaBounds() const
{
    const double x = qMax(28, m_size.width() - 60);
    return {x - 36, groundAt(x) - 66, 72, 67};
}

QRectF SeasonalScenery::lampBounds() const
{
    const double x = qMax(30, m_size.width() - 62);
    return {x - 38, groundAt(x) - 86, 76, 87};
}

QRectF SeasonalScenery::fanBounds() const
{
    const double x = qMin(68, m_size.width() / 3);
    return {x - 22, groundAt(x) - 59, 142, 60};
}

QRectF SeasonalScenery::raysBounds() const
{
    return {m_size.width() * .32, 0, m_size.width() * .68, m_size.height() * .58};
}

QRectF SeasonalScenery::mirageBounds(int patch) const
{
    const double x = m_size.width() * (.32 + patch * .22);
    return {x - 42, groundAt(x) - 23, 84, 18};
}

QPointF SeasonalScenery::flowerTip(const Flower &flower) const
{
    const double length = (m_season == Season::Summer ? 36 : 22) + (flower.type % 4) * 4;
    const double sway = std::sin(m_time * 2.1 + flower.x * .065) * qMin(6.0, 1.2 + std::abs(m_wind) * .036);
    return {flower.x + sway * flower.growth, groundAt(flower.x) - length * flower.growth};
}

QRectF SeasonalScenery::flowerBounds(const Flower &flower) const
{
    return {flower.x - 24, groundAt(flower.x) - 65, 48, 66};
}

QRectF SeasonalScenery::insectBounds(const QPointF &pos) const
{
    return around(pos, 15);
}

bool SeasonalScenery::plant(double x)
{
    if (m_flowers.size() >= 28 || m_size.width() < 8)
        return false;
    x = qBound(4.0, x, m_size.width() - 4.0);
    for (const Flower &flower : m_flowers)
        if (std::abs(flower.x - x) < 8)
            return false;
    m_flowers.push_back({x, .05, QRandomGenerator::global()->bounded(6)});
    return true;
}

bool SeasonalScenery::water(double x)
{
    bool watered = false;
    for (Flower &flower : m_flowers) {
        if (std::abs(x - flower.x) < 43 && flower.growth < 1) {
            flower.growth = qMin(1.0, flower.growth + .38);
            watered = true;
        }
    }
    return watered;
}

bool SeasonalScenery::launchBoat()
{
    if (m_wetness < .18)
        return false;
    m_boatPuddle = QRandomGenerator::global()->bounded(3);
    m_boatAge = 0;
    return true;
}

QRegion SeasonalScenery::setFan(bool enabled)
{
    if (m_season != Season::Summer || m_fan == enabled)
        return {};
    m_fan = enabled;
    return QRegion(fanBounds().toAlignedRect());
}

void SeasonalScenery::steerInsects(const QPointF &pos, const QPointF &motion)
{
    if (m_butterfly && QLineF(pos, *m_butterfly).length() < 85)
        m_butterflyOffset = qBound(-100.0, m_butterflyOffset + motion.x() * .65, 100.0);
}

QRegion SeasonalScenery::advance(double dt, double wind, double rain)
{
    QRegion dirty;
    const double previousGroundFrame = m_lastGroundFrame;
    m_time += dt;
    m_wind = wind + (m_fan ? 65 : 0);
    const bool groundFrame = m_time - m_lastGroundFrame >= .08;
    if (groundFrame)
        m_lastGroundFrame = m_time;
    const auto mark = [&](const QRectF &rect) { dirty += rect.toAlignedRect(); };

    const auto markPuddles = [&] {
        for (int i = 0; i < 3; ++i)
            mark(puddle(i).adjusted(-11, -19, 11, 3));
    };
    if (m_wetness > 0 || rain > 0) {
        const double old = m_wetness;
        m_wetness = qBound(0.0, m_wetness + dt * (rain > 0 ? .10 * rain : -.022), 1.0);
        if (groundFrame || (old > 0 && m_wetness == 0) || m_boatPuddle >= 0)
            markPuddles();
    }
    if (m_boatPuddle >= 0) {
        m_boatAge += dt;
        if (m_wetness < .15 || m_boatAge > 13) {
            markPuddles();
            m_boatPuddle = -1;
        }
    }

    if (m_season == Season::Autumn) {
        const double canopy = m_canopy;
        m_canopy = rain > 0 ? qMin(1.0, canopy + dt * 2.1) : qMax(0.0, canopy - dt * 1.6);
        if (canopy != m_canopy)
            mark(umbrellaBounds());
        if (m_hedgehog) {
            const auto bounds = [this](double x) { return QRectF(x - 25, groundAt(x) - 29, 52, 30); };
            mark(bounds(*m_hedgehog));
            *m_hedgehog += dt * 108;
            if (*m_hedgehog > m_size.width() + 28) {
                m_hedgehog.reset();
                m_hedgehogDelay = randomBetween(19, 53);
            } else {
                mark(bounds(*m_hedgehog));
            }
        } else if ((m_hedgehogDelay -= dt) <= 0) {
            m_hedgehog = -25;
        }
        if (rain > .62 && (m_flashDelay -= dt) <= 0) {
            m_flash = .29;
            m_flashDelay = randomBetween(31, 86);
        }
        if (m_flash > 0) {
            m_flash = qMax(0.0, m_flash - dt);
            mark(QRectF(0, 0, m_size.width(), m_size.height() * .26));
        }
    }

    for (Flower &flower : m_flowers) {
        flower.growth = qMin(1.0, flower.growth + dt * (.031 + .028 * rain));
        if (groundFrame)
            mark(flowerBounds(flower));
    }

    if (m_season == Season::Summer) {
        if (m_fan && groundFrame)
            mark(fanBounds());
        if ((m_lightCheck -= dt) <= 0) {
            m_lightCheck = 15;
            const bool night = isNight();
            if (night != m_night) {
                mark(raysBounds());
                mark(lampBounds());
                for (int patch = 0; patch < 3; ++patch)
                    mark(mirageBounds(patch));
                m_night = night;
            }
        }
        if (m_night) {
            if (groundFrame)
                mark(lampBounds());
        } else if (groundFrame) {
            for (int i = 0; i < 10; ++i) {
                mark(around(dustPosition(raysBounds(), i, previousGroundFrame), 5));
                mark(around(dustPosition(raysBounds(), i, m_time), 5));
            }
            for (int patch = 0; patch < 3; ++patch)
                mark(mirageBounds(patch));
        }
    }

    for (Bee &bee : m_bees) {
        mark(insectBounds(bee.pos));
        if (bee.pause > 0) {
            bee.pause -= dt;
        } else {
            const QPointF target = flowerTip(m_flowers[bee.target]) - QPointF(0, 7);
            const QPointF difference = target - bee.pos;
            const double distance = std::hypot(difference.x(), difference.y());
            if (distance < 7) {
                Flower &flower = m_flowers[bee.target];
                flower.growth = qMin(1.0, flower.growth + .17);
                if (m_season == Season::Summer && ++m_pollination >= 4) {
                    const double x = flower.x;
                    plant(x + randomBetween(-56, 56));
                    m_pollination = 0;
                }
                bee.target = (bee.target + (m_season == Season::Summer ? 3 : 1)) % int(m_flowers.size());
                bee.pause = .45;
            } else {
                bee.pos += difference * (qMin(distance, dt * 88) / distance);
                bee.pos.ry() += std::sin(m_time * 17 + bee.target) * dt * 20;
            }
        }
        mark(insectBounds(bee.pos));
    }

    if (m_season == Season::Spring || m_season == Season::Summer) {
        if (m_butterfly) {
            mark(insectBounds(*m_butterfly));
            m_butterflyAge += dt;
            m_butterflyX += dt * 87;
            m_butterflyOffset *= std::exp(-dt * .7);
            const double x = m_butterflyX + m_butterflyOffset + 12 * std::sin(m_time * 5);
            const double y = groundAt(x) - 44 - 28 * std::sin(m_butterflyAge * 1.3) -
                             12 * std::sin(m_butterflyAge * 7.4);
            m_butterfly = QPointF(x, y);
            if (m_butterflyX > m_size.width() + 105) {
                m_butterfly.reset();
                m_butterflyDelay = randomBetween(10, 25);
            } else {
                mark(insectBounds(*m_butterfly));
            }
        } else if ((m_butterflyDelay -= dt) <= 0) {
            m_butterflyX = -20;
            m_butterflyAge = 0;
            m_butterflyOffset = 0;
            m_butterfly = QPointF(-20, groundAt(0) - 44);
        }
    }
    return dirty;
}

QRegion SeasonalScenery::visualRegion() const
{
    QRegion region;
    for (const Flower &flower : m_flowers)
        region += flowerBounds(flower).toAlignedRect();
    for (const Bee &bee : m_bees)
        region += insectBounds(bee.pos).toAlignedRect();
    if (m_butterfly)
        region += insectBounds(*m_butterfly).toAlignedRect();
    if (m_wetness > 0)
        for (int i = 0; i < 3; ++i)
            region += puddle(i).adjusted(-11, -19, 11, 3).toAlignedRect();
    if (m_season == Season::Autumn) {
        region += umbrellaBounds().toAlignedRect();
        if (m_hedgehog)
            region += QRectF(*m_hedgehog - 25, groundAt(*m_hedgehog) - 29, 52, 30).toAlignedRect();
        if (m_flash > 0)
            region += QRectF(0, 0, m_size.width(), m_size.height() * .26).toAlignedRect();
    }
    if (m_season == Season::Summer) {
        region += m_night ? lampBounds().toAlignedRect() : raysBounds().toAlignedRect();
        if (m_fan)
            region += fanBounds().toAlignedRect();
        if (!m_night)
            for (int patch = 0; patch < 3; ++patch)
                region += mirageBounds(patch).toAlignedRect();
    }
    return region;
}

void SeasonalScenery::paintPlants(QPainter &p, const QRegion &clip) const
{
    static const std::array<QColor, 6> springColors{{
        QColor(250, 247, 238), QColor(249, 122, 165), QColor(255, 206, 71),
        QColor(177, 131, 237), QColor(255, 230, 58), QColor(186, 158, 244),
    }};
    static const std::array<QColor, 6> summerColors{{
        QColor(253, 248, 222), QColor(232, 76, 73), QColor(132, 150, 242),
        QColor(255, 192, 50), QColor(219, 178, 119), QColor(240, 167, 224),
    }};
    const bool summer = m_season == Season::Summer;
    const double time = m_lastGroundFrame;
    for (const Flower &flower : m_flowers) {
        if (!clip.intersects(flowerBounds(flower).toAlignedRect()))
            continue;
        const double ground = groundAt(flower.x) - 1;
        const QPointF tip = flowerTip(flower);
        for (int i = -2; i <= 2; ++i) {
            const double x = flower.x + i * 4;
            const double height = (4 + (i * i % 3) * 3 + (summer ? 11 : 3)) * flower.growth;
            const double bend = 2.5 * std::sin(time * 2.8 + x * .35) + m_wind * .045;
            p.setPen(QPen(i % 2 == 0 ? QColor(98, 157, 66, 215) : QColor(146, 187, 74, 205), 1.35));
            p.drawLine(QPointF(x, ground), QPointF(x + bend, ground - height));
        }
        p.setPen(QPen(QColor(74, 132, 61), 1.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(flower.x, ground), tip);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(102, 159, 70));
        p.drawEllipse(tip + QPointF(-3, (ground - tip.y()) * .54), 3.5 * flower.growth, 1.7);
        const double bloom = qBound(0.0, (flower.growth - .27) / .73, 1.0);
        const double radius = bloom * (summer && flower.type == 3 ? 6.6 : 4.1);
        QColor color = (summer ? summerColors : springColors)[flower.type];
        if (m_wetness > .2)
            color = color.lighter(114);
        p.setBrush(color);
        if (!summer && flower.type == 1) {
            QPainterPath tulip;
            tulip.moveTo(tip + QPointF(-radius, -radius));
            tulip.lineTo(tip + QPointF(-radius * .44, -radius * .54));
            tulip.lineTo(tip - QPointF(0, radius * 1.24));
            tulip.lineTo(tip + QPointF(radius * .44, -radius * .54));
            tulip.lineTo(tip + QPointF(radius, -radius));
            tulip.cubicTo(tip + QPointF(radius, radius * 1.08), tip + QPointF(-radius, radius * 1.08),
                          tip + QPointF(-radius, -radius));
            p.drawPath(tulip);
            continue;
        }
        if (summer && flower.type == 4) {
            for (int i = 0; i < 4; ++i) {
                const QPointF grain = tip + QPointF(0, (i - 1) * radius * .8);
                p.drawEllipse(grain + QPointF(-radius * .43, 0), radius * .41, radius * .33);
                p.drawEllipse(grain + QPointF(radius * .43, 0), radius * .41, radius * .33);
            }
            continue;
        }
        const int petals = summer && flower.type == 3 ? 11 : 4 + flower.type % 3;
        for (int i = 0; i < petals; ++i) {
            const double angle = 6.2831853 * i / petals;
            p.drawEllipse(tip + QPointF(std::cos(angle), std::sin(angle)) * radius, radius * .72, radius * .70);
        }
        const bool darkCenter = summer && (flower.type == 1 || flower.type == 3);
        p.setBrush(darkCenter ? QColor(93, 52, 43) : QColor(237, 170, 60));
        const double center = radius * (!summer && flower.type == 2 ? .77 : .55);
        p.drawEllipse(tip, center, center);
    }
}

void SeasonalScenery::paintAutumnProps(QPainter &p, const QRegion &clip) const
{
    const QRectF umbrella = umbrellaBounds();
    if (clip.intersects(umbrella.toAlignedRect())) {
        const QPointF top(umbrella.center().x(), umbrella.top() + 9);
        p.setPen(QPen(QColor(66, 58, 64), 2.3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(top, top + QPointF(0, 48));
        p.drawArc(QRectF(top.x() - 9, top.y() + 40, 9, 12), 180 * 16, 180 * 16);
        const double span = 4 + 27 * m_canopy;
        const double depth = 43 - 28 * m_canopy;
        QPainterPath cloth;
        cloth.moveTo(top.x() - span, top.y() + depth);
        cloth.quadTo(top.x() - span * .85, top.y(), top.x(), top.y());
        cloth.quadTo(top.x() + span * .85, top.y(), top.x() + span, top.y() + depth);
        cloth.quadTo(top.x() + span * .55, top.y() + depth - 6, top.x(), top.y() + depth);
        cloth.quadTo(top.x() - span * .55, top.y() + depth - 6, top.x() - span, top.y() + depth);
        QLinearGradient fabric(top - QPointF(span, 0), top + QPointF(span, depth));
        fabric.setColorAt(0, QColor(230, 99, 84));
        fabric.setColorAt(.55, QColor(169, 58, 90));
        fabric.setColorAt(1, QColor(105, 43, 77));
        p.setPen(QPen(QColor(99, 44, 73), 1.1));
        p.setBrush(fabric);
        p.drawPath(cloth);
        p.setPen(QPen(QColor(255, 184, 162, 160), .8));
        p.drawLine(top + QPointF(0, 3), top + QPointF(0, depth - 2));
    }

    if (m_hedgehog) {
        const double x = *m_hedgehog;
        const double ground = groundAt(x);
        p.save();
        p.translate(x, ground - 12 + std::sin(m_time * 25) * 1.4);
        p.setPen(QPen(QColor(100, 66, 49), 2.4, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 4; ++i)
            p.drawLine(QPointF(-14 + 6 * i, 5), QPointF(-14 + 6 * i + 3 * std::sin(m_time * 26 + i * 2), 10));
        QPolygonF spikes;
        for (int i = 0; i < 19; ++i) {
            const double angle = 3.14 + i * 3.14 / 18;
            const double radius = i % 2 ? 12 : 16;
            spikes << QPointF(-4 + std::cos(angle) * radius * 1.25, 3 + std::sin(angle) * radius);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(102, 66, 50));
        p.drawPolygon(spikes);
        p.drawEllipse(QPointF(-4, 3), 17, 6.5);
        p.setBrush(QColor(197, 152, 106));
        p.drawEllipse(QPointF(11, 2), 9, 6);
        p.setBrush(QColor(41, 28, 28));
        p.drawEllipse(QPointF(13, 0), 1.3, 1.3);
        p.drawEllipse(QPointF(20, 3), 2, 1.6);
        p.restore();
    }

    if (m_flash > 0) {
        QLinearGradient flash(0, 0, 0, m_size.height() * .26);
        flash.setColorAt(0, QColor(190, 199, 255, qRound(75 * m_flash / .29)));
        flash.setColorAt(1, QColor(190, 199, 255, 0));
        p.fillRect(QRectF(0, 0, m_size.width(), m_size.height() * .26), flash);
        p.setPen(QPen(QColor(231, 226, 255, qRound(135 * m_flash / .29)), 1));
        const double x = m_size.width() * .72;
        p.drawPolyline(QPolygonF{QPointF(x, 0), QPointF(x - 18, 26), QPointF(x - 7, 29), QPointF(x - 25, 55)});
    }
}

void SeasonalScenery::paintSummerLight(QPainter &p, const QRegion &clip) const
{
    if (m_night) {
        const QRectF r = lampBounds();
        if (!clip.intersects(r.toAlignedRect()))
            return;
        const QPointF light(r.center().x(), r.y() + 30);
        QRadialGradient glow(light, 29);
        glow.setColorAt(0, QColor(252, 194, 85, 130));
        glow.setColorAt(1, QColor(252, 194, 85, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(light, 29, 29);
        p.setPen(QPen(QColor(71, 64, 55), 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(light + QPointF(0, 6), QPointF(light.x(), r.bottom() - 2));
        p.setBrush(QColor(255, 226, 150));
        p.drawRoundedRect(QRectF(light - QPointF(6, 6), QSizeF(12, 14)), 2, 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 224, 175, 205));
        for (int i = 0; i < 5; ++i)
            p.drawEllipse(mothPosition(r, i, m_lastGroundFrame), 1.2, 1);
        return;
    }
    const QRectF r = raysBounds();
    if (clip.intersects(r.toAlignedRect())) {
        const QPointF source(r.right() - r.width() * .13, 0);
        QLinearGradient shaft(0, r.top(), 0, r.bottom());
        shaft.setColorAt(0, QColor(255, 209, 115, 43));
        shaft.setColorAt(.62, QColor(255, 215, 137, 12));
        shaft.setColorAt(1, QColor(255, 219, 154, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(shaft);
        for (int i = 0; i < 3; ++i) {
            const double x = r.left() + r.width() * (.04 + i * .24);
            p.drawPolygon(QPolygonF{source, QPointF(x, r.bottom()), QPointF(x + r.width() * .16, r.bottom())});
        }
        p.setBrush(QColor(255, 227, 169, 135));
        for (int i = 0; i < 10; ++i)
            p.drawEllipse(dustPosition(r, i, m_lastGroundFrame), i % 3 == 0 ? 1.8 : 1.1, 1.1);
    }
    p.setPen(QPen(QColor(255, 216, 157, 56), 1.2));
    p.setBrush(Qt::NoBrush);
    for (int patch = 0; patch < 3; ++patch) {
        const double center = m_size.width() * (.32 + patch * .22);
        const double y = groundAt(center) - 13;
        for (int i = 0; i < 3; ++i) {
            QPainterPath shimmer;
            shimmer.moveTo(center - 35 + i * 9, y + i * 3);
            shimmer.quadTo(center + std::sin(m_lastGroundFrame * 5 + i) * 19, y - 8 + i * 3,
                           center + 35 - i * 6, y + i * 3);
            p.drawPath(shimmer);
        }
    }
}

void SeasonalScenery::paintInsects(QPainter &p, const QRegion &clip) const
{
    for (const Bee &bee : m_bees) {
        if (!clip.intersects(insectBounds(bee.pos).toAlignedRect()))
            continue;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(226, 241, 250, 182));
        const double flap = 1 + 2.5 * std::abs(std::sin(m_time * 42));
        p.drawEllipse(bee.pos - QPointF(2, 3), flap, 2.7);
        p.drawEllipse(bee.pos + QPointF(2, -3), flap, 2.7);
        p.setBrush(QColor(241, 180, 59));
        p.drawEllipse(bee.pos, 5, 3);
        p.setPen(QPen(QColor(74, 54, 42), 1.5));
        p.drawLine(bee.pos + QPointF(-1, -2), bee.pos + QPointF(-1, 2));
        p.drawLine(bee.pos + QPointF(2, -2), bee.pos + QPointF(2, 2));
    }
    if (!m_butterfly || !clip.intersects(insectBounds(*m_butterfly).toAlignedRect()))
        return;
    const double spread = 2 + 8 * std::abs(std::sin(m_time * 18));
    p.setPen(QPen(QColor(105, 66, 103), .8));
    for (const int side : {-1, 1}) {
        p.setBrush(QColor(226, 145, 191, 231));
        p.drawEllipse(*m_butterfly + QPointF(side * spread * .52, -2), spread * .62, 5);
        p.setBrush(QColor(245, 194, 136, 231));
        p.drawEllipse(*m_butterfly + QPointF(side * spread * .40, 2), spread * .48, 3);
    }
}

void SeasonalScenery::paint(QPainter &p, const QRegion &clip) const
{
    p.save();
    if (m_season == Season::Summer)
        paintSummerLight(p, clip);
    if (m_season == Season::Autumn)
        paintAutumnProps(p, clip);

    if (m_wetness > 0) {
        for (int i = 0; i < 3; ++i) {
            const QRectF r = puddle(i);
            if (!clip.intersects(r.adjusted(-11, -19, 11, 3).toAlignedRect()))
                continue;
            p.setPen(QPen(QColor(157, 206, 218, qRound(138 * m_wetness)), .8));
            p.setBrush(QColor(65, 113, 145, qRound(160 * m_wetness)));
            p.drawEllipse(r);
            p.setPen(QPen(QColor(194, 221, 233, qRound(126 * m_wetness)), .75));
            p.drawLine(r.center() - QPointF(r.width() * .32, 1), r.center() - QPointF(r.width() * .17, 1));
            if (m_boatPuddle == i) {
                const QPointF b(r.center().x() + r.width() * .28 * std::sin(m_boatAge * .44),
                                r.center().y() + std::sin(m_boatAge * 2.3) * 1.2);
                p.setPen(QPen(QColor(92, 124, 146), .7));
                p.setBrush(QColor(232, 246, 247));
                p.drawPolygon(QPolygonF{b + QPointF(-9, -4), b + QPointF(8, -4), b + QPointF(5, 0), b + QPointF(-5, 0)});
                p.setBrush(QColor(249, 251, 244));
                p.drawPolygon(QPolygonF{b + QPointF(-6, -4), b + QPointF(1, -11), b + QPointF(5, -4)});
            }
        }
    }
    paintPlants(p, clip);
    paintInsects(p, clip);
    if (m_fan && clip.intersects(fanBounds().toAlignedRect())) {
        const QRectF box = fanBounds();
        const QPointF center = box.topLeft() + QPointF(22, 33);
        p.setPen(QPen(QColor(65, 91, 104), 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(center, center + QPointF(0, 20));
        p.drawLine(center + QPointF(-10, 21), center + QPointF(10, 21));
        p.setPen(QPen(QColor(144, 188, 193), 1));
        p.setBrush(QColor(62, 101, 115, 185));
        p.drawEllipse(center, 14, 14);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(154, 212, 218, 209));
        for (int i = 0; i < 3; ++i) {
            p.save();
            p.translate(center);
            p.rotate(m_lastGroundFrame * 680 + i * 120);
            p.drawEllipse(QPointF(0, 6), 3.2, 5.5);
            p.restore();
        }
        p.setBrush(QColor(241, 236, 196, 160));
        for (int i = 0; i < 7; ++i) {
            const double x = 18 + 98 * std::fmod(m_lastGroundFrame * .73 + i * .219, 1.0);
            p.drawEllipse(center + QPointF(x, std::sin(i * 2.4 + m_lastGroundFrame * 6) * 8), 1.25, .8);
        }
    }
    p.restore();
}
