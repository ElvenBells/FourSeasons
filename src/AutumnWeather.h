#pragma once

#include "Season.h"

#include <QLineF>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QSize>

#include <vector>

class QPainter;

// Осенняя «погода»: дождь и листопад. Не зависит от SnowScene — только от размера
// оверлея и положения дока, поэтому работает и тогда, когда мышь не поддерживается.
//
// Контракт тот же, что у снежинок в SnowWindow: advance() двигает объекты и
// возвращает область, которую надо перерисовать; paint() рисует только то, что
// попадает в переданный clip (оверлей уже вычел из него «дырку» активного окна).
class AutumnWeather
{
public:
    void setSize(const QSize &size);
    // Док в координатах оверлея (пустой QRect — дока нет). Капли и листья «приземляются» на него.
    void setGround(const QRect &dock);
    void setOptions(const AutumnOptions &options);
    AutumnOptions options() const { return m_options; }

    // hole — ограничивающий прямоугольник активного окна: то, что целиком внутри него, не перерисовываем.
    QRegion advance(double dt, const QRect &hole);
    void paint(QPainter &painter, const QRegion &clip) const;
    // Области объектов, пересекающиеся с area (нужно при изменении «дырки»).
    QRegion overlapping(const QRegion &area) const;

private:
    struct Drop {
        double x = 0;
        double y = 0;          // нижний конец капли (голова)
        double speed = 600;    // px/с
        double length = 10;    // длина штриха
        double windFactor = 1;
        int band = 0;          // «глубина»: определяет толщину и прозрачность
        QRect rect;
    };

    struct Splash {
        QPointF origin;
        double age = 0;
        double sideSpeed = 20;
        double upSpeed = 70;
    };

    struct Leaf {
        double x = 0;
        double y = 0;
        double baseX = 0;      // «ось», вокруг которой качается лист
        double size = 10;      // половина длины листа, px
        double speed = 40;
        double swayAmp = 30;
        double swayFreq = 1;
        double phase = 0;
        double windFactor = 2;
        double angle = 0;      // медленно дрейфующий поворот, рад
        double spin = 0;       // рад/с
        double flip = 0;       // фаза «переворота» листа (имитация 3D)
        double flipSpeed = 2;
        double opacity = 1;
        int shape = 0;
        int color = 0;
        QRect rect;
    };

    void populateRain();
    void populateLeaves();
    void respawn(Drop &drop, bool initial);
    void respawn(Leaf &leaf, bool initial);
    double groundAt(double x) const;
    QLineF streak(const Drop &drop) const;
    static QRect dropRect(const QLineF &line);
    static QRect leafRect(const Leaf &leaf);
    static QRect splashRect(const Splash &splash);

    QSize m_size;
    QRect m_dock;
    AutumnOptions m_options;
    std::vector<Drop> m_drops;
    std::vector<Splash> m_splashes;
    std::vector<Leaf> m_leaves;
    double m_time = 0;
    double m_rainSlant = 45;   // горизонтальный снос дождя, px/с (меняется с ветром)
};
