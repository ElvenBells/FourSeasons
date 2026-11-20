#include "SnowProps.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace SnowProps {

void drawSnowman(QPainter &p, const Ball &base, const Ball &torso, const Ball &head, double progress)
{
    const double t = qBound(0.0, progress, 1.0);
    if (t <= 0.01)
        return;

    p.save();

    // 1) Ручки-веточки по бокам среднего кома (туловища)
    const double armScale = qBound(0.0, (t - 0.1) / 0.5, 1.0);
    if (armScale > 0.0) {
        p.setPen(QPen(QColor(92, 60, 36), 2.4, Qt::SolidLine, Qt::RoundCap));
        const double lx = torso.x - torso.radius * 0.85;
        const double rx = torso.x + torso.radius * 0.85;
        const double ay = torso.y - torso.radius * 0.15;
        const double len = 16.0 * armScale;

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
        const double br = 2.3 * btnScale;
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
            QRectF tail(head.x + sw * 0.35, neckY + 1.0, 6.5 * scarfScale, 14.0 * scarfScale);
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
        const double er = 2.1 * faceScale;

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
            const double sx = head.x + i * 2.4;
            const double sy = head.y + head.radius * 0.34 - (i * i) * 0.38;
            p.drawEllipse(QPointF(sx, sy), 1.1 * faceScale, 1.1 * faceScale);
        }
    }

    // 5) Нос-морковка
    const double noseScale = qBound(0.0, (t - 0.20) / 0.45, 1.0);
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
            p.scale(hatScale, hatScale);

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
        const double r = qBound(4.0, 4.0 + pushedSnow * 0.35, 15.0);
        p.setPen(QPen(QColor(210, 228, 248), 1.0));
        p.setBrush(QColor(246, 251, 255));
        p.drawEllipse(QPointF(27.0 + r * 0.35, -r * 0.72), r * 0.9, r * 0.75);
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

} // namespace SnowProps
