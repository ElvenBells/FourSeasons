#pragma once

#include "SnowCrystal.h"
#include "SnowSurface.h"

#include <QImage>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QSize>

#include <array>
#include <deque>
#include <list>
#include <optional>
#include <vector>

class QPainter;

class SnowScene
{
public:
    void setLayout(const QSize &size, const QRect &dock, const QRect &activeWindow,
                   const QList<SnowSurface> &windows = {}, bool activeFullscreen = false);
    bool settleSnow(const QPointF &from, const QPointF &to, double amount, bool catchOnTree = true, bool keepFalling = false);
    void clearShedSnow();
    QRegion advance(double dt, double wind = 0);
    QRegion advanceGarlands(double dt);
    bool garlandsPending() const;
    using Garlands = QList<QPair<QRect, QRegion>>;
    Garlands garlandsAbove(const QString &windowId) const;
    void paint(QPainter &painter, const QRegion &repaint = {}) const;
    QRegion visualRegion() const;
    QRegion setGarlandsEnabled(bool enabled);
    QRegion inputRegion() const;

    bool press(const QPointF &point, double time);
    void move(const QPointF &point, double time);
    bool release(const QPointF &point, double time);
    void cancelDrag();
    bool dragging() const { return m_held != nullptr; }
    void hover(const QPointF &point);
    void hoverWindows(const QPointF &point);
    void leaveWindows() { m_windowHoverPoint.reset(); }
    void leave() { m_hoverPoint.reset(); }
    bool aiming() const { return m_aim.has_value(); }
    bool fireAt(const QPointF &point);
    void cancelAim();
    bool breakSnow(const QPointF &point);
    void startPlow();
    void setSecondPlowEnabled(bool enabled);
    void setPlowCadence(double snowSpeed);

private:
    struct Bank {
        explicit Bank(double depth = 28, bool taper = false) : maxDepth(depth), tapered(taper) {}

        QRect surface;
        std::vector<double> snow;
        std::vector<double> capacity;
        std::vector<double> pending;
        double maxDepth;
        bool tapered;

        void setSurface(const QRect &rect);
        int column(double x) const;
        double center(int column) const;
        double heightAt(double x) const;
        QRect bounds() const;
        bool hasSnow() const;
        QRegion inputRegion() const;
        double collect(const QPointF &point, double limit);
        void add(double x, double amount, int radius = 1, bool gradual = false);
        QRect advanceDeposits(double dt);
        void paint(QPainter &painter) const;
        void relax(int first, int last);
        void invalidate()
        {
            outlineDirty = true;
            imageDirty = true;
        }

    private:
        void updateOutline() const;
        void paintWindowSnow(QPainter &painter) const;
        void rasterizeGround(const QRect &band, qreal scale) const;
        double slope(int column) const;

        mutable QPainterPath outline;
        mutable QPainterPath ridge;
        mutable QRegion hitRegion;
        mutable double peak = 0;
        mutable bool outlineDirty = true;
        mutable QImage image;
        mutable bool imageDirty = true;
    };

    struct WindowBank {
        SnowSurface window;
        Bank bank;
        QRegion visible;
        QRegion garland;
        // Даём KWin закончить анимацию появления окна.
        static constexpr double initialRevealDelay = .25;
        double revealDelay = initialRevealDelay;
    };

    struct Ball {
        QPointF center;
        QPointF velocity;
        double mass = 0;
        Ball *support = nullptr;
        Ball *top = nullptr;
        bool resting = false;
        double celebration = -1;

        double radius() const;
        QRect bounds() const;
        QPointF snapPoint(const Ball &base) const;
        bool isHead() const;
        const Ball *root() const;
        bool inSnowman() const;
    };

    struct MouseSample {
        QPointF point;
        double time;
    };

    struct Flight {
        QPointF start;
        QPointF destination;
        double radius;
        double age = 0;

        QPointF center() const;
        double visibleRadius() const;
    };

    struct Splat {
        QPointF impact;
        double radius;
        std::array<double, 20> edge;
        double age = 0;

        QPointF center() const;
        double slideTime() const;
    };

