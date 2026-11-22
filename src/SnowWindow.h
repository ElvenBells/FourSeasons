#pragma once

#include "SeasonalWeather.h"
#include "SnowCrystal.h"
#include "SnowScene.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QRect>
#include <QRegion>
#include <QTimer>
#include <QWidget>

#include <vector>

#ifdef SNOW_HAS_LAYER_SHELL
#include <QProcess>
#endif

class QScreen;

// Полноэкранное прозрачное окно. Мышь принимают только сугробы и лежащие комы.
//
// Снег не рисуется в области активного окна («дырка» m_hole): она вырезается
// через QPainter::setClipRegion, поэтому при перемещении/смене активного окна
// снег над ним пропадает сразу.
class SnowWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SnowWindow(QWidget *parent = nullptr);
    ~SnowWindow() override;
    Season season() const { return m_season; }
    AutumnOptions autumnOptions() const { return m_weather.options(); }
    void setSeason(Season season);
    void setAutumnOptions(const AutumnOptions &options);
    void setSpringRain(bool enabled);
    void setBubbles(bool enabled);
    void setSeasonTool(SeasonTool tool);
    void setSeasonFan(bool enabled);
    void setFireflyJar(bool enabled);
    void startLeafBlower();
    bool launchPaperBoat();
    void setGarlandsEnabled(bool enabled);
    void setSnowIntensity(int percent);
    void setSnowSpeed(int percent);
    void setSnowWind(int percent);
    void setPrecipitation(const Precipitation::Settings &settings);

    // Геометрия экрана, который накрывает оверлей (глобальные логические координаты).
    QRect screenRect() const { return m_screenRect; }

public Q_SLOTS:
    void startPlow();
    // Геометрия активного окна в ГЛОБАЛЬНЫХ координатах. Пустой QRect — активного
    // окна нет (фолбэк: снег по всему экрану).
    void setActiveRect(const QRect &globalRect, bool fullscreen);
    void setSurfaces(const QList<SnowSurface> &surfaces);
    void setGlobalCursorPos(const QPoint &globalPosition);

Q_SIGNALS:
    // Оверлей сменил экран/размер — трекеру надо знать (KWin-скрипт перепозиционирует окно).
    void overlayGeometryChanged(const QRect &globalRect);
    void seasonalCursorTrackingChanged(bool enabled);

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
    struct Flake : Precipitation::Particle {
        double radius = 1;
        double alpha = 1;
        SnowCrystal::Glyph glyph = SnowCrystal::Glyph::Asterisk;
        QRect rect;               // где снежинка была нарисована (для частичной перерисовки)
    };

    void configureWindowFlags();
    void updateScreenGeometry();
    void refreshLayout();
    void updatePanelPopup();
    void updatePausedGarlands();
    const SnowSurface *fullscreenSurface() const;
    void refreshGarlandOverlay(const QRegion &damage);
    bool useGarlandLayer() const;
#ifdef SNOW_HAS_LAYER_SHELL
    void sendLayerGarlands();
    QProcess m_garlandLayer;
#endif
    bool coveredByFullscreen() const;
    QRect winterWorkArea() const;
    void resumeWeather();
    QRegion updateHole();
    void updateDamage(const QRegion &damage);
    void updateSceneLayout();
    void applyInputRegion();
    void pollWindowHover();
    double pointerTime(const QMouseEvent *event) const;
    void initFlakes();
    void spawnFlakesFromTop(int first);
    void respawn(Flake &f);
    static QRect flakeRect(const Flake &f);

    std::vector<Flake> m_flakes;
    static constexpr int kNormalFlakeCount = 180;
    int m_flakeCount = kNormalFlakeCount;
    double m_snowSpeed = 1.0;
    double m_snowWind = 0;
    QTimer m_timer;
    QTimer m_layoutTimer;
    QTimer m_resumeTimer;
    QTimer m_popupTimer;
    QElapsedTimer m_clock;
    QElapsedTimer m_gestureClock;
    quint32 m_pressTimestamp = 0;
    double m_time = 0;
    double m_inputRefresh = 0;
    double m_hoverRefresh = 0;

    QPointer<QScreen> m_screen;
    QRect m_screenRect;       // экран в глобальных координатах = положение оверлея
    QRect m_activeGlobal;     // активное окно (глобальные координаты)
    bool m_activeFullscreen = false;
    bool m_suspended = false;
    QRect m_popupFrame;
    SnowScene::Garlands m_pausedGarlands;
    bool m_showGarlands = true;
    QRegion m_hole;           // активное окно в координатах оверлея
    QList<SnowSurface> m_surfaces;
    bool m_receivedSurfaces = false;
    SnowScene m_scene;
    SeasonalWeather m_weather;
    Season m_season = Season::Winter;
    QRegion m_inputRegion;
    bool m_inputRegionSet = false;
    bool m_interactive = false;
    bool m_x11 = false;
    bool m_debug = false;     // SNOW_DEBUG=1 — рисовать рамку вокруг «дырки»
};
