#pragma once

#include "Season.h"

#include <QPointF>
#include <QRegion>
#include <QSize>

#include <optional>
#include <vector>

class QPainter;

class SeasonalScenery
{
public:
    void setSeason(Season season);
    void setLayout(const QSize &size, const QRect &dock);
    QRegion advance(double dt, double wind, double rain);
    QRegion visualRegion() const;
    void paint(QPainter &p, const QRegion &clip) const;

    double groundAt(double x) const;
    bool inPuddle(double x) const;
    bool night() const { return m_night; }
    bool plant(double x);
    bool water(double x);
    bool launchBoat();
    QRegion setFan(bool enabled);
    void steerInsects(const QPointF &p, const QPointF &motion);

private:
    struct Flower { double x, growth; int type; };
    struct Bee { QPointF pos; int target = 0; double pause = 0; };
    QRectF puddle(int index) const;
    QRectF umbrellaBounds() const;
    QRectF lampBounds() const;
    QRectF fanBounds() const;
    QRectF raysBounds() const;
    QRectF mirageBounds(int patch) const;
    QRectF flowerBounds(const Flower &flower) const;
    QRectF insectBounds(const QPointF &pos) const;
    QPointF flowerTip(const Flower &flower) const;
    void populate();
    void paintPlants(QPainter &p, const QRegion &clip) const;
    void paintAutumnProps(QPainter &p, const QRegion &clip) const;
    void paintSummerLight(QPainter &p, const QRegion &clip) const;
    void paintInsects(QPainter &p, const QRegion &clip) const;

    QSize m_size;
    QRect m_dock;
    Season m_season = Season::Winter;
    std::vector<Flower> m_flowers;
    std::vector<Bee> m_bees;
    std::optional<QPointF> m_butterfly;
    double m_butterflyDelay = 7;
    double m_butterflyAge = 0;
    double m_butterflyX = 0;
    double m_butterflyOffset = 0;
    std::optional<double> m_hedgehog;
    double m_hedgehogDelay = 12;
    double m_time = 0;
    double m_wetness = 0;
    double m_canopy = 0;
    double m_wind = 0;
    double m_lastGroundFrame = 0;
    double m_pollination = 0;
    double m_lightCheck = 0;
    double m_boatAge = 0;
    int m_boatPuddle = -1;
    double m_flashDelay = 32;
    double m_flash = 0;
    bool m_fan = false;
    bool m_night = false;
};
