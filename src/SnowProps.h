#pragma once

class QPainter;
class QPainterPath;
class QPointF;
class QRect;
class QRegion;

namespace SnowProps {

struct Ball {
    double x;
    double y;
    double radius;
};

void drawSnowman(QPainter &p, const Ball &base, const Ball &torso, const Ball &head, double progress);
void drawSnowball(QPainter &p, const QPointF &center, double radius, bool onGround = false);
void drawPlow(QPainter &p, double x, double y, int direction, double pushedSnow, double angle, double phase);
void drawGarland(QPainter &p, const QRect &frame, const QRegion &exposed);
QPainterPath treeCanopy(const QRect &frame);
void drawTree(QPainter &p, const QRect &frame);
void drawTreeLights(QPainter &p, const QRect &frame, bool alternate);

} // namespace SnowProps
