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
constexpr double snapDistance = 18;
constexpr double gestureDistance = 6;
constexpr double aimedRadius = 9;
constexpr double plowSpeed = 185;
constexpr double plowInterval = 48;
constexpr double followupDelay = 1.15;
constexpr double treeShakeDuration = 1.18;
constexpr double treeBlinkInterval = .36;
constexpr std::size_t maxSprayParticles = 36;
constexpr double flightDuration = 0.25;
constexpr double splatHold = 1.5;
constexpr double splatSlide = 2.5;
constexpr double slideSpeed = 18;
constexpr double gravity = 960;
constexpr std::size_t maxSplats = 6;
constexpr std::size_t maxBalls = 30;
constexpr std::size_t maxShedParticles = 72;
constexpr std::size_t maxFragments = 140;
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

} // namespace

void SnowScene::Bank::setSurface(const QRect &rect)
{
    if (surface == rect)
        return;
    const QRect oldSurface = surface;
    const auto oldSnow = std::move(snow);
    const auto oldPending = std::move(pending);
    surface = rect;
    snow.assign((rect.width() + columnWidth - 1) / columnWidth, 0);
    capacity.resize(snow.size());
    pending.clear();
    if (!oldPending.empty())
        pending.resize(snow.size());
    for (int i = 0; i < static_cast<int>(snow.size()); ++i) {
        const double x = center(i);
        const double along = x - surface.left();
        const double phase = maxDepth * .29;
        const double undulation = .82 + .103 * std::sin(along / 51 + phase)
                                     + .052 * std::sin(along / 22 - phase) + .024 * std::cos(along / 9);
        double limit = maxDepth * undulation;
        if (tapered) {
            const double t = std::min({1.0, along / 31, (surface.width() - along) / 39});
            limit *= t * t * (3 - 2 * t);
        }
        capacity[i] = limit;
        const int oldColumn = static_cast<int>((x - oldSurface.x()) / columnWidth);
        if (oldSurface.left() <= x && x < oldSurface.right() + 1 && oldColumn < static_cast<int>(oldSnow.size())) {
            snow[i] = qMin(limit, oldSnow[oldColumn]);
            if (!oldPending.empty())
                pending[i] = qMin(limit - snow[i], oldPending[oldColumn]);
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
    const double c0 = tapered ? h0 : h0 + slope(left) * (x1 - x0) / 3;
    const double c1 = tapered ? h1 : h1 - slope(right) * (x1 - x0) / 3;
    const double u = 1 - t;
    return u * u * (u * h0 + 3 * t * c0) + t * t * (3 * u * c1 + t * h1);
}

double SnowScene::Bank::slope(int i) const
{
    if (i <= 0 || i + 1 >= static_cast<int>(snow.size()))
        return 0;
    const double left = (snow[i] - snow[i - 1]) / (center(i) - center(i - 1));
    const double right = (snow[i + 1] - snow[i]) / (center(i + 1) - center(i));
    return left * right > 0 ? 2 * left * right / (left + right) : 0;
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
        ridge.cubicTo(previous + QPointF(handle, tapered ? 0 : -slope(i - 1) * handle),
                      point - QPointF(handle, tapered ? 0 : -slope(i) * handle), point);
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

void SnowScene::Bank::add(double x, double amount, int radius, bool gradual)
{
    const int i = column(x);
    if (i < 0 || amount <= 0)
        return;
    const int first = qMax(0, i - radius);
    const int last = qMin(i + radius, static_cast<int>(snow.size()) - 1);
    const auto weight = [i, radius](int j) {
        const double distance = double(j - i) / (radius + 1);
        return std::pow(1 - distance * distance, 2);
    };
    double totalWeight = 0;
    for (int j = first; j <= last; ++j)
        totalWeight += weight(j);
    if (gradual && pending.empty())
        pending.resize(snow.size());
    for (int j = first; j <= last; ++j) {
        const double share = amount * weight(j) / totalWeight;
        if (gradual)
            pending[j] = qMin(capacity[j] - snow[j], pending[j] + share);
        else
            snow[j] = qMin(capacity[j], snow[j] + share);
    }
    if (gradual)
        return;
    relax(first - 1, last + 1);
    invalidate();
}

QRect SnowScene::Bank::advanceDeposits(double dt)
{
    if (pending.empty() || dt <= 0)
        return {};
    int first = static_cast<int>(pending.size()), last = -1;
    bool more = false;
    const double settling = -std::expm1(-3 * dt);
    for (int i = 0; i < static_cast<int>(pending.size()); ++i) {
        const double left = qMax(0.0, qMin(pending[i], capacity[i] - snow[i]));
        const double step = std::min({left, dt * 9, (left + .10) * settling});
        pending[i] = left - step;
        if (step > 0) {
            snow[i] += step;
            first = qMin(first, i);
            last = i;
        }
        more = more || pending[i] > 0;
    }
    if (!more)
        pending.clear();
    if (last < first)
        return {};
    invalidate();
    const QRect band = bounds();
    return QRect(surface.left() + (first - 3) * columnWidth, band.y(),
                 (last - first + 7) * columnWidth, band.height()) & band;
}

void SnowScene::Bank::relax(int first, int last)
{
    // Перенос между соседями сохраняет снег и убирает отвесные ступеньки.
    constexpr double maxSlope = 5.5;
    first = qMax(first, 0);
    last = qMin(last, static_cast<int>(snow.size()) - 1);
    const auto balance = [&](int i) {
        const double diff = snow[i] - snow[i + 1];
        const double transfer = qBound(snow[i] - capacity[i],
            (diff - qBound(-maxSlope, diff, maxSlope)) / 2, capacity[i + 1] - snow[i + 1]);
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
    if (tapered)
        updateOutline();
    if (tapered ? outline.isEmpty() : !hasSnow())
        return;
    const QRect band = bounds();
    const qreal scale = p.device()->devicePixelRatioF();
    const QSize pixels(qCeil(band.width() * scale), qCeil(band.height() * scale));
    if (imageDirty || image.size() != pixels || image.devicePixelRatio() != scale) {
        if (image.size() != pixels)
            image = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull())
            return;
        image.setDevicePixelRatio(scale);
        image.fill(Qt::transparent);
        if (tapered) {
            QPainter buffer(&image);
            buffer.setRenderHint(QPainter::Antialiasing);
            buffer.translate(-band.topLeft());
            paintWindowSnow(buffer);
        } else {
            rasterizeGround(band, scale);
        }
        imageDirty = false;
    }
    p.drawImage(band.topLeft(), image);
}

void SnowScene::Bank::rasterizeGround(const QRect &band, qreal scale) const
{
    // Высота и блики считаются из того же профиля, что и столкновения.
    // У основания холодный оттенок, а гребень светлее.
    constexpr int shadesPerPixel = 8;
    static const auto shades = [] {
        std::array<QRgb, 7 * shadesPerPixel + 1> colors{};
        for (int i = 0; i < static_cast<int>(colors.size()); ++i) {
            const double depth = double(i) / shadesPerPixel;
            const double light = qMax(0.0, 1 - depth / 7);
            const double crest = qMax(0.0, 1 - depth / 2.5);
            colors[i] = qPremultiply(qRgba(qRound(194 + 32 * light + 27 * crest),
                                          qRound(217 + 22 * light + 15 * crest),
                                          qRound(234 + 13 * light + 8 * crest), 245));
        }
        return colors;
    }();

    const double base = (surface.y() - band.y()) * scale;
    const int bottom = qMin(qCeil(base), image.height());
    auto *pixels = reinterpret_cast<QRgb *>(image.bits());
    const qsizetype stride = image.bytesPerLine() / sizeof(QRgb);
    for (int x = 0; x < image.width(); ++x) {
        const double height = heightAt(band.x() + (x + .5) / scale);
        const double top = base - height * scale;
        for (int y = qMax(0, qFloor(top)); y < bottom; ++y) {
            const double covered = qMin<double>(y + 1, base) - qMax<double>(y, top);
            if (covered <= 0)
                continue;
            const double depth = qMax(0.0, (y + .5 - top) / scale);
            const QRgb color = shades[qMin(qRound(depth * shadesPerPixel), static_cast<int>(shades.size()) - 1)];
            pixels[y * stride + x] = covered >= 1 ? color
                : qRgba(qRound(qRed(color) * covered), qRound(qGreen(color) * covered),
                        qRound(qBlue(color) * covered), qRound(qAlpha(color) * covered));
        }
    }
}

void SnowScene::Bank::paintWindowSnow(QPainter &p) const
{
    const double y = surface.y();
    QLinearGradient tint(0, y - peak, 0, y);
    tint.setColorAt(0, QColor(254, 255, 255, 250));
    tint.setColorAt(.60, QColor(231, 244, 254, 244));
    tint.setColorAt(1, QColor(173, 204, 232, 228));
    p.fillPath(outline, tint);

    p.save();
    p.setClipPath(outline, Qt::IntersectClip);
    if (peak > 6) {
        p.save();
        p.translate(1.5, qMin(6.0, peak * .25));
        p.strokePath(ridge, QPen(QColor(143, 183, 210, 40), 2.4, Qt::SolidLine, Qt::RoundCap));
        p.restore();
    }
    p.strokePath(ridge, QPen(QColor(255, 255, 255, 155), 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.restore();
}

double SnowScene::Ball::radius() const
{
    if (mass <= 20)
        return 11;
    if (mass <= 48)
        return 11 + (mass - 20) * (8.0 / 28);
    if (mass <= 96)
        return 19 + (mass - 48) * (9.0 / 48);
    return std::sqrt(28 * 28 + (mass - 96) * 3.25);
}

QRect SnowScene::Ball::bounds() const
{
    const double r = radius();
    if (isHead())
        return around(center, qMax(r + 21 * std::sqrt(r / 11), 39.0));
    if (support && top)
        return around(center, qMax(r * 1.14, .85 * r + 16 * std::sqrt(r / 19)) + 3);
    return around(center, r * 1.14 + 3);
}

QPointF SnowScene::Ball::snapPoint(const Ball &base) const
{
    return base.center - QPointF(0, radius() + base.radius() - qMax(3.0, radius() * .16));
}

bool SnowScene::Ball::isHead() const
{
    return support && support->support;
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
                          const QList<SnowSurface> &windows, bool activeFullscreen)
{
    const QRect floor(0, size.height(), size.width(), 1);
    const QRect dockSurface = dock.isEmpty() ? QRect() : QRect(dock.x(), dock.y(), dock.width(), 1);
    const bool sizeChanged = m_size != size;
    const bool groundChanged = m_floor.surface != floor || m_dock.surface != dockSurface;
    m_size = size;
    if (groundChanged)
        m_dirty += m_floor.bounds().united(m_dock.bounds());
    m_floor.setSurface(floor);
    if (m_dock.surface != dockSurface) {
        if (m_dock.surface.isEmpty()) {
            m_dock.setSurface(dockSurface);
        } else {
            moveBank(m_dock, dockSurface, dockSurface.isEmpty() ? QPoint() :
                     dockSurface.topLeft() - m_dock.surface.topLeft());
        }
    }

    QList<SnowSurface> layout = windows;
    if (layout.isEmpty() && !activeWindow.isEmpty() && !activeFullscreen)
        layout.append({QStringLiteral("active-fallback"), activeWindow});
    if (sizeChanged || layout != m_layoutWindows)
        updateWindowBanks(layout);

    if (groundChanged) {
        updateTreeLayout();
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

void SnowScene::updateTreeLayout()
{
    const int height = qMin(132, qMin(m_size.width(), m_size.height()) / 3);
    const int width = qRound(height * (100.0 / 132.0));
    const int left = m_size.width() - width - 16;
    const QRect &dock = m_dock.surface;
    const bool aboveDock = !dock.isEmpty() && left <= dock.right() && left + width > dock.left();
    const int bottom = aboveDock ? dock.y() : m_size.height();
    const QRect frame = height >= 24 ? QRect(left, bottom - height, width, height) : QRect();
    if (m_treeFrame == frame)
        return;

    m_dirty += QRegion(treeBounds());
    m_dirty += frame;
    m_treeFrame = frame;
    m_treeInputDirty = true;
    m_treeCanopy = frame.isEmpty() ? QPainterPath() : SnowProps::treeCanopy(frame);
    const auto previous = std::move(m_treeBranches);
    m_treeBranches.clear();
    if (frame.isEmpty())
        return;
    // У каждого кончика ветки своя наклонная опора и локальный запас снега.
    struct Tier { int pairs; double halfWidth, innerY, outerY; };
    constexpr std::array<Tier, 3> tiers{{{3, 24, 23, 44}, {4, 33, 50, 69}, {5, 43, 78, 98}}};
    const double scale = height / 132.0;
    for (const Tier &tier : tiers) {
        for (int i = -tier.pairs; i <= tier.pairs; ++i) {
            const double u = double(i) / tier.pairs;
            const double x = 50 + u * tier.halfWidth;
            const double y = tier.innerY + std::abs(u) * (tier.outerY - tier.innerY)
                             + 1.8 * std::sin(i * 2.3 + tier.pairs);
            BranchPocket branch{QPointF(frame.x() + x * width / 100, frame.y() + y * scale),
                                (tier.halfWidth / tier.pairs * .69) * scale,
                                i == 0 ? .12 : std::copysign(.48, u), 11 + 3 * std::cos(i + tier.pairs)};
            if (m_treeBranches.size() < previous.size()) {
                const auto &old = previous[m_treeBranches.size()];
                branch.load = old.load;
                branch.offset = qBound(-branch.reach * .45, old.offset, branch.reach * .45);
            }
            m_treeBranches.push_back(branch);
        }
    }
}

double SnowScene::BranchPocket::crest(double x) const
{
    const double fill = std::sqrt(load / capacity);
    const double width = reach * (.42 + .76 * fill);
    const double u = (x - center.x() - offset) / width;
    const double dome = std::sqrt(qMax(0.0, 1 - u * u));
    return center.y() + (x - center.x()) * slope -
           reach * fill * dome * (.91 + .14 * std::sin(4.2 * u));
}

QPainterPath SnowScene::BranchPocket::snowShape() const
{
    QPainterPath cap;
    if (load < .05)
        return cap;
    const double fill = std::sqrt(load / capacity);
    const double width = reach * (.42 + .76 * fill);
    const double middle = center.x() + offset;
    for (int i = 0; i <= 16; ++i) {
        const double x = middle - width * std::cos(i * twoPi / 32);
        const QPointF point(x, crest(x));
        if (i == 0)
            cap.moveTo(point);
        else
            cap.lineTo(point);
    }
    cap.quadTo(middle, center.y() + offset * slope + reach * .55 * fill,
               middle - width, crest(middle - width));
    cap.closeSubpath();
    return cap;
}

void SnowScene::updateWindowBanks(const QList<SnowSurface> &windows)
{
    m_layoutWindows = windows;
    std::vector<WindowBank> next;
    next.reserve(windows.size());
    const QRect screen(QPoint(), m_size);

    // Перекрытия ограничивают отрисовку. Снежный профиль хранится для всей рамки.
    QRegion occlusion;
    for (auto it = windows.crbegin(); it != windows.crend(); ++it) {
        const QRect frame = it->rect.intersected(screen);
        if (frame.isEmpty())
            continue;
        const bool visibleTop = !it->dock && !it->panelPopup && !it->fullscreen && !it->maximized && it->rect.top() >= screen.top();
        // У верхней кромки монитора сугробу нужна опора внутри рамки.
        const int ledge = qMin(frame.bottom(), qMax(it->rect.top(), screen.top() + 8));
        const QRect surface = visibleTop ? QRect(frame.x(), ledge, frame.width(), 1) : QRect();
        auto old = std::find_if(m_windows.begin(), m_windows.end(), [&](const WindowBank &held) {
            return held.window.id == it->id;
        });
        WindowBank drift{*it, Bank(28, true), {}, {}};
        if (visibleTop && it->rect.width() >= 56 && it->rect.height() >= 36) {
            const QRect strip(it->rect.x(), it->rect.y(), it->rect.width(), 24);
            drift.garland = QRegion(strip & frame) - occlusion;
        }
        QRegion oldVisible;
        bool changedProfile = false;
        if (old != m_windows.end()) {
            if (old->revealDelay == 0 || old->window.rect == drift.window.rect)
                drift.revealDelay = old->revealDelay;
            if (old->garland.isEmpty() && !drift.garland.isEmpty())
                drift.revealDelay = WindowBank::initialRevealDelay;
            if (m_garlandsEnabled) {
                if (old->window.rect == drift.window.rect)
                    m_dirty += old->garland.xored(drift.garland);
                else
                    m_dirty += old->garland + drift.garland;
            }
            old->garland = {};
            oldVisible = old->visible;
            drift.bank = std::move(old->bank);
            old->bank = Bank();
            if (drift.bank.surface != surface || old->window.rect.topLeft() != it->rect.topLeft()) {
                changedProfile = drift.bank.hasSnow();
                moveBank(drift.bank, surface, it->rect.topLeft() - old->window.rect.topLeft());
                changedProfile = changedProfile || drift.bank.hasSnow();
            }
        } else {
            drift.bank.setSurface(surface);
            if (m_garlandsEnabled)
                m_dirty += drift.garland;
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
        if (m_garlandsEnabled)
            m_dirty += old.garland;
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
    bank = Bank(previous.maxDepth, previous.tapered);
    bank.setSurface(surface);
    const double distance = length(motion);
    const double shed = surface.isEmpty() ? 1 : qBound(0.0, (distance - 4) / 65, .72);
    const int columns = static_cast<int>(previous.snow.size());
    const int batchSize = qMax(1, (columns + particlesPerDrift - 1) / particlesPerDrift);
    double batchMass = 0;
    double weightedX = 0;
    for (int i = 0; i < columns; ++i) {
        const double oldX = previous.center(i);
        const double snow = previous.snow[i] + (previous.pending.empty() ? 0 : previous.pending[i]);
        double added = 0;
        if (snow > 0) {
            const int j = bank.column(oldX + motion.x() * .65);
            if (j >= 0) {
                const double released = snow * shed;
                const double kept = released >= .3 ? snow - released : snow;
                const double waiting = bank.pending.empty() ? 0 : bank.pending[j];
                const double room = qMax(0.0, bank.capacity[j] - bank.snow[j] - waiting);
                added = qMin(room, kept);
                const double visible = added * (previous.snow[i] / snow);
                bank.snow[j] += visible;
                if (added > visible) {
                    if (bank.pending.empty())
                        bank.pending.resize(bank.snow.size());
                    bank.pending[j] += added - visible;
                }
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

void SnowScene::shedSnow(double x, int y, double amount, const QPoint &motion, bool fromTree)
{
    if (amount < .3)
        return;
    const int count = qMin(amount >= 7 ? 2 : 1, static_cast<int>(maxShedParticles - m_shedParticles));
    if (count == 0) {
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
    for (int i = 0; i < count; ++i) {
        const QPointF origin(x + random(-6, 6), y - random(1, 10));
        const QPointF velocity(qBound(-120.0, motion.x() * 2.1, 120.0) + random(-26, 26),
                               qBound(0, motion.y(), 90) + random(15, 53));
        m_particles.push_back({origin, velocity, random(1.8, 5.1), 0, true,
                               SnowCrystal::Glyph::Asterisk, amount / count, 28, fromTree});
        ++m_shedParticles;
    }
}

bool SnowScene::settleSnow(const QPointF &from, const QPointF &to, double amount, bool catchOnTree, bool keepFalling)
{
    return deposit(from, to, amount, catchOnTree, false, keepFalling);
}

bool SnowScene::deposit(const QPointF &from, const QPointF &to, double amount, bool catchOnTree, bool gradual, bool keepFalling)
{
    const QPointF travel = to - from;
    if (travel.y() <= 0)
        return false;

    Bank *landed = nullptr;
    BranchPocket *pocket = nullptr;
    double firstContact = 1;
    const int steps = qBound(1, qCeil(std::abs(travel.x()) / (columnWidth / 2)), 64);
    const auto checkBank = [&](Bank &bank) {
        const int y = bank.surface.y();
        if (bank.surface.isEmpty() ||
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
                if (hi <= firstContact && bank.column(point.x()) >= 0) {
                    landed = &bank;
                    pocket = nullptr;
                    firstContact = hi;
                    break;
                }
            }
            previous = current;
            start = end;
        }
    };
    const auto accumulate = [&](Bank &bank, double x, bool spreadOverTime) {
        const int radius = qBound(1, qCeil(amount / (spreadOverTime ? 16 : 85)), 6);
        bank.add(x, amount, radius, spreadOverTime);
        const QRect band = bank.bounds();
        const int margin = columnWidth * (radius + 3);
        if (amount > 0)
            m_dirty += QRect(qFloor(x) - margin, band.y(), margin * 2, band.height()) & band;
    };
    // Кромка каждого окна получает снег из собственного слоя осадков.
    if (!gradual) {
        for (WindowBank &drift : m_windows) {
            checkBank(drift.bank);
            if (landed && keepFalling) {
                accumulate(drift.bank, from.x() + travel.x() * firstContact, false);
                landed = nullptr;
                firstContact = 1;
            }
        }
    }
    if (catchOnTree && !m_treeFrame.isEmpty() && to.y() >= m_treeFrame.top() && from.y() < m_treeFrame.bottom() &&
        qMax(from.x(), to.x()) >= m_treeFrame.left() && qMin(from.x(), to.x()) <= m_treeFrame.right()) {
        for (BranchPocket &branch : m_treeBranches) {
            if (branch.load >= branch.capacity)
                continue;
            double lo = 0, hi = firstContact;
            const double left = branch.center.x() - branch.reach;
            const double right = branch.center.x() + branch.reach;
            if (std::abs(travel.x()) > .001) {
                const double a = (left - from.x()) / travel.x();
                const double b = (right - from.x()) / travel.x();
                lo = qMax(lo, qMin(a, b));
                hi = qMin(hi, qMax(a, b));
            } else if (from.x() < left || from.x() > right) {
                continue;
            }
            const auto clearance = [&](double t) {
                const QPointF pos = from + travel * t;
                return pos.y() - branch.crest(pos.x());
            };
            if (hi < lo || clearance(lo) > 0 || clearance(hi) < 0)
                continue;
            for (int pass = 0; pass < 8; ++pass) {
                const double mid = (lo + hi) * .5;
                if (clearance(mid) < 0) lo = mid;
                else hi = mid;
            }
            firstContact = hi;
            pocket = &branch;
        }
    }
    checkBank(m_dock);
    checkBank(m_floor);
    const double x = from.x() + travel.x() * firstContact;
    if (pocket) {
        if (amount > 0) {
            const double added = qMin(amount, pocket->capacity - pocket->load);
            const double offset = qBound(-pocket->reach * .45, x - pocket->center.x(), pocket->reach * .45);
            pocket->offset = (pocket->offset * pocket->load + added * offset) / (pocket->load + added);
            pocket->load += added;
            m_treeInputDirty = true;
            m_dirty += around(pocket->center, pocket->reach * 2 + 3);
        }
        return true;
    }
    if (!landed)
        return false;
    accumulate(*landed, x, gradual || keepFalling);
    return true;
}

void SnowScene::clearShedSnow()
{
    if (m_shedParticles == 0)
        return;
    const auto removed = std::remove_if(m_particles.begin(), m_particles.end(), [](const Particle &particle) {
        return particle.snowMass > 0;
    });
    m_particles.erase(removed, m_particles.end());
    m_shedParticles = 0;
}

QRegion SnowScene::setGarlandsEnabled(bool enabled)
{
    if (m_garlandsEnabled == enabled)
        return {};
    m_garlandsEnabled = enabled;
    QRegion damage(treeBounds());
    for (const WindowBank &drift : m_windows)
        damage += drift.garland;
    return damage;
}

QRegion SnowScene::visualRegion() const
{
    QRegion region = m_dirty + animatedRegion();
    region += treeBounds();
    if (m_floor.hasSnow())
        region += floorRegion();
    if (m_dock.hasSnow())
        region += m_dock.bounds();
    for (const WindowBank &drift : m_windows) {
        if (m_garlandsEnabled)
            region += drift.garland;
        if (drift.bank.hasSnow())
            region += drift.visible;
    }
    for (const Ball &ball : m_balls)
        region += ball.bounds();
    if (m_aim)
        region += around(m_aim->cursor, aimedRadius + 4);
    return region;
}

QRegion SnowScene::inputRegion() const
{
    if (m_aim)
        return QRegion(QRect(QPoint(), m_size));
    QRegion region = (m_floor.inputRegion() & floorRegion()) + m_dock.inputRegion();
    if (m_treeInputDirty) {
        m_treeInput = {};
        if (!m_treeCanopy.isEmpty()) {
            const QTransform bend = treeBend();
            m_treeInput = QRegion(bend.map(m_treeCanopy).toFillPolygon().toPolygon(), Qt::WindingFill);
            for (const BranchPocket &branch : m_treeBranches) {
                if (branch.load > 0)
                    m_treeInput += QRegion(bend.map(branch.snowShape()).toFillPolygon().toPolygon());
            }
        }
        m_treeInputDirty = false;
    }
    region += m_treeInput;
    for (const Ball &ball : m_balls) {
        if (ball.resting && &ball != m_held)
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
            m_dirty += ball.support->bounds();
            ball.support->top = nullptr;
        }
        ball.support = nullptr;
        ball.celebration = -1;
        ball.resting = false;
        ball.velocity = {};
        m_held = &ball;
        m_grabOffset = point - ball.center;
        break;
    }
    if (!m_held) {
        for (Bank *bank : {&m_dock, &m_floor}) {
            if (!bank->inputRegion().contains(point.toPoint()) ||
                (bank == &m_floor && !floorRegion().contains(point.toPoint())))
                continue;
            m_collecting = bank;
            m_balls.push_back(Ball{});
            m_held = &m_balls.back();
            m_held->center = point;
            m_grabOffset = {};
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
    if ((!m_collecting || m_balls.size() <= maxBalls) && m_dragMoved && point != m_pointer)
        collectAlong(wasMoving ? m_pointer : m_pressPoint, point);
    recordPointer(point, time);
}

bool SnowScene::release(const QPointF &point, double time)
{
    if (!m_held)
        return false;
    move(point, time);
    if (m_collecting && !m_dragMoved && m_held->mass >= 2) {
        m_aim = Aim{m_pressPoint, point, m_collecting, m_held->mass};
        m_dirty += around(point, aimedRadius + 4);
        removeHeldBall();
        cancelDrag();
        return true;
    }
    if (m_collecting && (m_held->mass < smallSnow || m_balls.size() > maxBalls)) {
        cancelDrag();
        return true;
    }
    const QPointF desired = point - m_grabOffset;
    Ball *released = m_held;
    QPointF releaseVelocity;
    if (Ball *base = snapTarget(desired)) {
        attach(*base);
    } else {
        const double grip = qMax(.5, std::sqrt(11 / released->radius()));
        const QPointF velocity = (released->velocity * .32 + gestureVelocity() * .28) * grip;
        releaseVelocity = {qBound(-460.0, velocity.x(), 460.0), qBound(-300.0, velocity.y(), 420.0)};
    }
    cancelDrag();
    released->velocity = releaseVelocity;
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

QRect SnowScene::treeBounds() const
{
    return m_treeFrame.isEmpty() ? QRect() : m_treeFrame.adjusted(-15, 0, 15, 0);
}

QTransform SnowScene::treeBend() const
{
    if (m_treeShake <= 0)
        return {};
    const double progress = 1 - m_treeShake / treeShakeDuration;
    const double ramp = std::sin(progress * twoPi / 2);
    const double envelope = ramp * ramp * std::exp(-2 * progress);
    const double lean = .26 * envelope * std::sin(progress * twoPi * 2.2);
    const double base = m_treeFrame.bottom();
    return QTransform(1, 0, lean, 1, -lean * base, 0);
}

bool SnowScene::shakeTree(const QPointF &point)
{
    if (!treeBounds().contains(point.toPoint()))
        return false;
    const QTransform bend = treeBend();
    const QPointF local = bend.inverted().map(point);
    const auto touchesSnow = [&local](const BranchPocket &branch) {
        return branch.load > 0 && std::abs(local.x() - branch.center.x()) < branch.reach * 2 &&
               branch.snowShape().contains(local);
    };
    if (!m_treeCanopy.contains(local) && !std::any_of(m_treeBranches.cbegin(), m_treeBranches.cend(), touchesSnow))
        return false;

    for (BranchPocket &branch : m_treeBranches) {
        if (branch.load <= 0)
            continue;
        const QPointF origin = bend.map(branch.center + QPointF(branch.offset, 0));
        const int direction = origin.x() < m_treeFrame.center().x() ? -1 : 1;
        shedSnow(origin.x(), qRound(origin.y()), branch.load, QPoint(direction * 32, 0), true);
        branch.load = branch.offset = 0;
    }
    if (m_treeShake <= 0)
        m_treeShake = treeShakeDuration;
    m_treeInputDirty = true;
    m_dirty += treeBounds();
    return true;
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

bool SnowScene::breakSnow(const QPointF &point)
{
    if (m_held || m_aim)
        return false;
    for (const Ball &member : m_balls) {
        if (!member.resting || length(point - member.center) > member.radius() + .5)
            continue;
        shatter(*member.root());
        return true;
    }
    return shakeTree(point);
}

void SnowScene::shatter(const Ball &root, Plow *plow)
{
    const Ball *base = &root;
    const Ball *middle = base->top;
    const Ball *head = middle ? middle->top : nullptr;
    const double push = plow ? plow->direction * 105.0 : 0;
    double moundMass = 0;
    for (const Ball *ball : {base, middle, head}) {
        if (!ball)
            continue;
        m_dirty += ball->bounds();
        const double size = ball->radius();
        const int count = qMin(qBound(16, qRound(size * 1.15), 26),
                               static_cast<int>(maxFragments - m_fragments.size()));
        moundMass += ball->mass * (count > 0 ? .52 : 1.0);
        for (int i = 0; i < count; ++i) {
            const double angle = twoPi * (i + random(.1, .9)) / count;
            const QPointF extent = polar(size * random(.12, .67), angle);
            const QPointF velocity = polar(random(52, 156), angle) + QPointF(push, -random(38, 92));
            const double radius = qBound(1.25, size * random(.055, .17), 4.8);
            m_fragments.push_back({ball->center + extent, velocity, radius, ball->mass * .48 / count,
                                   random(-710, 710), random(0, 360)});
        }
        const int dustCount = qMin(13, static_cast<int>(maxFragments - m_fragments.size()));
        for (int i = 0; i < dustCount; ++i) {
            const double angle = random(0, twoPi);
            const QPointF extent = polar(size * random(.08, .6), angle);
            const QPointF velocity = polar(random(82, 190), angle) + QPointF(push * .62, -62);
            m_fragments.push_back({ball->center + extent, velocity, random(.6, 1.25), 0, 0, 0});
        }
    }
    if (plow) {
        plow->collectedSnow += moundMass;
    } else {
        const double x = base->center.x();
        Bank &bank = surfaceY(*base) == m_dock.surface.y() && m_dock.column(x) >= 0 ? m_dock : m_floor;
        bank.add(x, moundMass, 5, true);
    }
    m_balls.remove_if([=](const Ball &ball) { return &ball == base || &ball == middle || &ball == head; });
}

void SnowScene::cancelDrag()
{
    if (m_held && m_collecting && (m_held->mass < smallSnow || m_balls.size() > maxBalls)) {
        m_collecting->add(m_collectionX, m_held->mass);
        m_dirty += m_collecting->bounds();
        removeHeldBall();
    }
    if (m_held)
        m_held->velocity = {};
    m_held = nullptr;
    m_collecting = nullptr;
    m_samples.clear();
}

void SnowScene::collectAlong(const QPointF &from, const QPointF &to)
{
    // Отсчёты вдоль жеста сохраняют сбор снега при быстрых движениях мыши.
    const QPointF delta = to - from;
    const int steps = qBound(1, qCeil(length(delta) / 6), 300);
    const int start = from == to ? 0 : 1;
    const QRect previous = around(m_held->center, m_held->radius() + 3);
    for (int i = start; i <= steps; ++i) {
        if (from == to && i > 0)
            break;
        const QPointF point = from + delta * (double(i) / steps);
        Bank *bank = m_collecting;
        QPointF contact = point;
        if (!bank) {
            // Поднятый ком захватывает снег нижней частью, даже если курсор выше сугроба.
            const QPointF center = point - m_grabOffset;
            bank = m_dock.column(center.x()) >= 0 ? &m_dock : &m_floor;
            const double depth = bank->heightAt(center.x());
            const double base = bank->surface.y();
            const double crest = base - depth;
            const double radius = m_held->radius();
            if (depth <= 0 || center.y() + radius < crest || center.y() - radius > base)
                continue;
            contact = QPointF(center.x(), crest);
        }
        const double amount = bank->collect(contact, from == to ? 16.0 : 8.0);
        if (amount > 0) {
            m_held->mass += amount;
            const QRect band = bank->bounds();
            m_dirty += QRect(qFloor(contact.x()) - columnWidth * 4, band.y(), columnWidth * 8, band.height()) & band;
        }
    }
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
        if (base.isHead() || base.radius() - m_held->radius() < 1.5)
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
    m_dirty += base.bounds();
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
    ball.velocity.rx() *= std::pow(.55, dt);

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

    double ground = surfaceY(ball) - radius;
    const double dockContact = m_dock.surface.y() - radius;
    if (previous.y() <= dockContact && ball.center.y() > dockContact &&
        m_dock.column(ball.center.x()) >= 0) {
        const double progress = (dockContact - previous.y()) / (ball.center.y() - previous.y());
        const double contactX = previous.x() + (ball.center.x() - previous.x()) * progress;
        if (m_dock.column(contactX) >= 0)
            ground = dockContact;
    }
    if (ball.center.y() >= ground) {
        ball.center.setY(ground);
        ball.velocity.ry() = 0;
        const double speed = ball.velocity.x();
        const double friction = qBound(240.0, 350.0 - (radius - 11) * 2.4, 350.0);
        ball.velocity.setX(std::copysign(qMax(0.0, std::abs(speed) - friction * dt), speed));
        ball.resting = std::abs(ball.velocity.x()) < 8;
    }
}

void SnowScene::setSecondPlowEnabled(bool enabled)
{
    m_secondPlowEnabled = enabled;
    if (!enabled)
        m_followupDelay.reset();
}

void SnowScene::setPlowCadence(double snowSpeed)
{
    m_plowCadence = qMax(1.0, snowSpeed);
}

void SnowScene::startPlow()
{
    if (m_followupDelay || m_floor.surface.isEmpty() ||
        std::any_of(m_plows.begin(), m_plows.end(), [](const Plow &plow) { return plow.active; }))
        return;
    startPlow(m_plows.front(), -m_plows.front().direction);
    if (m_secondPlowEnabled)
        m_followupDelay = followupDelay;
}

void SnowScene::startPlow(Plow &plow, int direction)
{
    plow = {};
    plow.active = true;
    plow.direction = direction;
    plow.x = direction > 0 ? m_floor.surface.left() - 60 : m_floor.surface.right() + 60;
    const int entry = direction > 0 ? m_floor.surface.left() : m_floor.surface.right();
    plow.y = m_dock.column(entry) >= 0 ? m_dock.surface.y() : m_floor.surface.y();
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

void SnowScene::updatePlows(double dt)
{
    for (Smoke &smoke : m_smoke) {
        smoke.age += dt;
        smoke.center.ry() -= dt * 21;
    }
    m_smoke.erase(std::remove_if(m_smoke.begin(), m_smoke.end(), [](const Smoke &smoke) {
        return smoke.age >= .84;
    }), m_smoke.end());

    if (m_floor.surface.isEmpty()) {
        for (Plow &plow : m_plows)
            plow.active = false;
        m_followupDelay.reset();
        m_untilNextPlow = plowInterval;
        return;
    }

    if (m_followupDelay) {
        *m_followupDelay -= dt;
        if (*m_followupDelay <= 0) {
            startPlow(m_plows.back(), m_plows.front().direction);
            m_followupDelay.reset();
        }
    }
    const bool active = std::any_of(m_plows.begin(), m_plows.end(), [](const Plow &plow) { return plow.active; });
    if (active || m_followupDelay) {
        m_untilNextPlow = plowInterval;
        for (Plow &plow : m_plows) {
            if (plow.active)
                advancePlow(plow, dt);
        }
    } else if ((m_untilNextPlow -= dt * m_plowCadence) <= 0) {
        startPlow();
    }
}

void SnowScene::advancePlow(Plow &plow, double dt)
{
    const double travel = plow.direction * plowSpeed * dt;
    plow.x += travel;
    const double blade = plow.x + plow.direction * 25;
    const double x = qBound(double(m_floor.surface.left()), blade, double(m_floor.surface.right()));
    Bank &bank = m_dock.column(x) >= 0 ? m_dock : m_floor;
    const double ground = bank.surface.y();
    plow.y += (ground - plow.y) * (1 - std::exp(-18 * dt));
    plow.angle += plowSpeed * dt / 6;
    plow.phase += dt * 10;
    plow.untilSmoke -= dt;
    if (plow.untilSmoke <= 0) {
        m_smoke.push_back({QPointF(plow.x - plow.direction * 15, plow.y - 25)});
        plow.untilSmoke = .16;
    }

    plow.untilSpray -= dt;
    // Ведро, шарф и собранная фигура остаются целыми после уборки.
    const bool protectedSnow = std::any_of(m_balls.begin(), m_balls.end(), [&](const Ball &ball) {
        return ball.isHead() && std::abs(ball.center.x() - blade) < 36 &&
               std::abs(ball.root()->center.y() + ball.root()->radius() - ground) < 12;
    });
    if (!protectedSnow && dt > 0) {
        const double amount = bank.collect(QPointF(blade, ground - 1), plowSpeed * dt * 1.7);
        if (amount > 0) {
            plow.collectedSnow += amount;
            const QRect band = bank.bounds();
            m_dirty += QRect(qFloor(blade) - 36, band.y(), 72, band.height()) & band;
            if (amount > .15 && plow.untilSpray <= 0) {
                spray(QPointF(blade, ground - 8), plow.direction * 100, amount, true);
                plow.untilSpray = .07;
            }
        }
    }
    for (auto it = m_balls.begin(); dt > 0 && it != m_balls.end();) {
        Ball &ball = *it;
        if (&ball == m_held || !ball.resting || ball.support || ball.inSnowman() ||
            std::abs(ball.center.y() + ball.radius() - ground) > 5 ||
            std::abs(ball.center.x() - blade) > ball.radius() + 8 + std::abs(travel)) {
            ++it;
            continue;
        }
        // Позиция следующего основания сохраняется, даже если верхний ком был создан раньше.
        const Ball *top = ball.top;
        ++it;
        if (it != m_balls.end() && &*it == top)
            ++it;
        shatter(ball, &plow);
    }
    if (plow.x < m_floor.surface.left() - 76 || plow.x > m_floor.surface.right() + 76)
        plow.active = false;
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

QRect SnowScene::Fragment::bounds() const
{
    return around(center, radius * 1.07 + 2);
}

QRect SnowScene::Particle::bounds() const
{
    if (snowMass > 0)
        return SnowCrystal::naturalBounds(center, radius);
    return flake ? SnowCrystal::bounds(center, radius) : around(center, radius + 1);
}

QRegion SnowScene::animatedRegion() const
{
    QRegion dirty;
    for (const Plow &plow : m_plows) {
        if (plow.active)
            dirty += plow.bounds();
    }
    for (const Smoke &smoke : m_smoke)
        dirty += around(smoke.center, 10);
    for (const Ball &ball : m_balls) {
        const Ball *base = ball.root();
        if (!base->resting || (ball.celebration >= 0 && ball.celebration < .85))
            dirty += ball.bounds();
        if (ball.isHead() && ball.celebration < .85) {
            dirty += base->bounds();
            dirty += ball.support->bounds();
        }
    }
    for (const Flight &flight : m_flights)
        dirty += around(flight.center(), flight.visibleRadius() + 2);
    for (const Splat &splat : m_splats)
        dirty += around(splat.center(), splat.radius * 1.6 + 16);
    for (const Fragment &fragment : m_fragments)
        dirty += fragment.bounds();
    for (const Particle &particle : m_particles)
        dirty += particle.bounds();
    return dirty;
}

QRegion SnowScene::advanceGarlands(double dt)
{
    QRegion dirty;
    for (WindowBank &window : m_windows) {
        if (window.revealDelay > 0) {
            window.revealDelay = qMax(0.0, window.revealDelay - dt);
            if (m_garlandsEnabled && window.revealDelay == 0)
                dirty += window.garland;
        }
    }
    return dirty;
}

bool SnowScene::garlandsPending() const
{
    return m_garlandsEnabled && std::any_of(m_windows.cbegin(), m_windows.cend(), [](const WindowBank &window) {
        return window.revealDelay > 0 && !window.garland.isEmpty();
    });
}

SnowScene::Garlands SnowScene::garlandsAbove(const QString &windowId) const
{
    Garlands result;
    if (!m_garlandsEnabled || windowId.isEmpty())
        return result;
    bool above = false;
    for (const WindowBank &window : m_windows) {
        if (above && window.revealDelay <= 0 && !window.garland.isEmpty())
            result.append({window.window.rect, window.garland});
        above = above || window.window.id == windowId;
    }
    return result;
}

QRegion SnowScene::advance(double dt, double wind)
{
    QRegion dirty = animatedRegion();
    m_hoverCooldown = qMax(0.0, m_hoverCooldown - dt);
    if (m_treeShake > 0) {
        m_treeShake = qMax(0.0, m_treeShake - dt);
        m_treeInputDirty = true;
        dirty += treeBounds();
    }
    if (m_garlandsEnabled && !m_treeFrame.isEmpty()) {
        m_treeBlinkTime += dt;
        if (m_treeBlinkTime >= treeBlinkInterval) {
            const int flashes = qFloor(m_treeBlinkTime / treeBlinkInterval);
            m_treeBlinkTime -= flashes * treeBlinkInterval;
            if (flashes % 2)
                m_treeAlternateLights = !m_treeAlternateLights;
            dirty += treeBounds();
        }
    }
    dirty += m_floor.advanceDeposits(dt);
    dirty += m_dock.advanceDeposits(dt);
    dirty += advanceGarlands(dt);
    updatePlows(dt);
    if (m_held) {
        // Критически затухающая пружина с точным шагом для неподвижной цели.
        const QPointF destination = m_pointer - m_grabOffset;
        const double response = qMax(23.0, 31.0 - (m_held->radius() - 11) * .15);
        const QPointF offset = m_held->center - destination;
        const QPointF impulse = (m_held->velocity + offset * response) * dt;
        const double damping = std::exp(-response * dt);
        m_held->center = destination + (offset + impulse) * damping;
        m_held->velocity = (m_held->velocity - impulse * response) * damping;
    }

    // Сначала перемещаем основания, затем обновляем позиции всех прикреплённых комов.
    const int steps = qMax(1, qCeil(dt * 120));
    const double step = dt / steps;
    for (Ball &ball : m_balls) {
        if (&ball != m_held && !ball.support && !ball.resting && dt > 0) {
            for (int i = 0; i < steps && !ball.resting; ++i)
                fall(ball, step);
            if (ball.resting)
                dirty += ball.bounds();
        }
    }
    for (Ball &ball : m_balls) {
        if (!ball.support && ball.top) {
            Ball &middle = *ball.top;
            const QPointF position = middle.snapPoint(ball);
            if (middle.center != position) {
                middle.center = position;
                if (middle.top)
                    middle.top->center = middle.top->snapPoint(middle);
                if (ball.resting) {
                    dirty += middle.bounds();
                    if (middle.top)
                        dirty += middle.top->bounds();
                }
            }
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

    const auto landedFragments = std::remove_if(m_fragments.begin(), m_fragments.end(), [&](Fragment &fragment) {
        fragment.age += dt;
        const bool dust = fragment.mass == 0;
        fragment.angle += fragment.spin * dt;
        fragment.velocity.rx() *= std::exp((dust ? -4.2 : -1.25) * dt);
        fragment.velocity.ry() += (dust ? 145 : 480) * dt;
        const QPointF previous = fragment.center;
        fragment.center += fragment.velocity * dt + QPointF(wind * (dust ? .42 : .10) * dt, 0);
        if (dust)
            return fragment.age >= .55 || fragment.center.y() >= m_size.height();

        const QPointF edge(0, fragment.radius * .7);
        if (!fragment.bounced && fragment.radius >= 2.35 && fragment.velocity.y() > 58 &&
            deposit(previous + edge, fragment.center + edge, 0, false, true)) {
            fragment.center.setY(previous.y());
            fragment.velocity.ry() *= -.26;
            fragment.velocity.rx() *= .6;
            fragment.spin *= -.45;
            fragment.bounced = true;
            return false;
        }
        return deposit(previous + edge, fragment.center + edge, fragment.mass, false, true) ||
               fragment.center.y() + edge.y() >= m_size.height() || fragment.age > 2.5;
    });
    m_fragments.erase(landedFragments, m_fragments.end());

    const auto firstRemoved = std::remove_if(m_particles.begin(), m_particles.end(), [&](Particle &particle) {
        const QPointF oldCenter = particle.center;
        particle.age += dt;
        const bool shed = particle.snowMass > 0;
        if (shed) {
            particle.velocity.setY(qMin(230.0, particle.velocity.y() + 85 * dt));
            particle.velocity.rx() *= std::exp(-.65 * dt);
        } else {
            particle.velocity.ry() += (particle.flake ? 125 : 510) * dt;
        }
        if (particle.flake)
            particle.velocity.rx() += std::sin(particle.age * 5 + particle.radius) * (shed ? 48 : 22) * dt;
        particle.center += particle.velocity * dt;
        if (particle.flake)
            particle.center.rx() += wind * .65 * dt;
        const bool spray = particle.life < 5;
        const double amount = spray ? -particle.snowMass : shed ? particle.snowMass : (particle.flake ? 1.4 : .7);
        const bool expired = deposit(oldCenter, particle.center, amount, !particle.fromTree, shed) ||
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
        if (m_garlandsEnabled && drift.revealDelay <= 0 && !drift.garland.isEmpty()) {
            const QRegion exposed = repaint.isEmpty() ? drift.garland : drift.garland & repaint;
            if (!exposed.isEmpty())
                SnowProps::drawGarland(p, drift.window.rect, exposed);
        }
        if (drift.visible.isEmpty() || (!repaint.isEmpty() && !repaint.intersects(drift.visible)))
            continue;
        p.save();
        p.setClipRegion(drift.visible, Qt::IntersectClip);
        drift.bank.paint(p);
        p.restore();
    }

    if (!m_treeFrame.isEmpty() && (repaint.isEmpty() || repaint.intersects(treeBounds()))) {
        p.save();
        p.setWorldTransform(treeBend(), true);
        SnowProps::drawTree(p, m_treeFrame);
        for (const BranchPocket &branch : m_treeBranches) {
            if (branch.load <= 0)
                continue;
            const QPainterPath cap = branch.snowShape();
            const QRectF bounds = cap.boundingRect();
            QLinearGradient frost(bounds.topLeft(), bounds.bottomLeft());
            frost.setColorAt(0, QColor(255, 255, 255));
            frost.setColorAt(.62, QColor(233, 246, 251));
            frost.setColorAt(1, QColor(161, 195, 211));
            p.fillPath(cap, frost);
        }
        if (m_garlandsEnabled)
            SnowProps::drawTreeLights(p, m_treeFrame, m_treeAlternateLights);
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
    for (const Plow &plow : m_plows) {
        if (plow.active && (repaint.isEmpty() || repaint.intersects(plow.bounds())))
            SnowProps::drawPlow(p, plow.x, plow.y, plow.direction, plow.collectedSnow, plow.angle, plow.phase);
    }
    for (const Ball &ball : m_balls) {
        if (&ball != m_held && (repaint.isEmpty() || repaint.intersects(ball.bounds())))
            SnowProps::drawSnowball(p, ball.center, ball.radius(), ball.resting && !ball.support);
    }
    const auto part = [](const Ball &ball) {
        return SnowProps::Ball{ball.center.x(), ball.center.y(), ball.radius()};
    };
    for (const Ball &head : m_balls) {
        if (!head.isHead())
            continue;
        const Ball &middle = *head.support;
        const Ball &base = *middle.support;
        if (!repaint.isEmpty() && !repaint.intersects(head.bounds().united(middle.bounds()).united(base.bounds())))
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
        SnowProps::drawSnowball(p, m_held->center, m_held->radius());
    if (m_aim && (repaint.isEmpty() || repaint.intersects(around(m_aim->cursor, aimedRadius + 4))))
        SnowProps::drawSnowball(p, m_aim->cursor, aimedRadius);

    for (const Flight &flight : m_flights)
        SnowProps::drawSnowball(p, flight.center(), flight.visibleRadius());

    static const QPolygonF chip{{-.94, -.24}, {-.37, -.87}, {.55, -.73}, {1, .02},
                                {.33, .86}, {-.63, .59}};
    for (const Fragment &fragment : m_fragments) {
        if (!repaint.isEmpty() && !repaint.intersects(fragment.bounds()))
            continue;
        if (fragment.mass == 0) {
            const double opacity = qMax(0.0, 1 - fragment.age / .55);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(233, 247, 255, qRound(185 * opacity)));
            p.drawEllipse(fragment.center, fragment.radius, fragment.radius * .76);
            continue;
        }
        p.save();
        p.translate(fragment.center);
        p.rotate(fragment.angle);
        p.scale(fragment.radius, fragment.radius);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(199, 225, 244, 235));
        p.drawPolygon(chip);
        p.setBrush(QColor(249, 253, 255, 225));
        p.drawEllipse(QPointF(-.25, -.31), .45, .28);
        p.restore();
    }

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
        if (!repaint.isEmpty() && !repaint.intersects(particle.bounds()))
            continue;
        if (particle.snowMass > 0) {
            SnowCrystal::paintNatural(p, particle.center, particle.radius);
        } else if (particle.flake) {
            SnowCrystal::paint(p, particle.center, particle.radius, .86, particle.glyph);
        } else {
            p.setBrush(QColor(235, 250, 255, 231));
            p.drawEllipse(particle.center, particle.radius, particle.radius);
        }
    }
}
