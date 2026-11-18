#pragma once

#include <QElapsedTimer>
#include <QRect>
#include <QRegion>
#include <QTimer>
#include <QWidget>

#include <vector>

class QScreen;

// Полноэкранное прозрачное окно со снегом. Не принимает ни фокус, ни мышь.
//
// Снег не рисуется в области активного окна («дырка» m_hole): она вырезается
// через QPainter::setClipRegion, поэтому при перемещении/смене активного окна
// снег над ним пропадает сразу.
class SnowWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SnowWindow(QWidget *parent = nullptr);

    // Геометрия экрана, который накрывает оверлей (глобальные логические координаты).
    QRect screenRect() const { return m_screenRect; }

public Q_SLOTS:
    // Геометрия активного окна в ГЛОБАЛЬНЫХ координатах. Пустой QRect — активного
    // окна нет (фолбэк: снег по всему экрану).
    void setActiveRect(const QRect &globalRect);

Q_SIGNALS:
    // Оверлей сменил экран/размер — трекеру надо знать (KWin-скрипт перепозиционирует окно).
    void overlayGeometryChanged(const QRect &globalRect);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private Q_SLOTS:
    void tick();                       // шаг физики + перерисовка (по таймеру)
    void adoptScreen(QScreen *screen); // привязка к (новому) основному экрану
    void applyWaylandInputRegion();

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
        QRect rect;               // где снежинка была нарисована (для частичной перерисовки)
    };

    void configureWindowFlags();
    void updateScreenGeometry();
    void applyHole();
    void initFlakes();
    void respawn(Flake &f, bool initial);
    static QRect flakeRect(const Flake &f);

    std::vector<Flake> m_flakes;
    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_time = 0;

    QScreen *m_screen = nullptr;
    QRect m_screenRect;       // экран в глобальных координатах = положение оверлея
    QRect m_activeGlobal;     // активное окно (глобальные координаты)
    QRegion m_hole;           // активное окно в координатах оверлея
    bool m_debug = false;     // SNOW_DEBUG=1 — рисовать рамку вокруг «дырки»
};
