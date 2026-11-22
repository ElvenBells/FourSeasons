#pragma once

#include <QPointF>
#include <QRandomGenerator>
#include <QRegion>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace Precipitation {

struct Settings {
    int intensity = 100;
    int speed = 100;
    int wind = 0;

};

inline constexpr std::size_t denseCount = 320;
inline constexpr double windScale = 1.55;

struct Particle {
    double x = 0, y = 0;
    double baseX = 0;
    double speed = 30;
    double swayAmp = 0;
    double swayFreq = 0;
    double phase = 0;
    double windFactor = 1;
};

inline double windAt(double strength, double time)
{
    return strength * (1 + .15 * std::sin(time * .73));
}

inline void stage(Particle &particle, double clearance, int height, int index, int count)
{
    const double offset = (index + QRandomGenerator::global()->generateDouble()) / count;
    particle.y = -clearance - height * offset;
}

inline bool fall(Particle &particle, double dt, double speed, double wind, double width, double margin)
{
    particle.y += particle.speed * speed * dt;
    particle.baseX += wind * particle.windFactor * dt;
    particle.phase += particle.swayFreq * dt;
    const bool wrapped = particle.baseX < -margin || particle.baseX > width + margin;
    if (wrapped) {
        const double span = width + 2 * margin;
        particle.baseX = std::fmod(particle.baseX + margin, span);
        if (particle.baseX < 0)
            particle.baseX += span;
        particle.baseX -= margin;
    }
    particle.x = particle.baseX + std::sin(particle.phase) * particle.swayAmp;
    return wrapped;
}

inline std::optional<QPointF> crossing(const QPointF &from, const QPointF &to, const QRect &ledge)
{
    const double y = ledge.y();
    if (ledge.isEmpty() || from.y() > y || to.y() < y || to.y() <= from.y())
        return std::nullopt;
    const double x = from.x() + (to.x() - from.x()) * ((y - from.y()) / (to.y() - from.y()));
    if (x < ledge.left() || x >= ledge.right() + 1)
        return std::nullopt;
    return QPointF(x, y);
}

// При плотном потоке осадков отмечаем блоки, чтобы не складывать сотни мелких QRegion.
class DamageTiles
{
public:
    explicit DamageTiles(const QSize &size, bool enabled)
        : m_size(size),
          m_columns(enabled ? (size.width() + tileWidth - 1) / tileWidth : 0),
          m_rows(enabled ? (size.height() + tileHeight - 1) / tileHeight : 0),
          m_tiles(m_columns * m_rows)
    {
    }

    void mark(const QRect &area)
    {
        const int first = area.left() / tileWidth;
        const int last = area.right() / tileWidth;
        for (int y = area.top() / tileHeight; y <= area.bottom() / tileHeight; ++y)
            std::fill_n(m_tiles.begin() + y * m_columns + first, last - first + 1, true);
    }

    QRegion region() const
    {
        // setRects получает непересекающиеся прямоугольники, упорядоченные по строкам.
        std::vector<QRect> bands;
        for (int row = 0; row < m_rows; ++row) {
            const int y = row * tileHeight;
            int first = -1;
            for (int column = 0; column <= m_columns; ++column) {
                const bool marked = column < m_columns && m_tiles[row * m_columns + column];
                if (marked && first < 0)
                    first = column;
                if (!marked && first >= 0) {
                    const int x = first * tileWidth;
                    bands.emplace_back(x, y, qMin(m_size.width(), column * tileWidth) - x,
                                       qMin(tileHeight, m_size.height() - y));
                    first = -1;
                }
            }
        }
        QRegion result;
        if (!bands.empty())
            result.setRects(bands.data(), static_cast<int>(bands.size()));
        return result;
    }

private:
    static constexpr int tileWidth = 48;
    static constexpr int tileHeight = 32;
    QSize m_size;
    int m_columns;
    int m_rows;
    std::vector<unsigned char> m_tiles;
};

} // namespace Precipitation
