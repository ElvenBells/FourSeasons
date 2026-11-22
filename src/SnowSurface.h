#pragma once

#include <QRect>
#include <QString>

struct SnowSurface {
    QString id;
    QRect rect;
    bool dock = false;
    bool fullscreen = false;
    bool maximized = false;
    bool panelPopup = false;

    bool operator==(const SnowSurface &other) const
    {
        return id == other.id && rect == other.rect && dock == other.dock &&
               fullscreen == other.fullscreen && maximized == other.maximized && panelPopup == other.panelPopup;
    }
};
