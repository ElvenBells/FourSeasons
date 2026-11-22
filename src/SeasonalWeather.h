#pragma once

#include "Precipitation.h"
#include "Season.h"
#include "SnowSurface.h"
#include "SeasonalScenery.h"

#include <QLineF>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QSize>

#include <vector>

class QPainter;

// Дождь, листья, лепестки и светлячки зависят от размера оверлея и положения дока.
// advance() возвращает область перерисовки. SnowWindow вычитает активное окно
// из области перед вызовом paint().
class SeasonalWeather
{
public:
    void setSize(const QSize &size);
    void setSeason(Season season);
    void restart();
    // Док в координатах оверлея. Пустой QRect означает нижний край экрана.
    void setGround(const QRect &dock);
    void setWindows(const QList<SnowSurface> &windows);
    void setPrecipitation(const Precipitation::Settings &settings);
    void paintWindowLeaves(QPainter &painter, const QRegion &clip) const;
    void paintLeafOverlays(QPainter &painter, const QRegion &clip) const;
    bool densePrecipitation() const { return m_leaves.size() + m_drops.size() >= Precipitation::denseCount; }
    void setOptions(const AutumnOptions &options);
    AutumnOptions options() const { return m_options; }
    void setSpringRain(bool enabled);
    void setBubbles(bool enabled);
    void setTool(SeasonTool tool) { m_tool = tool; }
    SeasonTool tool() const { return m_tool; }
    void setFan(bool enabled) { m_damage += m_scenery.setFan(enabled); }
    void setJar(bool enabled);
    void startBlower();
    bool launchBoat();
    bool press(const QPointF &position);
    bool breakPile(const QPointF &position);
    void movePointer(const QPointF &position, bool held);
    void leavePointer() { m_pointer.reset(); }
    QRegion inputRegion() const;
    QRegion pileInputRegion() const;

    // hole скрывает летящие частицы над активным окном. Осевшие листья видны на его рамке.
    QRegion advance(double dt, const QRect &hole);
    void paint(QPainter &painter, const QRegion &clip) const;
    // Области объектов, пересекающиеся с area (нужно при изменении «дырки»).
    QRegion overlapping(const QRegion &area) const;

private:
    struct Drop : Precipitation::Particle {
        double length = 10;    // длина штриха
        QPoint streak;
        int band = 0;          // «глубина»: определяет толщину и прозрачность
        QRect rect;
    };

    struct Splash {
        QPointF origin;
        double age = 0;
        double sideSpeed = 20;
        double upSpeed = 70;
        bool ring = false;
    };

    struct Leaf : Precipitation::Particle {
        double size = 10;
        double angle = 0;      // медленно дрейфующий поворот, рад
        double spin = 0;       // рад/с
        double flip = 0;       // фаза «переворота» листа (имитация 3D)
        double flipSpeed = 2;
        double opacity = 1;
        int shape = 0;
        int color = 0;
        bool wet = false;
        bool dry = false;
        QRect rect;
    };

    struct GroundPile {
        QPointF base;
        std::vector<Leaf> leaves;
        QRect bounds;
    };

    struct WindowLedge {
        SnowSurface surface;
        QRect top;
        QRegion exposed;
        std::vector<Leaf> leaves;
    };

    struct ScatteredLeaf {
        Leaf leaf;
        QPointF velocity;
        double life;
        bool clear;
    };

    struct Firefly {
        double x = 0;
        double y = 0;
        double speed = 0;
        double phase = 0;
        double boost = 0;
        QPointF impulse;
    };

    struct Bubble { QPointF pos; double radius, speed, phase; };
    struct Impact { QPointF pos; double age; bool bubble; };
    struct Flight { QPointF start, target, pos; double age = 0; };

    void populateRain();
    void populateFireflies();
    void populateLeaves();
    void respawn(Drop &drop);
    void respawn(Leaf &leaf);
    void respawn(Firefly &fly);
    QPointF fireflyPosition(const Firefly &fly) const;
    QRect fireflyRect(const Firefly &fly) const;
    void settle(Leaf leaf);
    void settle(Leaf leaf, std::vector<Leaf> &pile, double ground, std::size_t limit, bool floats);
    void disperseWindowLeaves(const WindowLedge &window);
    std::optional<QPointF> groundContact(const QPointF &from, const QPointF &to) const;
    void layoutPile(GroundPile &pile);
    void burstPile(GroundPile &pile, bool clear);
    void clearPilesAlong(const QPointF &from, const QPointF &to, double radius);
    bool scatterNear(std::vector<Leaf> &leaves, const QPointF &from, const QPointF &to,
                     double radius, double push, bool clear = false, const QRegion *exposed = nullptr);
    void applyTool(const QPointF &position);
    QRegion advanceGround(double dt, double wind);
    QRegion advanceExtras(double dt);
    QRegion extraVisualRegion() const;
    void paintLeaf(QPainter &p, const Leaf &leaf, bool onGround) const;
    void paintExtras(QPainter &p, const QRegion &clip) const;
    QPointF launcher() const;
    QPointF jarPosition() const;
    QRect mistRect() const;
    double rainStrength() const;
    double groundAt(double x) const;
    void updateDropBounds(Drop &drop) const;
    static QRect leafRect(const Leaf &leaf);
    static QRect splashRect(const Splash &splash);

    QSize m_size;
    QRect m_dock;
    Season m_season = Season::Winter;
    AutumnOptions m_options;
    SeasonalScenery m_scenery;
    SeasonTool m_tool = SeasonTool::Passive;
    QList<SnowSurface> m_windowLayout;
    std::vector<WindowLedge> m_windows;
    Precipitation::Settings m_precipitation;
    std::vector<Drop> m_drops;
    std::vector<Splash> m_splashes;
    std::vector<Leaf> m_leaves;
    std::vector<Leaf> m_groundLeaves;
    std::vector<GroundPile> m_groundPiles;
    std::vector<ScatteredLeaf> m_scattered;
    std::vector<Firefly> m_fireflies;
    std::vector<Bubble> m_bubbles;
    std::vector<Impact> m_impacts;
    std::optional<Flight> m_flight;
    std::optional<QPointF> m_pointer;
    std::optional<double> m_blower;
    QRegion m_damage;
    double m_time = 0;
    double m_bubbleDelay = 0;
    double m_afterRain = 0;
    bool m_springRain = false;
    bool m_bubblesEnabled = false;
    bool m_jar = false;
    int m_caught = 0;
    double m_rainSlant = 45;   // горизонтальный снос дождя, px/с (меняется с ветром)
};
