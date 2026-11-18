#include "ActiveWindowTracker.h"
#include "Constants.h"
#include "SnowWindow.h"

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSystemTrayIcon>

// Иконка в трее рисуется кодом: белая снежинка на синем круге (внешние файлы не нужны).
static QIcon makeTrayIcon()
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 110, 200));
    p.drawEllipse(2, 2, 60, 60);

    p.setPen(QPen(Qt::white, 4, Qt::SolidLine, Qt::RoundCap));
    p.translate(32, 32);
    for (int i = 0; i < 3; ++i) {      // три линии через центр = шесть лучей
        p.drawLine(0, -22, 0, 22);
        p.rotate(60);
    }
    p.end();

    return QIcon(pm);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromLatin1(SnowIds::kAppId));
    QApplication::setApplicationDisplayName(QStringLiteral("SnowOverlay"));
    // app_id окна в Wayland берётся из desktopFileName — по нему KWin-скрипт найдёт оверлей.
    QGuiApplication::setDesktopFileName(QString::fromLatin1(SnowIds::kAppId));
    // Окно оверлея не должно «держать» программу: выходим только через трей.
    QApplication::setQuitOnLastWindowClosed(false);

    qInfo().noquote() << "[SnowOverlay] Платформа Qt:" << QGuiApplication::platformName();

    // --- Окно со снегом ---------------------------------------------------
    SnowWindow window;

    // --- Определение активного окна (может быть nullptr — это нормально) ----
    ActiveWindowTracker *tracker = ActiveWindowTracker::create(&app, window.screenRect());
    if (tracker) {
        QObject::connect(tracker, &ActiveWindowTracker::activeRectChanged,
                         &window, &SnowWindow::setActiveRect);
        QObject::connect(&window, &SnowWindow::overlayGeometryChanged,
                         tracker, &ActiveWindowTracker::setOverlayGeometry);
        // Корректно выгружаем KWin-скрипт при выходе.
        QObject::connect(&app, &QCoreApplication::aboutToQuit, tracker, &ActiveWindowTracker::stop);
    } else if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        qWarning() << "[SnowOverlay] Wayland без KWin: окно нельзя поднять поверх остальных."
                   << "Запустите с QT_QPA_PLATFORM=xcb (режим XWayland).";
    }

    window.show();

    // --- Иконка в системном трее с единственным пунктом «Выход» ---------------
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        qWarning() << "[SnowOverlay] Системный трей недоступен — закрыть программу можно только через kill/Ctrl+C.";

    QMenu trayMenu;
    QAction *quitAction = trayMenu.addAction(QObject::tr("Выход"));
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    QSystemTrayIcon tray(makeTrayIcon());
    tray.setToolTip(QStringLiteral("SnowOverlay"));
    tray.setContextMenu(&trayMenu);
    tray.show();

    return app.exec();
}
