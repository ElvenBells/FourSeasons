#pragma once

class QPainter;

namespace SnowProps {

struct Ball {
    double x;
    double y;
    double radius;
};

void drawSnowman(QPainter &p, const Ball &base, const Ball &torso, const Ball &head, double progress);
void drawPlow(QPainter &p, double x, double y, int direction, double pushedSnow, double angle, double phase);

} // namespace SnowProps
