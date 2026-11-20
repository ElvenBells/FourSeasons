#pragma once

#include "AutumnWeather.h"
#include "Season.h"
#include "SnowCrystal.h"
#include "SnowScene.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QRect>
#include <QRegion>
#include <QTimer>
#include <QWidget>

#include <vector>

class QScreen;

// Полноэкранное прозрачное окно. Зимой мышь принимают только сугробы и лежащие комы,
// осенью (дождь/листопад) окно полностью прозрачно для мыши.
//
// Снег и осадки не рисуются в области активного окна («дырка» m_hole): она вырезается
// через QPainter::setClipRegion, поэтому при перемещении/смене активного окна
// снег над ним пропадает сразу.
class SnowWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SnowWindow(QWidget *parent = nullptr);

    // Геометрия экрана, который накрывает оверлей (глобальные логические координаты).
    QRect screenRect() const { return m_screenRect; }

    Season season() const { return m_season; }
    AutumnOptions autumnOptions() const { return m_autumn.options(); }
    // Смена сезона сохраняет зимнюю сцену: сугробы и снеговики вернутся, когда снова наступит зима.
    void setSeason(Season season);
    // Хотя бы одно из rain/leaves должно быть true — иначе вызов игнорируется.
    void setAutumnOptions(const AutumnOptions &options);

public Q_SLOTS:
    void startPlow();
    // Геометрия активного окна в ГЛОБАЛЬНЫХ координатах. Пустой QRect — активного
    // окна нет (фолбэк: снег по всему экрану).
    void setActiveRect(const QRect &globalRect);
    void setSurfaces(const QList<SnowSurface> &surfaces);
    void setGlobalCursorPos(const QPoint &globalPosition);

Q_SIGNALS:
    // Оверлей сменил экран/размер — трекеру надо знать (KWin-скрипт перепозиционирует окно).
    void overlayGeometryChanged(const QRect &globalRect);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private Q_SLOTS:
    void tick();                       // шаг физики + перерисовка (по таймеру)
    void adoptScreen(QScreen *screen); // привязка к (новому) основному экрану

private:
    struct Flake {
        double x = 0, y = 0;      // текущая позиция центра
        double baseX = 0;         // «ось», вокруг которой качается снежинка
        double radius = 1;        // размер
        double speed = 30;        // скорость падения, px/с
        double alpha = 1;         // прозрачность 0..1
        double swayAmp = 10;      // амплитуда покачивания, px
        double swayFreq = 1;      // частота покачивания, рад/с
        double phase = 0;         // текущая фаза синусоиды
        double windFactor = 1;    // насколько сильно сносит ветром
        SnowCrystal::Glyph glyph = SnowCrystal::Glyph::Asterisk;
        QRect rect;               // где снежинка была нарисована (для частичной перерисовки)
    };

    void configureWindowFlags();
    void updateScreenGeometry();
    void applyHole();
    void updateSceneLayout();
    void applyInputRegion();
    void pollWindowHover();
    void tickAutumn(double dt);
    double pointerTime(const QMouseEvent *event) const;
    void initFlakes();
    void respawn(Flake &f, bool initial);
    static QRect flakeRect(const Flake &f);

    std::vector<Flake> m_flakes;
    QTimer m_timer;
    QTimer m_layoutTimer;
    QElapsedTimer m_clock;
    QElapsedTimer m_gestureClock;
    quint32 m_pressTimestamp = 0;
    double m_time = 0;
    double m_inputRefresh = 0;
    double m_hoverRefresh = 0;

    QPointer<QScreen> m_screen;
    QRect m_screenRect;       // экран в глобальных координатах = положение оверлея
    QRect m_activeGlobal;     // активное окно (глобальные координаты)
    QRegion m_hole;           // активное окно в координатах оверлея
    QList<SnowSurface> m_surfaces;
    SnowScene m_scene;
    AutumnWeather m_autumn;
    Season m_season = Season::Winter;
    QRegion m_inputRegion;
    bool m_inputRegionSet = false;
    bool m_interactive = false;
    bool m_debug = false;     // SNOW_DEBUG=1 — рисовать рамку вокруг «дырки»
};
