#pragma once

class QPainter;
class QPointF;
class QRect;

namespace SnowCrystal {

enum class Glyph { Asterisk, Plus, Cross };
constexpr int glyphCount = 3;

void prepareSprites();
QRect bounds(const QPointF &center, double radius);
void paint(QPainter &painter, const QPointF &center, double radius, double opacity, Glyph glyph);
QRect naturalBounds(const QPointF &center, double radius);
void paintNatural(QPainter &painter, const QPointF &center, double radius);

} // namespace SnowCrystal
