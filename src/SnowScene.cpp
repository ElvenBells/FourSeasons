#include "SnowScene.h"
#include "SnowProps.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr int columnWidth = 8;
constexpr double smallSnow = 8;
constexpr double mediumSnow = 48;
constexpr double largeSnow = 96;
constexpr double snapDistance = 18;
constexpr double gestureDistance = 6;
constexpr double aimedRadius = 9;
constexpr double plowSpeed = 185;
constexpr std::size_t maxSprayParticles = 36;
constexpr double flightDuration = 0.25;
constexpr double splatHold = 1.5;
constexpr double splatSlide = 2.5;
constexpr double slideSpeed = 18;
constexpr double gravity = 960;
constexpr std::size_t maxSplats = 6;
constexpr std::size_t maxBalls = 30;
constexpr std::size_t maxShedParticles = 72;
constexpr int particlesPerDrift = 24;
constexpr double twoPi = 6.283185307179586;

double random(double low, double high)
{
    return low + (high - low) * QRandomGenerator::global()->generateDouble();
}

double length(const QPointF &v)
{
    return std::hypot(v.x(), v.y());
}

QRect around(const QPointF &center, double radius)
{
    return QRectF(center.x() - radius, center.y() - radius, 2 * radius, 2 * radius).toAlignedRect();
}

QPointF polar(double radius, double angle)
{
    return {radius * std::cos(angle), radius * std::sin(angle)};
}

void drawSnowball(QPainter &p, const QPointF &center, double radius, bool onGround = false)
{
    if (onGround) {
        const QPointF shadowCenter = center + QPointF(0, radius * .85);
        QRadialGradient shadow(shadowCenter, radius * .95);
        shadow.setColorAt(0, QColor(30, 50, 80, 95));
        shadow.setColorAt(1, QColor(30, 50, 80, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(shadow);
        p.drawEllipse(shadowCenter, radius * .9, radius * .28);
    }
    QRadialGradient color(center + QPointF(-radius * .32, -radius * .32), radius * 1.35);
    color.setColorAt(0, QColor(255, 255, 255));
    color.setColorAt(.65, QColor(234, 244, 255));
    color.setColorAt(1, QColor(186, 210, 238));
    p.setBrush(color);
    p.setPen(QPen(QColor(170, 198, 228, 200), 1.1));
    p.drawEllipse(center, radius, radius);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(200, 220, 245, 160));
    const double grain = radius / 19;
    p.drawEllipse(center + QPointF(radius * .33, radius * .11), 2 * grain, 1.5 * grain);
    p.drawEllipse(center + QPointF(-radius * .275, radius * .2475), 1.8 * grain, 1.8 * grain);
    p.drawEllipse(center + QPointF(-radius * .11, -radius * .3025), 2.2 * grain, 1.6 * grain);
}
} // namespace

void SnowScene::Bank::setSurface(const QRect &rect, double startingSnow)
{
    if (surface == rect)
        return;
    const QRect oldSurface = surface;
    const auto oldSnow = std::move(snow);
    surface = rect;
    snow.assign((rect.width() + columnWidth - 1) / columnWidth, 0);
    for (int i = 0; i < static_cast<int>(snow.size()); ++i) {
        const double x = center(i);
        const int oldColumn = static_cast<int>((x - oldSurface.x()) / columnWidth);
        if (oldSurface.left() <= x && x < oldSurface.right() + 1 && oldColumn < static_cast<int>(oldSnow.size())) {
            snow[i] = oldSnow[oldColumn];
        } else if (startingSnow > 0) {
            const double localX = x - rect.x();
            snow[i] = startingSnow * (1 + .23 * std::sin(localX * .025) + .14 * std::cos(localX * .067));
        }
    }
    invalidate();
}

int SnowScene::Bank::column(double x) const
{
    if (x < surface.x() || x >= surface.x() + surface.width() || snow.empty())
        return -1;
    return static_cast<int>((x - surface.x()) / columnWidth);
}

double SnowScene::Bank::center(int index) const
{
    const int left = surface.x() + index * columnWidth;
    return left + qMin(columnWidth, surface.right() + 1 - left) / 2.0;
}

double SnowScene::Bank::heightAt(double x) const
{
    const int index = column(x);
    if (index < 0)
        return 0;
    const int left = x < center(index) ? index - 1 : index;
    const int right = left + 1;
    const double x0 = left < 0 ? surface.left() : center(left);
    const bool last = right == static_cast<int>(snow.size());
    const double x1 = last ? surface.right() + 1 : center(right);
    const double h0 = left < 0 ? 0 : snow[left];
    const double h1 = last ? 0 : snow[right];
    const double t = (x - x0) / (x1 - x0);
    return h0 + (h1 - h0) * t * t * (3 - 2 * t);
}

QRect SnowScene::Bank::bounds() const
{
    if (snow.empty())
        return {};
    return {surface.x(), surface.y() - qCeil(maxDepth) - 2, surface.width(), qCeil(maxDepth) + 4};
}

bool SnowScene::Bank::hasSnow() const
{
    return std::any_of(snow.cbegin(), snow.cend(), [](double depth) { return depth >= .25; });
}

void SnowScene::Bank::updateOutline() const
{
    if (!outlineDirty)
        return;
    outlineDirty = false;
    outline = {};
    ridge = {};
    hitRegion = {};
    peak = 0;
    if (snow.empty())
        return;

    const double base = surface.y();
    const double right = surface.right() + 1;
    ridge.moveTo(surface.left(), base);
    QPointF previous(surface.left(), base);
    for (int i = 0; i <= static_cast<int>(snow.size()); ++i) {
        const bool end = i == static_cast<int>(snow.size());
        const double height = end ? 0 : snow[i];
        peak = qMax(peak, height);
        const QPointF point(end ? right : center(i), base - height);
        const double handle = (point.x() - previous.x()) / 3;
        ridge.cubicTo(previous + QPointF(handle, 0), point - QPointF(handle, 0), point);
        previous = point;
    }
    if (peak < .25)
        return;
    outline = ridge;
    outline.closeSubpath();
}