    struct Particle {
        QPointF center;
        QPointF velocity;
        double radius;
        double age = 0;
        bool flake = false;
        SnowCrystal::Glyph glyph = SnowCrystal::Glyph::Asterisk;
        double snowMass = 0;
        double life = 5;
        bool fromTree = false;

        QRect bounds() const;
    };

    struct Fragment {
        QPointF center;
        QPointF velocity;
        double radius;
        double mass;
        double spin;
        double angle;
        double age = 0;
        bool bounced = false;

        QRect bounds() const;
    };

    struct Aim {
        QPointF origin;
        QPointF cursor;
        Bank *bank;
        double mass;
    };

    struct Plow {
        bool active = false;
        int direction = -1;
        double x = 0;
        double y = 0;
        double collectedSnow = 0;
        double angle = 0;
        double phase = 0;
        double untilSpray = 0;
        double untilSmoke = 0;

        QRect bounds() const;
    };

    struct Smoke {
        QPointF center;
        double age = 0;
    };

    Ball *snapTarget(const QPointF &point);
    void attach(Ball &base);
    double surfaceY(const Ball &ball) const;
    void fall(Ball &ball, double dt);
    void recordPointer(const QPointF &point, double time);
    QPointF gestureVelocity() const;
    void collectAlong(const QPointF &from, const QPointF &to);
    void updatePlows(double dt);
    void startPlow(Plow &plow, int direction);
    void advancePlow(Plow &plow, double dt);
    void shatter(const Ball &base, Plow *plow = nullptr);
    void spray(const QPointF &center, double speedX, double amount, bool plow);
    std::optional<QPointF> hoverTravel(const QPointF &point, std::optional<QPointF> &previous);
    bool disturbBank(Bank &bank, const QPointF &point, const QPointF &travel);
    void impact(const Flight &flight);
    void scatter(const QPointF &center, double radius, bool flakes);
    void crumbleOldest();
    void removeHeldBall();
    void updateWindowBanks(const QList<SnowSurface> &windows);
    void updateTreeLayout();
    bool shakeTree(const QPointF &point);
    QTransform treeBend() const;
    QRect treeBounds() const;
    void moveBank(Bank &bank, const QRect &surface, const QPoint &motion);
    void shedSnow(double x, int y, double amount, const QPoint &motion, bool fromTree = false);
    QRegion floorRegion() const;
    QRegion animatedRegion() const;

    QSize m_size;
    Bank m_floor{24};
    Bank m_dock{42};
    QRect m_treeFrame;
    QPainterPath m_treeCanopy;
    mutable QRegion m_treeInput;
    mutable bool m_treeInputDirty = true;
    struct BranchPocket {
        QPointF center;
        double reach, slope, capacity, load = 0, offset = 0;
        double crest(double x) const;
        QPainterPath snowShape() const;
    };
    std::vector<BranchPocket> m_treeBranches;
    double m_treeShake = 0;
    double m_treeBlinkTime = 0;
    bool m_treeAlternateLights = false;
    std::vector<WindowBank> m_windows;
    QList<SnowSurface> m_layoutWindows;
    bool m_garlandsEnabled = true;
    std::list<Ball> m_balls;
    Ball *m_held = nullptr;
    Bank *m_collecting = nullptr;
    QPointF m_pointer;
    QPointF m_grabOffset;
    QPointF m_pressPoint;
    bool m_dragMoved = false;
    double m_collectionX = 0;
    std::deque<MouseSample> m_samples;
    std::optional<Aim> m_aim;
    std::optional<QPointF> m_hoverPoint;
    std::optional<QPointF> m_windowHoverPoint;
    double m_hoverCooldown = 0;
    std::array<Plow, 2> m_plows;
    bool m_secondPlowEnabled = false;
    std::optional<double> m_followupDelay;
    double m_untilNextPlow = 48;
    double m_plowCadence = 1;
    std::vector<Smoke> m_smoke;
    std::vector<Flight> m_flights;
    std::deque<Splat> m_splats;
    std::vector<Particle> m_particles;
    std::vector<Fragment> m_fragments;
    std::size_t m_shedParticles = 0;
    std::size_t m_sprayParticles = 0;
    QRegion m_dirty;
    bool deposit(const QPointF &from, const QPointF &to, double amount, bool catchOnTree, bool gradual, bool keepFalling = false);
};
