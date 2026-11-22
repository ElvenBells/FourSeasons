#include "SnowCrystal.h"

#include <QImage>
#include <QPainter>

#include <array>
#include <cmath>

namespace SnowCrystal {
namespace {

constexpr int glyphSize = 5;
constexpr int scales = 3;
constexpr int levels = 16;

using GlyphPattern = std::array<const char *, glyphSize>;

constexpr std::array<GlyphPattern, glyphCount> patterns{{
    {"#.#.#", ".###.", "..#..", ".###.", "#.#.#"}, // *
    {"..#..", "..#..", "#####", "..#..", "..#.."}, // +
    {"#...#", ".#.#.", "..#..", ".#.#.", "#...#"}, // x
}};

using Shades = std::array<QImage, levels>;
constexpr std::array naturalSizes{5, 7, 9, 11};

int naturalIndex(double radius)
{
    return qBound(0, qRound(radius - 2), static_cast<int>(naturalSizes.size()) - 1);
}

int dotSize(double radius)
{
    return radius >= 1.65 ? 2 : 1;
}

int glyphScale(double radius)
{
    return radius >= 8 ? 3 : radius >= 5.4 ? 2 : 1;
}

QPoint spritePosition(const QPointF &center, int size)
{
    return QPoint(qRound(center.x()) - size / 2, qRound(center.y()) - size / 2);
}

QImage makeSprite(int scale, int level, const GlyphPattern *pattern)
{
    const int size = pattern ? glyphSize * scale : scale;
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const QRgb ink = qPremultiply(qRgba(235, 247, 255, level * 255 / (levels - 1)));
    for (int y = 0; y < size; ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < size; ++x) {
            if (!pattern || (*pattern)[y / scale][x / scale] == '#')
                row[x] = ink;
        }
    }
    return image;
}

QImage makeNaturalSprite(int size, int variant)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(235, 248, 255, 220), size > 7 ? .9 : .72,
                        Qt::SolidLine, Qt::RoundCap));
    const QPointF center(size / 2.0, size / 2.0);
    const double r = size / 2.0 - .55;
    constexpr double pi = 3.141592653589793;
    for (int i = 0; i < 6; ++i) {
        const double angle = i * pi / 3 + variant * .21;
        const QPointF arm(std::cos(angle), std::sin(angle));
        const QPointF normal(-arm.y(), arm.x());
        painter.drawLine(center, center + arm * r);
        const QPointF branch = center + arm * (r * .53);
        const QPointF tip = center + arm * (r * .78);
        painter.drawLine(branch, tip + normal * (r * .23));
        painter.drawLine(branch, tip - normal * (r * .23));
    }
    painter.end();
    return image;
}

struct Sprites {
    std::array<Shades, 2> dots;
    std::array<std::array<Shades, scales>, glyphCount> shapes;
    std::array<QImage, naturalSizes.size()> natural;

    Sprites()
    {
        for (std::size_t i = 0; i < natural.size(); ++i)
            natural[i] = makeNaturalSprite(naturalSizes[i], static_cast<int>(i));
        for (int shade = 1; shade < levels; ++shade) {
            for (int i = 0; i < static_cast<int>(dots.size()); ++i)
                dots[i][shade] = makeSprite(i + 1, shade, nullptr);
            for (int glyph = 0; glyph < glyphCount; ++glyph)
                for (int scale = 1; scale <= scales; ++scale)
                    shapes[glyph][scale - 1][shade] = makeSprite(scale, shade, &patterns[glyph]);
        }
    }
};

const Sprites &sprites()
{
    static const Sprites cache;
    return cache;
}

} // namespace

void prepareSprites()
{
    sprites();
}

QRect bounds(const QPointF &center, double radius)
{
    const int size = radius < 2.2 ? dotSize(radius) : glyphSize * glyphScale(radius);
    return QRect(spritePosition(center, size), QSize(size, size));
}

void paint(QPainter &p, const QPointF &center, double radius, double opacity, Glyph glyph)
{
    const int shade = qBound(0, qRound(opacity * (levels - 1)), levels - 1);
    if (shade == 0)
        return;

    const Sprites &cache = sprites();
    const QImage &image = radius < 2.2 ? cache.dots[dotSize(radius) - 1][shade]
                                      : cache.shapes[static_cast<int>(glyph)][glyphScale(radius) - 1][shade];
    p.drawImage(spritePosition(center, image.width()), image);
}

QRect naturalBounds(const QPointF &center, double radius)
{
    const int size = naturalSizes[naturalIndex(radius)];
    return QRect(spritePosition(center, size), QSize(size, size));
}

void paintNatural(QPainter &p, const QPointF &center, double radius)
{
    const QImage &sprite = sprites().natural[naturalIndex(radius)];
    p.drawImage(spritePosition(center, sprite.width()), sprite);
}

} // namespace SnowCrystal