QRegion SnowScene::Bank::inputRegion() const
{
    updateOutline();
    if (peak >= 3 && hitRegion.isEmpty()) {
        hitRegion = QRegion(outline.toFillPolygon().toPolygon()) &
                    QRect(surface.x(), surface.y() - qCeil(maxDepth) - 1, surface.width(), qCeil(maxDepth) + 1);
    }
    return hitRegion;
}

double SnowScene::Bank::collect(const QPointF &point, double limit)
{
    const double depth = surface.y() - point.y();
    const int middle = column(point.x());
    if (middle < 0 || depth < -2 || depth > heightAt(point.x()) + 6)
        return 0;

    double collected = 0;
    for (const int offset : {0, -1, 1}) {
        const int i = middle + offset;
        if (i < 0 || i >= static_cast<int>(snow.size()) || depth > snow[i] + 6)
            continue;
        const double amount = qMin(snow[i], qMin(limit - collected, limit * (offset == 0 ? .6 : .4)));
        snow[i] -= amount;
        collected += amount;
    }
    if (collected > 0) {
        relax(middle - 2, middle + 2);
        invalidate();
    }
    return collected;
}

void SnowScene::Bank::add(double x, double amount, int radius)
{
    const int i = column(x);
    if (i < 0 || amount <= 0)
        return;
    const int first = qMax(0, i - radius);
    const int last = qMin(i + radius, static_cast<int>(snow.size()) - 1);
    const auto weight = [i, radius](int j) {
        return j == i ? 1.0 : .35 * (radius + 1 - std::abs(j - i)) / radius;
    };
    double totalWeight = 0;
    for (int j = first; j <= last; ++j)
        totalWeight += weight(j);
    for (int j = first; j <= last; ++j) {
        snow[j] = qMin(maxDepth, snow[j] + amount * weight(j) / totalWeight);
    }
    relax(first - 1, last + 1);
    invalidate();
}

void SnowScene::Bank::relax(int first, int last)
{
    // Перенос между соседями сохраняет снег и убирает отвесные ступеньки.
    constexpr double maxSlope = 5.5;
    first = qMax(first, 0);
    last = qMin(last, static_cast<int>(snow.size()) - 1);
    const auto balance = [&](int i) {
        const double diff = snow[i] - snow[i + 1];
        const double transfer = (diff - qBound(-maxSlope, diff, maxSlope)) / 2;
        snow[i] -= transfer;
        snow[i + 1] += transfer;
    };
    for (int i = first; i < last; ++i)
        balance(i);
    for (int i = last - 1; i >= first; --i)
        balance(i);
}

