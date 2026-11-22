#include "SnowProps.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QRegion>
#include <QtMath>

#include <array>
#include <cmath>

namespace SnowProps {
namespace {

QPainterPath treeTier(double tip, double base, double halfWidth)
{
    QPainterPath tier(QPointF(50, tip));
    tier.cubicTo(50 + halfWidth * .22, tip + 13, 50 + halfWidth * .62, base - 14, 50 + halfWidth, base - 8);
    tier.quadTo(50 + halfWidth * .91, base + 1, 50 + halfWidth * .64, base - 2);
    tier.quadTo(50, base + 8, 50 - halfWidth * .64, base - 2);
    tier.quadTo(50 - halfWidth * .91, base + 1, 50 - halfWidth, base - 8);
    tier.cubicTo(50 - halfWidth * .62, base - 14, 50 - halfWidth * .22, tip + 13, 50, tip);
    tier.closeSubpath();
    return tier;
}

const std::array<QPainterPath, 3> &treeTiers()
{
    static const std::array tiers{treeTier(43, 106, 45), treeTier(27, 77, 35), treeTier(10, 51, 26)};
    return tiers;
}

QTransform treeTransform(const QRect &frame)
{
    QTransform transform;
    transform.translate(frame.x(), frame.y());
    transform.scale(frame.width() / 100.0, frame.height() / 132.0);
    return transform;
}

QImage makeSnowball()
{
    constexpr int size = 144;
    constexpr int radius = 68;
    const QPointF center(size / 2, size / 2);
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    constexpr double tau = 6.283185307179586;
    QPainterPath outline;
    for (int i = 0; i < 144; ++i) {
        const double angle = tau * i / 144;
        const double edge = radius + 1.25 * std::sin(7 * angle) + .85 * std::sin(17 * angle + .7);
        const QPointF point = center + QPointF(std::cos(angle) * edge, std::sin(angle) * edge);
        if (i == 0)
            outline.moveTo(point);
        else
            outline.lineTo(point);
    }
    outline.closeSubpath();

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QRadialGradient lighting(center + QPointF(-15, -23), 98);
    lighting.setColorAt(0, QColor(255, 255, 251));
    lighting.setColorAt(.38, QColor(247, 249, 248));
    lighting.setColorAt(.69, QColor(217, 231, 240));
    lighting.setColorAt(.92, QColor(157, 183, 207));
    lighting.setColorAt(1, QColor(184, 209, 228));
    painter.fillPath(outline, lighting);
    painter.setClipPath(outline);
    painter.setPen(Qt::NoPen);

    QRandomGenerator grains(647);
    for (int i = 0; i < 1250; ++i) {
        const QPointF point(grains.bounded(size), grains.bounded(size));
        const double r = .55 + grains.generateDouble() * (i < 90 ? 2.5 : 1.3);
        painter.setBrush(QColor(73, 120, 154, i < 90 ? 29 : 17));
        painter.drawEllipse(point + QPointF(.4, .75), r, r * .72);
        painter.setBrush(QColor(255, 255, 255, i < 90 ? 155 : 88));
        painter.drawEllipse(point, r * .85, r * .62);
    }
    painter.end();
    return image;
}

} // namespace

QPainterPath treeCanopy(const QRect &frame)
{
    QPainterPath canopy;
    canopy.setFillRule(Qt::WindingFill);
    for (const auto &tier : treeTiers())
        canopy.addPath(tier);
    canopy.addEllipse(QPointF(50, 12), 7, 7);
    return treeTransform(frame).map(canopy);
}

static void paintTreeArtwork(QPainter &p, const QRect &frame)
{
    p.save();
    p.setWorldTransform(treeTransform(frame), true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(12, 31, 42, 88));
    p.drawEllipse(QRectF(13, 124, 74, 7));
    p.setBrush(QColor(91, 56, 37));
    p.drawRoundedRect(QRectF(44, 92, 12, 30), 2, 2);
    QLinearGradient pot(38, 0, 64, 0);
    pot.setColorAt(0, QColor(121, 35, 39));
    pot.setColorAt(.44, QColor(206, 67, 62));
    pot.setColorAt(1, QColor(135, 38, 43));
    p.setBrush(pot);
    p.drawRoundedRect(QRectF(37, 113, 26, 15), 4, 4);
    p.setPen(QPen(QColor(251, 199, 104), 1.5));
    p.drawLine(QPointF(38, 117), QPointF(62, 117));

    const auto &tiers = treeTiers();
    for (const auto &tier : tiers) {
        const QRectF bounds = tier.boundingRect();
        QLinearGradient needles(bounds.topLeft(), bounds.bottomRight());
        needles.setColorAt(0, QColor(82, 172, 122));
        needles.setColorAt(.48, QColor(29, 116, 84));
        needles.setColorAt(1, QColor(12, 58, 51));
        p.setPen(QPen(QColor(10, 57, 48), 1.2));
        p.setBrush(needles);
        p.drawPath(tier);
    }

    const QPointF starCenter(50, 12);
    QRadialGradient glow(starCenter, 12);
    glow.setColorAt(0, QColor(255, 236, 146, 190));
    glow.setColorAt(.54, QColor(255, 194, 69, 75));
    glow.setColorAt(1, QColor(255, 179, 61, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(starCenter, 12, 12);

    QPolygonF star;
    for (int i = 0; i < 10; ++i) {
        const double angle = (i * .2 - .5) * 3.141592653589793;
        const double radius = i % 2 ? 3.7 : 8.3;
        star.append(starCenter + QPointF(std::cos(angle) * radius, std::sin(angle) * radius));
    }
    QLinearGradient gold(50, 4, 50, 19);
    gold.setColorAt(0, QColor(255, 255, 229));
    gold.setColorAt(.52, QColor(255, 224, 132));
    gold.setColorAt(1, QColor(248, 173, 71));
    p.setBrush(gold);
    p.drawPolygon(star);
    p.restore();
}

void drawTree(QPainter &p, const QRect &frame)
{
    static const QImage sprite = [] {
        QImage image(200, 264, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        paintTreeArtwork(painter, image.rect());
        painter.end();
        return image;
    }();
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(frame, sprite);
    p.restore();
}

void drawTreeLights(QPainter &p, const QRect &frame, bool alternate)
{
    const auto makeSprite = [](bool phase) {
        QImage image(200, 264, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter lights(&image);
        lights.setRenderHint(QPainter::Antialiasing);
        lights.scale(2, 2);
        const std::array<QColor, 5> colors{{QColor(255, 118, 74), QColor(255, 217, 91),
            QColor(119, 243, 195), QColor(122, 189, 255), QColor(248, 148, 216)}};
        std::array<QPainterPath, 3> wire{{QPainterPath(QPointF(39, 32)), QPainterPath(QPointF(28, 54)),
                                         QPainterPath(QPointF(20, 81))}};
        wire[0].cubicTo(46, 40, 57, 40, 65, 43);
        wire[1].cubicTo(34, 63, 58, 63, 75, 68);
        wire[2].cubicTo(34, 91, 59, 91, 81, 94);
        lights.setBrush(Qt::NoBrush);
        lights.setPen(QPen(QColor(186, 158, 85, 210), 1));
        for (const auto &tier : wire)
            lights.drawPath(tier);
        lights.setPen(Qt::NoPen);
        for (int tier = 0; tier < 3; ++tier) {
            const int count = 4 + tier;
            for (int i = 0; i < count; ++i) {
                lights.setOpacity((i + tier) % 2 == phase ? 1.0 : .18);
                const QPointF bulb = wire[tier].pointAtPercent((i + .5) / count);
                QColor color = colors[(tier * 2 + i) % colors.size()];
                QRadialGradient glow(bulb, 5.2);
                glow.setColorAt(0, QColor(255, 254, 222));
                glow.setColorAt(.23, color.lighter(123));
                color.setAlpha(110);
                glow.setColorAt(.46, color);
                color.setAlpha(0);
                glow.setColorAt(1, color);
                lights.setBrush(glow);
                lights.drawEllipse(bulb, 5.2, 5.2);
            }
        }
        lights.end();
        return image;
    };
    static const std::array<QImage, 2> sprites{makeSprite(false), makeSprite(true)};
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(frame, sprites[alternate]);
    p.restore();
}

void drawSnowball(QPainter &p, const QPointF &center, double radius, bool onGround)
{
    if (onGround) {
        const QPointF shadowCenter = center + QPointF(0, radius * .91);
        QRadialGradient shadow(shadowCenter, radius);
        shadow.setColorAt(0, QColor(28, 47, 74, 108));
        shadow.setColorAt(1, QColor(28, 47, 74, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(shadow);
        p.drawEllipse(shadowCenter, radius * .87, radius * .2);
    }
    static const QImage snow = makeSnowball();
    const double extent = radius * (72.0 / 68);
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(center.x() - extent, center.y() - extent, extent * 2, extent * 2), snow);
    p.restore();
}

void drawSnowman(QPainter &p, const Ball &base, const Ball &torso, const Ball &head, double progress)
{
    const double t = qBound(0.0, progress, 1.0);
    if (t <= 0.01)
        return;

    p.save();
    const double detail = std::sqrt(head.radius / 11);

    // 1) Ручки-веточки по бокам среднего кома (туловища)
    const double armScale = qBound(0.0, (t - 0.1) / 0.5, 1.0);
    if (armScale > 0.0) {
        p.setPen(QPen(QColor(92, 60, 36), 2.4, Qt::SolidLine, Qt::RoundCap));
        const double lx = torso.x - torso.radius * 0.85;
        const double rx = torso.x + torso.radius * 0.85;
        const double ay = torso.y - torso.radius * 0.15;
        const double len = 16.0 * armScale * std::sqrt(torso.radius / 19);

        p.drawLine(QPointF(lx, ay), QPointF(lx - len, ay - len * 0.45));
        p.drawLine(QPointF(lx - len * 0.65, ay - len * 0.3), QPointF(lx - len * 0.85, ay - len * 0.75));

        p.drawLine(QPointF(rx, ay), QPointF(rx + len, ay - len * 0.45));
        p.drawLine(QPointF(rx + len * 0.65, ay - len * 0.3), QPointF(rx + len * 0.85, ay - len * 0.75));
    }

    // 2) Пуговицы-угольки на туловище и основании
    const double btnScale = qBound(0.0, (t - 0.15) / 0.45, 1.0);
    if (btnScale > 0.0) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(35, 38, 45));
        const double br = 2.3 * btnScale * std::sqrt(torso.radius / 19);
        p.drawEllipse(QPointF(torso.x, torso.y - 4.0), br, br);
        p.drawEllipse(QPointF(torso.x, torso.y + 4.0), br, br);
        p.drawEllipse(QPointF(base.x,  base.y - 6.0),  br * 1.1, br * 1.1);
    }

    // 3) Уютный шарф вокруг шеи (между головой и туловищем)
    {
        const double scarfScale = qBound(0.0, (t - 0.25) / 0.5, 1.0);
        if (scarfScale > 0.0) {
            const double neckY = head.y + head.radius - 2.0;
            const double sw = (head.radius * 1.08) * scarfScale;
            p.setPen(QPen(QColor(150, 25, 35), 1.0));
            p.setBrush(QColor(215, 45, 55));
            p.drawRoundedRect(QRectF(head.x - sw, neckY - 3.5, sw * 2.0, 7.0), 3.5, 3.5);

            // Свисающий кончик шарфа с полоской
            QRectF tail(head.x + sw * 0.35, neckY + 1.0, 6.5 * scarfScale * detail, 14.0 * scarfScale * detail);
            p.drawRoundedRect(tail, 2.0, 2.0);
            p.setBrush(QColor(250, 215, 90));
            p.setPen(Qt::NoPen);
            p.drawRect(QRectF(tail.left(), tail.bottom() - 4.5 * scarfScale, tail.width(), 2.2 * scarfScale));
        }
    }

    // 4) Глаза-угольки и улыбка на малом коме (голове)
    const double faceScale = qBound(0.0, (t - 0.05) / 0.45, 1.0);
    if (faceScale > 0.0) {
        const double eyeY = head.y - head.radius * 0.22;
        const double eyeDx = head.radius * 0.36;
        const double er = 2.1 * faceScale * detail;

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(28, 30, 36));
        p.drawEllipse(QPointF(head.x - eyeDx, eyeY), er, er);
        p.drawEllipse(QPointF(head.x + eyeDx, eyeY), er, er);

        // Блики в глазах
        p.setBrush(QColor(255, 255, 255, 220));
        p.drawEllipse(QPointF(head.x - eyeDx - 0.6, eyeY - 0.6), er * 0.38, er * 0.38);
        p.drawEllipse(QPointF(head.x + eyeDx - 0.6, eyeY - 0.6), er * 0.38, er * 0.38);

        // Улыбка из мелких угольков
        p.setBrush(QColor(35, 38, 45));
        for (int i = -2; i <= 2; ++i) {
            const double sx = head.x + i * 2.4 * detail;
            const double sy = head.y + head.radius * 0.34 - (i * i) * 0.38;
            p.drawEllipse(QPointF(sx, sy), 1.1 * faceScale * detail, 1.1 * faceScale * detail);
        }
    }

    // 5) Нос-морковка
    const double noseScale = qBound(0.0, (t - 0.20) / 0.45, 1.0) * detail;
    if (noseScale > 0.0) {
        const QPointF noseBase(head.x + 1.0, head.y);
        QPolygonF carrot;
        carrot << QPointF(noseBase.x(), noseBase.y() - 2.3 * noseScale)
               << QPointF(noseBase.x() + 11.5 * noseScale, noseBase.y() + 0.6 * noseScale)
               << QPointF(noseBase.x(), noseBase.y() + 2.3 * noseScale);
        p.setPen(QPen(QColor(200, 85, 15), 0.9));
        p.setBrush(QColor(255, 125, 30));
        p.drawPolygon(carrot);
    }

    // 6) Ведро на голове (слегка набекрень)
    {
        const double hatScale = qBound(0.0, (t - 0.35) / 0.55, 1.0);
        if (hatScale > 0.0) {
            p.save();
            p.translate(head.x, head.y - head.radius + 2.0);
            p.rotate(-9.0 * hatScale);
            p.scale(hatScale * detail, hatScale * detail);

            QPolygonF bucket;
            bucket << QPointF(-9.5, 0.0)
                   << QPointF(9.5,  0.0)
                   << QPointF(7.2, -13.5)
                   << QPointF(-7.2, -13.5);

            QLinearGradient bGrad(-9.5, 0, 9.5, 0);
            bGrad.setColorAt(0.0, QColor(95, 108, 125));
            bGrad.setColorAt(0.45, QColor(145, 162, 182));
            bGrad.setColorAt(1.0, QColor(75, 86, 102));

            p.setPen(QPen(QColor(55, 65, 78), 1.1));
            p.setBrush(bGrad);
            p.drawPolygon(bucket);

            // Ободок ведра
            p.drawRoundedRect(QRectF(-10.8, -1.8, 21.6, 3.0), 1.2, 1.2);
            p.restore();
        }
    }

    p.restore();
}

void drawPlow(QPainter &p, double x, double y, int direction, double pushedSnow, double angle, double phase)
{
    p.save();
    p.translate(x, y);
    if (direction < 0)
        p.scale(-1.0, 1.0);

    // 1) Снежный вал перед отвалом
    if (pushedSnow > 0.5) {
        const double r = 4 + 25 * (1 - std::exp(-pushedSnow / 220));
        QPainterPath bank;
        bank.moveTo(24, -.6);
        bank.cubicTo(22, -r * .55, 24 + r * .26, -r * 1.26, 30 + r * .44, -r * .99);
        bank.cubicTo(32 + r * .68, -r * 1.04, 31 + r, -r * .33, 32 + r * 1.13, -.6);
        bank.closeSubpath();
        QLinearGradient snow(0, -r, 0, 0);
        snow.setColorAt(0, QColor(253, 254, 253));
        snow.setColorAt(.58, QColor(233, 246, 252));
        snow.setColorAt(1, QColor(167, 195, 219));
        p.fillPath(bank, snow);
        p.save();
        p.setClipPath(bank, Qt::IntersectClip);
        p.setPen(QPen(QColor(255, 255, 255, 150), 1.3, Qt::SolidLine, Qt::RoundCap));
        QPainterPath ridge;
        ridge.moveTo(26, -r * .42);
        ridge.quadTo(28 + r * .32, -r * .98, 30 + r * .77, -r * .64);
        p.drawPath(ridge);
        p.restore();
    }

    // 2) Свет фары
    QPolygonF beam;
    beam << QPointF(18.0, -13.0)
         << QPointF(68.0, -2.0)
         << QPointF(68.0, -22.0);
    QLinearGradient beamGrad(18.0, -13.0, 68.0, -13.0);
    beamGrad.setColorAt(0.0, QColor(255, 245, 175, 115));
    beamGrad.setColorAt(1.0, QColor(255, 245, 175, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(beamGrad);
    p.drawPolygon(beam);

    // 3) Корпус и кабина мини-снегоуборщика
    p.setPen(QPen(QColor(140, 65, 10), 1.2));
    p.setBrush(QColor(235, 130, 25));
    // Моторный отсек
    p.drawRoundedRect(QRectF(-18.0, -16.0, 34.0, 11.0), 2.5, 2.5);
    // Кабина
    p.drawRoundedRect(QRectF(-12.0, -25.0, 18.0, 11.0), 2.5, 2.5);

    // Окошко кабины
    p.setPen(QPen(QColor(70, 95, 120), 1.0));
    p.setBrush(QColor(175, 225, 250));
    p.drawRoundedRect(QRectF(-8.5, -23.0, 12.0, 6.5), 1.5, 1.5);

    // Выхлопная труба
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(75, 80, 90));
    p.drawRect(QRectF(-16.0, -23.0, 3.0, 7.5));

    // 4) Изогнутый металлический отвал спереди
    p.setPen(QPen(QColor(80, 90, 105), 2.2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(15.0, -9.0), QPointF(24.0, -7.0));

    QPainterPath blade;
    blade.moveTo(22.0, -18.0);
    blade.quadTo(28.5, -9.0, 24.5, -1.0);
    blade.lineTo(21.0, -1.0);
    blade.quadTo(25.0, -9.0, 19.0, -18.0);
    blade.closeSubpath();
    p.setPen(QPen(QColor(70, 78, 92), 1.0));
    p.setBrush(QColor(165, 178, 195));
    p.drawPath(blade);

    // 5) Проблесковый маячок на крыше
    const double glow = 0.5 + 0.5 * std::sin(phase);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 195, 30, static_cast<int>(90 + 140 * glow)));
    p.drawEllipse(QPointF(-3.0, -27.5), 4.5 + 2.5 * glow, 4.5 + 2.5 * glow);
    p.setBrush(QColor(255, 220, 60));
    p.drawRoundedRect(QRectF(-5.0, -28.0, 4.0, 3.2), 1.2, 1.2);

    // 6) Колёса с вращающимися спицами
    const double wheelX[2] = {-10.0, 9.0};
    for (double wx : wheelX) {
        p.save();
        p.translate(wx, -5.5);
        p.rotate(angle * 57.2957795);
        p.setPen(QPen(QColor(30, 32, 38), 1.4));
        p.setBrush(QColor(50, 54, 62));
        p.drawEllipse(QPointF(0, 0), 5.5, 5.5);
        p.setPen(QPen(QColor(180, 190, 205), 1.2));
        p.drawLine(QPointF(-3.2, 0), QPointF(3.2, 0));
        p.drawLine(QPointF(0, -3.2), QPointF(0, 3.2));
        p.restore();
    }

    p.restore();
}

void drawGarland(QPainter &p, const QRect &frame, const QRegion &exposed)
{
    constexpr int colors = 5;
    static const QColor palette[colors] = {{255, 113, 88}, {255, 210, 103}, {112, 210, 160},
                                         {125, 190, 255}, {214, 155, 250}};
    const double left = frame.left();
    const double width = frame.width() - 1;
    if (width <= 0)
        return;
    const int sections = qMax(1, qRound(width / 108));
    const double span = width / sections;
    const double top = frame.top() + 3;
    const QRect damage = exposed.boundingRect();

    p.save();
    p.setClipRegion(exposed, Qt::IntersectClip);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(25, 54, 44, 230), 1.4, Qt::SolidLine, Qt::RoundCap));
    QPainterPath wire;
    const int first = qMax(0, qFloor((damage.left() - left - 9) / span));
    const int last = qMin(sections - 1, qFloor((damage.right() - left + 9) / span));
    for (int section = first; section <= last; ++section) {
        const double x = left + section * span;
        wire.moveTo(x, top);
        wire.quadTo(x + span / 2, top + 12, x + span, top);
    }
    p.drawPath(wire);

    for (int section = first; section <= last; ++section) {
        const int bulbs = qMax(1, qRound(span / 19));
        for (int i = 0; i <= bulbs; ++i) {
            if (i == 0 && section > 0)
                continue;
            const double t = double(i) / bulbs;
            const QPointF bulb(left + (section + t) * span, top + 24 * t * (1 - t) + 3);
            if (!exposed.intersects(QRectF(bulb.x() - 7, bulb.y() - 7, 14, 14).toAlignedRect()))
                continue;
            QColor tint = palette[(section * bulbs + i) % colors];
            QColor glow = tint;
            glow.setAlpha(150);
            QRadialGradient halo(bulb, 7);
            halo.setColorAt(0, glow);
            glow.setAlpha(0);
            halo.setColorAt(1, glow);
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(bulb, 7, 7);
            p.setBrush(QColor(34, 62, 49));
            p.drawRoundedRect(QRectF(bulb.x() - 1.3, bulb.y() - 3.7, 2.6, 2.5), .6, .6);
            p.setBrush(tint);
            p.drawEllipse(bulb, 2, 2.8);
            p.setBrush(QColor(255, 255, 245, 230));
            p.drawEllipse(bulb - QPointF(.45, .75), .75, 1.2);
        }
    }
    p.restore();
}

} // namespace SnowProps
