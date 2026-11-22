#pragma once

#include "ActiveWindowTracker.h"

#include <QTimer>

// Бэкенд для KDE Plasma (KWin), работает и в Wayland-, и в X11-сессии.
//
// В Wayland приложение НЕ МОЖЕТ ни узнать геометрию чужих окон, ни само
// выставить себе положение/«поверх всех». Единственный штатный путь в KDE —
// запустить маленький скрипт внутри KWin (через D-Bus: org.kde.KWin /Scripting).
// Скрипт делает три вещи:
//   1) шлёт нам геометрию активного окна (D-Bus вызов geometryMessage);
//   2) «прижимает» окно оверлея: keepAbove, без панели задач, на весь экран;
//   3) если оверлей случайно получил фокус — возвращает его прежнему окну.
// Скрипт совместим и с Plasma 5.27 (Debian 12), и с Plasma 6.
class KWinTracker : public ActiveWindowTracker
{
    Q_OBJECT
    // Должно совпадать с kInterface в KWinTracker.cpp
    Q_CLASSINFO("D-Bus Interface", "org.snowoverlay.Tracker")
public:
    explicit KWinTracker(QObject *parent = nullptr);
    ~KWinTracker() override;

    QString name() const override { return QStringLiteral("KWin (D-Bus + KWin-скрипт)"); }
    bool start() override;
    void stop() override;
    void setOverlayGeometry(const QRect &globalRect) override;
    void setSeasonalCursorTracking(bool enabled) override { m_seasonalCursorTracking = enabled; }

public Q_SLOTS:
    // Вызывается из KWin-скрипта. Формат: "0" (нет активного окна)
    // или "1,x,y,w,h" с префиксом "F:" для полноэкранного окна. Типы чисел при передаче из JS
    // в D-Bus бывают разными (int/double), а строка проходит всегда.
    Q_SCRIPTABLE void geometryMessage(const QString &scriptToken, const QString &message);
    Q_SCRIPTABLE bool cursorMessage(const QString &scriptToken, const QString &message);

Q_SIGNALS:
    void initialGeometryReceived();

private Q_SLOTS:
    void showingDesktopChanged(bool showing);

private:
    void reloadScript();
    void publishGeometry();
    QList<SnowSurface> visibleSurfaces() const;
    bool loadScript();
    void unloadScript();
    QString buildScript() const;

    QRect m_overlay;
    QRect m_activeRect;
    bool m_activeFullscreen = false;
    bool m_showingDesktop = false;
    QList<SnowSurface> m_surfaces;
    QString m_scriptToken;
    QTimer m_reloadTimer;
    bool m_dbusRegistered = false;
    bool m_scriptLoaded = false;
    bool m_receivedInitialGeometry = false;
    bool m_seasonalCursorTracking = false;
};