void SnowScene::Bank::paint(QPainter &p) const
{
    updateOutline();
    if (outline.isEmpty())
        return;
    const double y = surface.y();
    QLinearGradient tint(0, y - peak, 0, y);
    tint.setColorAt(0, QColor(254, 255, 255, 250));
    tint.setColorAt(.60, QColor(231, 244, 254, 244));
    tint.setColorAt(1, QColor(173, 204, 232, 228));
    p.fillPath(outline, tint);

    p.save();
    p.setClipPath(outline, Qt::IntersectClip);
    p.strokePath(ridge, QPen(QColor(255, 255, 255, 155), 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.restore();
}

double SnowScene::Ball::radius() const
{
    switch (size) {
    case Size::Small: return 11;
    case Size::Medium: return 19;
    case Size::Large: return 28;
    }
    return 11;
}

QRect SnowScene::Ball::bounds() const
{
    return around(center, celebration >= 0 ? 39 : radius() * 1.14 + 3);
}

QPointF SnowScene::Ball::snapPoint(const Ball &base) const
{
    return base.center - QPointF(0, radius() + base.radius() - 3);
}

bool SnowScene::Ball::isHead() const
{
    return size == Size::Small && support && support->support;
}

const SnowScene::Ball *SnowScene::Ball::root() const
{
    const Ball *base = this;
    while (base->support)
        base = base->support;
    return base;
}

bool SnowScene::Ball::inSnowman() const
{
    const Ball *base = root();
    return base->top && base->top->top;
}

QRect SnowScene::Plow::bounds() const
{
    return QRectF(x - 76, y - 38, 152, 41).toAlignedRect();
}

QPointF SnowScene::Flight::center() const
{
    const double progress = qMin(1.0, age / flightDuration);
    return start + (destination - start) * progress - QPointF(0, 240 * progress * (1 - progress));
}

double SnowScene::Flight::visibleRadius() const
{
    return radius * (1 + 1.3 * qMin(1.0, age / flightDuration));
}

QPointF SnowScene::Splat::center() const
{
    return impact + QPointF(0, slideSpeed * slideTime());
}

double SnowScene::Splat::slideTime() const
{
    return qBound(0.0, age - splatHold, splatSlide);
}

void SnowScene::setLayout(const QSize &size, const QRect &dock, const QRect &activeWindow,
                          const QList<SnowSurface> &windows)
{
    const QRect floor(0, size.height(), size.width(), 1);
    const QRect dockSurface = dock.isEmpty() ? QRect() : QRect(dock.x(), dock.y(), dock.width(), 1);
    const bool sizeChanged = m_size != size;
    const bool groundChanged = m_floor.surface != floor || m_dock.surface != dockSurface;
    m_size = size;
    if (groundChanged)
        m_dirty += m_floor.bounds().united(m_dock.bounds());
    m_floor.setSurface(floor, dock.isEmpty() ? 8 : 0);
    if (m_dock.surface != dockSurface) {
        if (m_dock.surface.isEmpty()) {
            m_dock.setSurface(dockSurface, 10);
        } else {
            moveBank(m_dock, dockSurface, dockSurface.isEmpty() ? QPoint() :
                     dockSurface.topLeft() - m_dock.surface.topLeft());
        }
    }

    QList<SnowSurface> layout = windows;
    if (layout.isEmpty() && !activeWindow.isEmpty())
        layout.append({QStringLiteral("active-fallback"), activeWindow});
    if (sizeChanged || layout != m_layoutWindows)
        updateWindowBanks(layout);

    if (groundChanged) {
        m_dirty += m_floor.bounds().united(m_dock.bounds());
        for (Ball &ball : m_balls) {
            m_dirty += ball.bounds();
            if (&ball == m_held || ball.support)
                continue;
            ball.resting = false;
            const double ground = surfaceY(ball) - ball.radius();
            if (ball.center.y() > ground)
                ball.center.setY(ground);
        }
    }
}

void SnowScene::updateWindowBanks(const QList<SnowSurface> &windows)
{
    m_layoutWindows = windows;
    std::vector<WindowBank> next;
    next.reserve(windows.size());
    const QRect screen(QPoint(), m_size);

    // Отсекаем часть сугроба, закрытую окнами выше в стеке.
    QRegion occlusion;
    for (auto it = windows.crbegin(); it != windows.crend(); ++it) {
        const QRect frame = it->rect.intersected(screen);
        if (frame.isEmpty())
            continue;
        const QRect surface = it->rect.top() > 2 ? QRect(frame.x(), frame.y(), frame.width(), 1) : QRect();
        auto old = std::find_if(m_windows.begin(), m_windows.end(), [&](const WindowBank &held) {
            return held.window.id == it->id;
        });
        WindowBank drift{*it, Bank(), {}};
        QRegion oldVisible;
        bool changedProfile = false;
        if (old != m_windows.end()) {
            oldVisible = old->visible;
            drift.bank = std::move(old->bank);
            old->bank = Bank();
            if (drift.bank.surface != surface || old->window.rect.topLeft() != it->rect.topLeft()) {
                changedProfile = drift.bank.hasSnow();
                moveBank(drift.bank, surface, it->rect.topLeft() - old->window.rect.topLeft());
                changedProfile = changedProfile || drift.bank.hasSnow();
            }
        } else {
            drift.bank.setSurface(surface, 0);
        }
        drift.visible = QRegion(drift.bank.bounds() & screen) - occlusion;
        if (changedProfile)
            m_dirty += oldVisible + drift.visible;
        else if (oldVisible != drift.visible && drift.bank.hasSnow())
            m_dirty += oldVisible.xored(drift.visible);
        occlusion += frame;
        next.push_back(std::move(drift));
    }
    for (WindowBank &old : m_windows) {
        if (old.bank.surface.isEmpty())
            continue;
        if (old.bank.hasSnow())
            m_dirty += old.visible;
        moveBank(old.bank, {}, {});
    }
    std::reverse(next.begin(), next.end());
    m_windows = std::move(next);
}

void SnowScene::moveBank(Bank &bank, const QRect &surface, const QPoint &motion)
{
    Bank previous = std::move(bank);
    bank = Bank(previous.maxDepth);
    bank.setSurface(surface, 0);
    const double distance = length(motion);
    const double shed = surface.isEmpty() ? 1 : qBound(0.0, (distance - 4) / 65, .72);
    const int columns = static_cast<int>(previous.snow.size());
    const int batchSize = qMax(1, (columns + particlesPerDrift - 1) / particlesPerDrift);
    double batchMass = 0;
    double weightedX = 0;
    for (int i = 0; i < columns; ++i) {
        const double oldX = previous.center(i);
        const double snow = previous.snow[i];
        double added = 0;
        if (snow > 0) {
            const int j = bank.column(oldX + motion.x() * .65);
            if (j >= 0) {
                const double released = snow * shed;
                const double kept = released >= .3 ? snow - released : snow;
                const double room = qMax(0.0, bank.maxDepth - bank.snow[j]);
                added = qMin(room, kept);
                bank.snow[j] += added;
            }
        }
        batchMass += snow - added;
        weightedX += (snow - added) * oldX;
        if (((i + 1) % batchSize == 0 || i + 1 == columns) && batchMass > 0) {
            shedSnow(weightedX / batchMass, previous.surface.y(), batchMass, motion);
            batchMass = 0;
            weightedX = 0;
        }
    }
    bank.relax(0, static_cast<int>(bank.snow.size()) - 1);
    bank.invalidate();
}

void SnowScene::shedSnow(double x, int y, double amount, const QPoint &motion)
{
    if (amount < .3)
        return;
    if (m_shedParticles >= maxShedParticles) {
        // Лишняя порция едет с ближайшей уже падающей частицей.
        auto nearest = m_particles.end();
        double nearestDistance = std::numeric_limits<double>::infinity();
        for (auto it = m_particles.begin(); it != m_particles.end(); ++it) {
            if (it->snowMass <= 0)
                continue;
            const QPointF delta = it->center - QPointF(x, y);
            const double distance = QPointF::dotProduct(delta, delta);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = it;
            }
        }
        nearest->snowMass += amount;
        return;
    }
    const QPointF origin(x + random(-3, 3), y - random(0, qMin(6.0, amount)));
    const QPointF velocity(qBound(-260.0, motion.x() * 3.5, 260.0) + random(-32, 32),
                           qBound(0.0, motion.y() * 2.5, 140.0) + random(18, 60));
    const double radius = qBound(1.4, 1.35 + amount * .09, 4.4);
    m_particles.push_back({origin, velocity, radius, 0, true, SnowCrystal::Glyph::Asterisk, amount});
    ++m_shedParticles;
}

bool SnowScene::settleSnow(const QPointF &from, const QPointF &to, double amount)
{
    const QPointF travel = to - from;
    if (travel.y() <= 0)
        return false;

    Bank *landed = nullptr;
    double firstContact = 1;
    const int steps = qBound(1, qCeil(std::abs(travel.x()) / (columnWidth / 2)), 64);
    const auto checkBank = [&](Bank &bank, const QRegion *visible) {
        const int y = bank.surface.y();
        if (bank.surface.isEmpty() || (visible && visible->isEmpty()) ||
            from.y() > y || to.y() < y - bank.maxDepth ||
            qMax(from.x(), to.x()) < bank.surface.left() || qMin(from.x(), to.x()) >= bank.surface.right() + 1)
            return;
        const auto depth = [&](double t) {
            const QPointF point = from + travel * t;
            return point.y() - y + bank.heightAt(point.x());
        };
        double start = 0;
        double previous = depth(0);
        for (int i = 1; i <= steps && start <= firstContact; ++i) {
            const double end = double(i) / steps;
            const double current = depth(end);
            if (previous <= 0 && current >= 0) {
                double lo = start, hi = end;
                for (int pass = 0; pass < 7; ++pass) {
                    const double mid = (lo + hi) / 2;
                    if (depth(mid) < 0) lo = mid;
                    else hi = mid;
                }
                const QPointF point = from + travel * hi;
                if (hi <= firstContact && bank.column(point.x()) >= 0 &&
                    (!visible || visible->contains(QPoint(qFloor(point.x()), qCeil(point.y()) - 1)))) {
                    landed = &bank;
                    firstContact = hi;
                    break;
                }
            }
            previous = current;
            start = end;
        }
    };
    for (WindowBank &drift : m_windows)
        checkBank(drift.bank, &drift.visible);
    checkBank(m_dock, nullptr);
    checkBank(m_floor, nullptr);
    if (!landed)
        return false;
    const double x = from.x() + travel.x() * firstContact;
    const int radius = qBound(1, qCeil(amount / 85), 5);
    landed->add(x, amount, radius);
    const QRect band = landed->bounds();
    const int margin = columnWidth * (radius + 3);
    if (amount > 0)
        m_dirty += QRect(qFloor(x) - margin, band.y(), margin * 2, band.height()) & band;
    return true;
}

QRegion SnowScene::inputRegion() const
{
    if (m_aim)
        return QRegion(QRect(QPoint(), m_size));
    QRegion region = (m_floor.inputRegion() & floorRegion()) + m_dock.inputRegion();
    for (const Ball &ball : m_balls) {
        if (ball.resting && (!ball.top || ball.inSnowman()) && &ball != m_held)
            region += QRegion(around(ball.center, ball.radius()), QRegion::Ellipse);
    }
    return region;
}

QRegion SnowScene::floorRegion() const
{
    const QRect bounds = m_floor.bounds();
    const QRect underDock(m_dock.surface.x(), bounds.y(), m_dock.surface.width(), bounds.height());
    return QRegion(bounds) - underDock;
}

bool SnowScene::press(const QPointF &point, double time)
{
    if (m_held || m_aim)
        return false;

    for (auto it = m_balls.rbegin(); it != m_balls.rend(); ++it) {
        Ball &ball = *it;
        if (!ball.resting || ball.top || length(ball.center - point) > ball.radius() + .5)
            continue;
        m_dirty += ball.bounds();
        if (ball.support) {
            if (ball.isHead())
                m_dirty += ball.support->support->bounds();
            ball.support->top = nullptr;
            m_dirty += around(ball.support->center, 38);
        }
        ball.support = nullptr;
        ball.celebration = -1;
        ball.resting = false;
        m_held = &ball;
        m_grabOffset = point - ball.center;
        break;
    }
    if (!m_held) {
        if (m_balls.size() >= maxBalls)
            return false;
        for (Bank *bank : {&m_dock, &m_floor}) {
            if (!bank->inputRegion().contains(point.toPoint()) ||
                (bank == &m_floor && !floorRegion().contains(point.toPoint())))
                continue;
            m_collecting = bank;
            m_balls.push_back(Ball{});
            m_held = &m_balls.back();
            m_held->center = point;
            m_grabOffset = {};
            m_collected = 0;
            m_collectionX = point.x();
            collectAlong(point, point);
            break;
        }
    }
    if (!m_held)
        return false;
    m_hoverPoint.reset();
    m_windowHoverPoint.reset();
    m_pressPoint = point;
    m_dragMoved = false;
    m_samples.clear();
    recordPointer(point, time);
    return true;
}

void SnowScene::move(const QPointF &point, double time)
{
    if (!m_held)
        return;
    const bool wasMoving = m_dragMoved;
    m_dragMoved = m_dragMoved || length(point - m_pressPoint) >= gestureDistance;
    if (m_collecting && m_dragMoved && point != m_pointer)
        collectAlong(wasMoving ? m_pointer : m_pressPoint, point);
    recordPointer(point, time);
}

bool SnowScene::release(const QPointF &point, double time)
{
    if (!m_held)
        return false;
    move(point, time);
    if (m_collecting && !m_dragMoved && m_collected >= 2) {
        m_aim = Aim{m_pressPoint, point, m_collecting, m_collected};
        m_dirty += around(point, aimedRadius + 4);
        removeHeldBall();
        cancelDrag();
        return true;
    }
    if (m_collecting && m_collected < smallSnow) {
        cancelDrag();
        return true;
    }
    const QPointF velocity = gestureVelocity();
    const QPointF desired = point - m_grabOffset;
    if (Ball *base = snapTarget(desired)) {
        attach(*base);
    } else {
        m_held->velocity = QPointF(qBound(-210.0, velocity.x() * .28, 210.0), 0);
    }
    cancelDrag();
    return true;
}

void SnowScene::hover(const QPointF &point)
{
    if (m_aim) {
        if (m_aim->cursor != point) {
            m_dirty += around(m_aim->cursor, aimedRadius + 4);
            m_aim->cursor = point;
            m_dirty += around(point, aimedRadius + 4);
        }
        return;
    }
    const auto travel = hoverTravel(point, m_hoverPoint);
    if (!travel)
        return;
    for (Bank *bank : {&m_dock, &m_floor}) {
        if (bank == &m_floor && !floorRegion().contains(point.toPoint()))
            continue;
        if (disturbBank(*bank, point, *travel))
            break;
    }
}

void SnowScene::hoverWindows(const QPointF &point)
{
    if (m_held || m_aim)
        return;
    const auto travel = hoverTravel(point, m_windowHoverPoint);
    if (!travel)
        return;
    for (auto it = m_windows.rbegin(); it != m_windows.rend(); ++it) {
        if (it->visible.contains(point.toPoint()) && disturbBank(it->bank, point, *travel))
            break;
    }
}

std::optional<QPointF> SnowScene::hoverTravel(const QPointF &point, std::optional<QPointF> &previous)
{
    if (!previous) {
        previous = point;
        return std::nullopt;
    }
    const QPointF travel = point - *previous;
    const double distance = length(travel);
    if (distance < 3 || m_hoverCooldown > 0)
        return std::nullopt;
    previous = point;
    return distance <= 80 ? std::optional<QPointF>(travel) : std::nullopt;
}

bool SnowScene::disturbBank(Bank &bank, const QPointF &point, const QPointF &travel)
{
    const double depth = bank.heightAt(point.x());
    const double crest = bank.surface.y() - depth;
    if (depth < 1 || point.y() < crest - 3 || point.y() >= bank.surface.y())
        return false;
    const double amount = bank.collect(point, qMin(qMin(3.6, length(travel) * .12), depth * .28));
    if (amount <= .15)
        return false;
    const QRect band = bank.bounds();
    m_dirty += QRect(qFloor(point.x()) - 36, band.y(), 72, band.height()) & band;
    spray(QPointF(point.x(), crest), qBound(-125.0, travel.x() * 7, 125.0), amount, false);
    m_hoverCooldown = .045;
    return true;
}

bool SnowScene::fireAt(const QPointF &point)
{
    if (!m_aim)
        return false;
    if (m_splats.size() == maxSplats)
        crumbleOldest();
    m_flights.push_back({m_aim->origin - QPointF(0, 8), point, aimedRadius});
    m_dirty += around(m_aim->cursor, aimedRadius + 4);
    m_aim.reset();
    return true;
}

void SnowScene::cancelAim()
{
    if (!m_aim)
        return;
    m_dirty += around(m_aim->cursor, aimedRadius + 4);
    m_aim->bank->add(m_aim->origin.x(), m_aim->mass);
    m_dirty += m_aim->bank->bounds();
    m_aim.reset();
    m_hoverPoint.reset();
}

bool SnowScene::breakSnowman(const QPointF &point)
{
    if (m_held || m_aim)
        return false;
    for (const Ball &member : m_balls) {
        if (!member.inSnowman() || length(point - member.center) > member.radius() + .5)
            continue;
        const Ball *base = member.root();
        const Ball *middle = base->top;
        const Ball *head = middle->top;
        m_dirty += base->bounds();
        m_dirty += around(middle->center, 38);
        m_dirty += head->bounds();
        const double x = base->center.x();
        Bank &bank = surfaceY(*base) == m_dock.surface.y() && m_dock.column(x) >= 0 ? m_dock : m_floor;
        bank.add(x, 185, 5);
        const QRect band = bank.bounds();
        m_dirty += QRect(qFloor(x) - 64, band.y(), 128, band.height()) & band;
        spray(QPointF(x, bank.surface.y() - bank.heightAt(x)), 0, 6, false);
        m_balls.remove_if([=](const Ball &ball) { return &ball == base || &ball == middle || &ball == head; });
        return true;
    }
    return false;
}

void SnowScene::cancelDrag()
{
    if (m_held && m_collecting && m_collected < smallSnow) {
        m_collecting->add(m_collectionX, m_collected);
        m_dirty += m_collecting->bounds();
        removeHeldBall();
    }
    m_held = nullptr;
    m_collecting = nullptr;
    m_samples.clear();
}

void SnowScene::collectAlong(const QPointF &from, const QPointF &to)
{
    if (m_collected >= largeSnow)
        return;
    // Отсчёты вдоль жеста сохраняют сбор снега при быстрых движениях мыши.
    const QPointF delta = to - from;
    const int steps = qBound(1, qCeil(length(delta) / 6), 300);
    const int start = from == to ? 0 : 1;
    const QRect previous = around(m_held->center, m_held->radius() + 3);
    for (int i = start; i <= steps && m_collected < largeSnow; ++i) {
        if (from == to && i > 0)
            break;
        const QPointF point = from + delta * (double(i) / steps);
        const double amount = m_collecting->collect(point, qMin(from == to ? 16.0 : 8.0, largeSnow - m_collected));
        if (amount > 0) {
            m_collected += amount;
            const QRect band = m_collecting->bounds();
            m_dirty += QRect(qFloor(point.x()) - columnWidth * 4, band.y(), columnWidth * 8, band.height()) & band;
        }
    }
    m_held->size = m_collected >= largeSnow - .01 ? Size::Large : m_collected >= mediumSnow ? Size::Medium : Size::Small;
    m_dirty += previous;
}

void SnowScene::recordPointer(const QPointF &point, double time)
{
    m_pointer = point;
    m_samples.push_back({point, time});
    while (m_samples.size() > 1 && m_samples.front().time < time - .11)
        m_samples.pop_front();
    if (m_samples.size() > 64)
        m_samples.pop_front();
}

QPointF SnowScene::gestureVelocity() const
{
    const auto &first = m_samples.front();
    const auto &last = m_samples.back();
    const double elapsed = last.time - first.time;
    return elapsed > .008 ? (last.point - first.point) / elapsed : QPointF();
}

SnowScene::Ball *SnowScene::snapTarget(const QPointF &point)
{
    Ball *best = nullptr;
    double distance = snapDistance;
    for (Ball &base : m_balls) {
        if (!base.resting || base.top || &base == m_held)
            continue;
        if (m_held->size >= base.size)
            continue;
        const double candidate = length(point - m_held->snapPoint(base));
        if (candidate <= distance) {
            best = &base;
            distance = candidate;
        }
    }
    return best;
}

void SnowScene::attach(Ball &base)
{
    m_dirty += m_held->bounds();
    m_held->center = m_held->snapPoint(base);
    m_held->velocity = {};
    m_held->support = &base;
    m_held->resting = true;
    base.top = m_held;
    if (m_held->isHead())
        m_held->celebration = 0;
    m_dirty += m_held->bounds();
    m_dirty += around(base.center, base.radius() + 3);
}

double SnowScene::surfaceY(const Ball &ball) const
{
    const double x = ball.center.x();
    const double dockY = m_dock.surface.y();
    return m_dock.column(x) >= 0 && ball.center.y() - ball.radius() < dockY ? dockY : m_size.height();
}

void SnowScene::fall(Ball &ball, double dt)
{
    const QPointF previous = ball.center;
    const double radius = ball.radius();
    ball.velocity.ry() += gravity * dt;
    ball.center += ball.velocity * dt;
    ball.velocity.rx() *= std::pow(.42, dt);

    for (const Ball &other : m_balls) {
        if (&other == &ball || !other.resting)
            continue;
        const double width = radius + other.radius();
        double dx = ball.center.x() - other.center.x();
        const double dy = ball.center.y() - other.center.y();
        if (dx * dx + dy * dy >= width * width)
            continue;

        const bool onTop = dy < 0 && previous.y() <= other.center.y();
        const double separation = onTop ? width : std::sqrt(width * width - dy * dy);
        const bool roomOnRight = other.center.x() + separation + radius < m_size.width();
        const bool roomOnLeft = other.center.x() - separation - radius > 0;
        const double direction = (roomOnRight && (dx >= 0 || !roomOnLeft)) || !roomOnLeft ? 1 : -1;
        if (onTop) {
            dx = direction * qMin(width, qMax(0.0, direction * dx) + qMax(1.5, 135 * dt));
            ball.center = other.center + QPointF(dx, -std::sqrt(width * width - dx * dx));
            ball.velocity = {direction * 140, 30};
        } else {
            // Центр малого кома у земли бывает ниже центра большого. Здесь
            // решаем боковой контакт, не поднимая малый ком обратно на вершину.
            ball.center.setX(other.center.x() + direction * separation);
            ball.velocity.rx() = direction * 85;
        }
    }

    const double maxX = qMax(radius, m_size.width() - radius);
    const double clampedX = qBound(radius, ball.center.x(), maxX);
    if (clampedX != ball.center.x()) {
        ball.center.setX(clampedX);
        ball.velocity.rx() *= -.28;
    }

    const double ground = surfaceY(ball) - radius;
    if (ball.center.y() >= ground) {
        ball.center.setY(ground);
        ball.velocity.ry() = 0;
        ball.velocity.rx() *= std::pow(.02, dt);
        ball.resting = std::abs(ball.velocity.x()) < 14;
    }
}

void SnowScene::startPlow()
{
    if (m_plow.active || m_floor.surface.isEmpty())
        return;
    m_plow.active = true;
    m_plow.onDock = !m_dock.surface.isEmpty();
    const Bank &bank = m_plow.onDock ? m_dock : m_floor;
    m_plow.direction *= -1;
    m_plow.x = m_plow.direction > 0 ? bank.surface.left() - 60 : bank.surface.right() + 60;
    m_plow.y = bank.surface.y();
    m_plow.trailSnow = 0;
    m_plow.untilSpray = 0;
    m_plow.untilSmoke = 0;
}

void SnowScene::spray(const QPointF &center, double speedX, double amount, bool plow)
{
    const int count = qMin(3, static_cast<int>(maxSprayParticles - m_sprayParticles));
    for (int i = 0; i < count; ++i) {
        const QPointF origin = center + QPointF(random(-4, 4), random(-3, -1));
        const QPointF velocity(speedX + random(-45, 45), -random(42, plow ? 145 : 110));
        m_particles.push_back({origin, velocity, random(1.3, 3.2), 0, true,
                               SnowCrystal::Glyph::Asterisk, plow ? 0 : -amount * .55 / count, 1.25});
        ++m_sprayParticles;
    }
}

void SnowScene::updatePlow(double dt)
{
    for (Smoke &smoke : m_smoke) {
        smoke.age += dt;
        smoke.center.ry() -= dt * 21;
    }
    m_smoke.erase(std::remove_if(m_smoke.begin(), m_smoke.end(), [](const Smoke &smoke) {
        return smoke.age >= .84;
    }), m_smoke.end());

    if (!m_plow.active) {
        m_plow.untilNext -= dt;
        if (m_plow.untilNext <= 0)
            startPlow();
        return;
    }

    Bank &bank = m_plow.onDock ? m_dock : m_floor;
    if (bank.surface.isEmpty()) {
        m_plow.active = false;
        m_plow.untilNext = 48;
        return;
    }
    const double travel = m_plow.direction * plowSpeed * dt;
    m_plow.x += travel;
    m_plow.y = bank.surface.y();
    m_plow.angle += plowSpeed * dt / 6;
    m_plow.phase += dt * 10;
    m_plow.untilSmoke -= dt;
    if (m_plow.untilSmoke <= 0) {
        m_smoke.push_back({QPointF(m_plow.x - m_plow.direction * 15, m_plow.y - 25)});
        m_plow.untilSmoke = .16;
    }

    const double blade = m_plow.x + m_plow.direction * 25;
    m_plow.untilSpray -= dt;
    m_plow.trailSnow *= std::exp(-2.1 * dt);
    // Ведро, шарф и собранная фигура остаются целыми после уборки.
    const bool protectedSnow = std::any_of(m_balls.begin(), m_balls.end(), [&](const Ball &ball) {
        return ball.isHead() && std::abs(ball.center.x() - blade) < 36 &&
               std::abs(ball.root()->center.y() + ball.root()->radius() - m_plow.y) < 12;
    });
    if (!protectedSnow && dt > 0) {
        const double amount = bank.collect(QPointF(blade, m_plow.y - 1), plowSpeed * dt * 1.7);
        if (amount > .15) {
            m_plow.trailSnow = qMin(35.0, m_plow.trailSnow + amount * .40);
            const QRect band = bank.bounds();
            m_dirty += QRect(qFloor(blade) - 36, band.y(), 72, band.height()) & band;
            if (m_plow.untilSpray <= 0) {
                spray(QPointF(blade, m_plow.y - 8), m_plow.direction * 100, amount, true);
                m_plow.untilSpray = .07;
            }
        }
        for (Ball &ball : m_balls) {
            if (&ball == m_held || !ball.resting || ball.support || ball.top ||
                std::abs(ball.center.y() + ball.radius() - m_plow.y) > 5 ||
                std::abs(ball.center.x() - blade) > ball.radius() + 8)
                continue;
            m_dirty += ball.bounds();
            ball.center.rx() += travel;
            ball.resting = ball.center.x() > ball.radius() &&
                           ball.center.x() < m_size.width() - ball.radius() &&
                           surfaceY(ball) == m_plow.y;
            m_dirty += ball.bounds();
        }
    }
    if (m_plow.x < bank.surface.left() - 76 || m_plow.x > bank.surface.right() + 76) {
        m_plow.active = false;
        m_plow.untilNext = 48;
    }
}

void SnowScene::removeHeldBall()
{
    m_dirty += around(m_held->center, m_held->radius() + 3);
    const Ball *removed = m_held;
    m_held = nullptr;
    m_balls.remove_if([removed](const Ball &ball) { return &ball == removed; });
}

void SnowScene::impact(const Flight &flight)
{
    if (m_splats.size() == maxSplats)
        crumbleOldest();
    Splat splat{flight.destination, flight.visibleRadius(), {}};
    for (double &edge : splat.edge)
        edge = random(.56, 1.14);
    m_splats.push_back(splat);
    scatter(flight.destination, splat.radius, false);
}

void SnowScene::scatter(const QPointF &center, double radius, bool flakes)
{
    const int count = flakes ? 24 : 12;
    for (int i = 0; i < count; ++i) {
        const double angle = twoPi * i / count + random(-.18, .18);
        const QPointF position = center + (flakes ? polar(radius * random(.12, .78), angle) : QPointF());
        const QPointF velocity = polar(random(flakes ? 22 : 65, flakes ? 77 : 185), angle);
        m_particles.push_back({position, velocity, random(1.2, flakes ? 4.2 : 3.8), 0, flakes,
                               static_cast<SnowCrystal::Glyph>(i % SnowCrystal::glyphCount)});
    }
}

void SnowScene::crumbleOldest()
{
    const Splat &oldest = m_splats.front();
    m_dirty += around(oldest.center(), oldest.radius * 1.6 + 16);
    scatter(oldest.center(), oldest.radius, true);
    m_splats.pop_front();
}

QRegion SnowScene::animatedRegion() const
{
    QRegion dirty;
    if (m_plow.active)
        dirty += m_plow.bounds();
    for (const Smoke &smoke : m_smoke)
        dirty += around(smoke.center, 10);
    for (const Ball &ball : m_balls) {
        const Ball *base = ball.root();
        if (!base->resting || (ball.celebration >= 0 && ball.celebration < .85))
            dirty += ball.bounds();
        if (ball.isHead() && ball.celebration < .85)
            dirty += base->bounds();
    }
    for (const Flight &flight : m_flights)
        dirty += around(flight.center(), flight.visibleRadius() + 2);
    for (const Splat &splat : m_splats)
        dirty += around(splat.center(), splat.radius * 1.6 + 16);
    for (const Particle &particle : m_particles) {
        dirty += particle.flake ? SnowCrystal::bounds(particle.center, particle.radius)
                               : around(particle.center, particle.radius + 1);
    }
    return dirty;
}

QRegion SnowScene::advance(double dt)
{
    QRegion dirty = animatedRegion();
    m_hoverCooldown = qMax(0.0, m_hoverCooldown - dt);
    updatePlow(dt);
    if (m_held) {
        const QPointF destination = m_pointer - m_grabOffset;
        m_held->center += (destination - m_held->center) * (1 - std::exp(-25 * dt));
    }

    // Сначала перемещаем основания, затем обновляем позиции всех прикреплённых комов.
    for (Ball &ball : m_balls) {
        if (&ball != m_held && !ball.support && !ball.resting)
            fall(ball, dt);
    }
    for (Ball &ball : m_balls) {
        if (!ball.support && ball.top) {
            Ball &middle = *ball.top;
            middle.center = middle.snapPoint(ball);
            if (middle.top)
                middle.top->center = middle.top->snapPoint(middle);
        }
        if (ball.celebration >= 0)
            ball.celebration = qMin(.85, ball.celebration + dt);
    }

    for (auto it = m_flights.begin(); it != m_flights.end();) {
        it->age += dt;
        if (it->age < flightDuration) {
            ++it;
        } else {
            impact(*it);
            it = m_flights.erase(it);
        }
    }
    for (Splat &splat : m_splats)
        splat.age += dt;
    while (!m_splats.empty() && m_splats.front().age >= splatHold + splatSlide)
        crumbleOldest();

    const auto firstRemoved = std::remove_if(m_particles.begin(), m_particles.end(), [&](Particle &particle) {
        const QPointF oldCenter = particle.center;
        particle.age += dt;
        particle.velocity.ry() += (particle.flake ? 125 : 510) * dt;
        if (particle.flake)
            particle.velocity.rx() += std::sin(particle.age * 5 + particle.radius) * 22 * dt;
        particle.center += particle.velocity * dt;
        const bool shed = particle.snowMass > 0;
        const bool spray = particle.life < 5;
        const double amount = spray ? -particle.snowMass : shed ? particle.snowMass : (particle.flake ? 1.4 : .7);
        const bool expired = settleSnow(oldCenter, particle.center, amount) ||
                             particle.center.y() > m_size.height() + 20 || particle.age > particle.life;
        if (expired && shed)
            --m_shedParticles;
        if (expired && spray)
            --m_sprayParticles;
        return expired;
    });
    m_particles.erase(firstRemoved, m_particles.end());

    dirty += animatedRegion();
    dirty += std::exchange(m_dirty, QRegion());
    return dirty;
}

void SnowScene::paint(QPainter &p, const QRegion &repaint) const
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    if (repaint.isEmpty() || repaint.intersects(m_floor.bounds())) {
        p.save();
        p.setClipRegion(floorRegion(), Qt::IntersectClip);
        m_floor.paint(p);
        p.restore();
    }
    if (repaint.isEmpty() || repaint.intersects(m_dock.bounds()))
        m_dock.paint(p);
    for (const WindowBank &drift : m_windows) {
        if (drift.visible.isEmpty() || (!repaint.isEmpty() && !repaint.intersects(drift.visible)))
            continue;
        p.save();
        p.setClipRegion(drift.visible, Qt::IntersectClip);
        drift.bank.paint(p);
        p.restore();
    }

    for (const Smoke &smoke : m_smoke) {
        if (!repaint.isEmpty() && !repaint.intersects(around(smoke.center, 10)))
            continue;
        const double radius = 2.4 + smoke.age * 6.5;
        QColor tint(215, 222, 232);
        tint.setAlphaF(.43 * (1 - smoke.age / .84));
        p.setBrush(tint);
        p.drawEllipse(smoke.center, radius, radius);
    }
    if (m_plow.active && (repaint.isEmpty() || repaint.intersects(m_plow.bounds())))
        SnowProps::drawPlow(p, m_plow.x, m_plow.y, m_plow.direction, m_plow.trailSnow, m_plow.angle, m_plow.phase);
    for (const Ball &ball : m_balls) {
        if (&ball != m_held && (repaint.isEmpty() || repaint.intersects(ball.bounds())))
            drawSnowball(p, ball.center, ball.radius(), ball.resting && !ball.support);
    }
    const auto part = [](const Ball &ball) {
        return SnowProps::Ball{ball.center.x(), ball.center.y(), ball.radius()};
    };
    for (const Ball &head : m_balls) {
        if (!head.isHead())
            continue;
        const Ball &middle = *head.support;
        const Ball &base = *middle.support;
        if (!repaint.isEmpty() && !repaint.intersects(head.bounds().united(base.bounds())))
            continue;
        SnowProps::drawSnowman(p, part(base), part(middle), part(head), head.celebration / .85);
        if (head.celebration >= 0 && head.celebration < .85) {
            p.save();
            p.setOpacity(qMax(0.0, 1 - head.celebration / .85));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 247, 209));
            for (int i = 0; i < 9; ++i) {
                const QPointF star = head.center + polar(14 + head.celebration * 24, twoPi * i / 9);
                const double size = i % 2 == 0 ? 3 : 1.8;
                p.drawPolygon(QPolygonF{star + QPointF(0, -size), star + QPointF(size, 0),
                                        star + QPointF(0, size), star + QPointF(-size, 0)});
            }
            p.restore();
        }
    }
    if (m_held)
        drawSnowball(p, m_held->center, m_held->radius());
    if (m_aim && (repaint.isEmpty() || repaint.intersects(around(m_aim->cursor, aimedRadius + 4))))
        drawSnowball(p, m_aim->cursor, aimedRadius);

    for (const Flight &flight : m_flights)
        drawSnowball(p, flight.center(), flight.visibleRadius());

    for (const Splat &splat : m_splats) {
        const QPointF center = splat.center();
        const double slide = splat.slideTime() / splatSlide;
        if (slide > 0) {
            QLinearGradient trail(splat.impact, center);
            trail.setColorAt(0, QColor(224, 246, 255, 0));
            trail.setColorAt(1, QColor(218, 241, 255, 115));
            QPainterPath streak;
            streak.moveTo(splat.impact + QPointF(-splat.radius * .08, 0));
            streak.quadTo(center + QPointF(-splat.radius * .23, -5), center + QPointF(-splat.radius * .34, 0));
            streak.lineTo(center + QPointF(splat.radius * .31, 0));
            streak.quadTo(center + QPointF(splat.radius * .13, -5), splat.impact + QPointF(splat.radius * .02, 0));
            streak.closeSubpath();
            p.fillPath(streak, trail);
        }

        std::array<QPointF, 20> outline;
        for (std::size_t i = 0; i < outline.size(); ++i) {
            QPointF point = polar(splat.radius * splat.edge[i], twoPi * i / outline.size());
            point.ry() *= .82 + slide * .32;
            outline[i] = center + point;
        }
        QPainterPath patch;
        patch.moveTo((outline.back() + outline.front()) / 2);
        for (std::size_t i = 0; i < outline.size(); ++i)
            patch.quadTo(outline[i], (outline[i] + outline[(i + 1) % outline.size()]) / 2);
        patch.closeSubpath();
        QRadialGradient ice(center - QPointF(0, splat.radius * .19), splat.radius);
        ice.setColorAt(0, QColor(254, 255, 255, 215));
        ice.setColorAt(.7, QColor(214, 238, 250, 182));
        ice.setColorAt(1, QColor(178, 215, 240, 145));
        p.fillPath(patch, ice);
    }
    for (const Particle &particle : m_particles) {
        const QRect bounds = particle.flake ? SnowCrystal::bounds(particle.center, particle.radius)
                                           : around(particle.center, particle.radius + 1);
        if (!repaint.isEmpty() && !repaint.intersects(bounds))
            continue;
        if (particle.flake) {
            SnowCrystal::paint(p, particle.center, particle.radius, .86, particle.glyph);
        } else {
            p.setBrush(QColor(235, 250, 255, 231));
            p.drawEllipse(particle.center, particle.radius, particle.radius);
        }
    }
}
