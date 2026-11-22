#include "SnowProps.h"
#include "SnowScene.h"

#include <LayerShellQt/Window>

#include <QApplication>
#include <QDataStream>
#include <QPainter>
#include <QScreen>
#include <QSocketNotifier>
#include <QWidget>
#include <QWindow>

#include <unistd.h>

namespace {
class PanelGarland final : public QWidget
{
public:
    PanelGarland() : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowTransparentForInput |
                                     Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        winId();
        auto *layer = LayerShellQt::Window::get(windowHandle());
        // KWin распознаёт эту прозрачную поверхность как служебный индикатор.
        layer->setScope(QStringLiteral("on-screen-display"));
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors({LayerShellQt::Window::AnchorTop, LayerShellQt::Window::AnchorBottom,
                           LayerShellQt::Window::AnchorLeft, LayerShellQt::Window::AnchorRight});
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    }

    void setGarlands(SnowScene::Garlands garlands, QScreen *screen)
    {
        m_garlands = std::move(garlands);
        if (m_garlands.isEmpty() || !screen) {
            hide();
            return;
        }
        if (windowHandle()->screen() != screen) {
            hide();
            setScreen(screen);
        }
        show();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        for (const auto &garland : m_garlands)
            SnowProps::drawGarland(painter, garland.first, garland.second);
    }

private:
    SnowScene::Garlands m_garlands;
};
} // namespace

int runPanelGarland(QApplication &app)
{
    PanelGarland window;
    app.setQuitOnLastWindowClosed(false);
    // Родитель присылает рамки и видимые области гирлянд. Закрытие канала завершает оверлей.
    QSocketNotifier commands(STDIN_FILENO, QSocketNotifier::Read);
    QByteArray buffer;
    QObject::connect(&commands, &QSocketNotifier::activated, &app, [&] {
        char data[4096];
        const auto count = read(STDIN_FILENO, data, sizeof(data));
        if (count <= 0) {
            app.quit();
            return;
        }
        buffer.append(data, count);
        qsizetype newline;
        while ((newline = buffer.indexOf('\n')) >= 0) {
            const QByteArray payload = QByteArray::fromBase64(buffer.first(newline));
            buffer.remove(0, newline + 1);
            QDataStream stream(payload);
            qsizetype screen = -1;
            SnowScene::Garlands garlands;
            stream >> screen >> garlands;
            if (stream.status() == QDataStream::Ok)
                window.setGarlands(std::move(garlands), QGuiApplication::screens().value(screen));
        }
    });
    return app.exec();
}
