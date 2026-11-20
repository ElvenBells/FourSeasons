#pragma once

#include <QRect>
#include <QString>

struct SnowSurface {
    QString id;
    QRect rect;
    bool dock = false;

    bool operator==(const SnowSurface &other) const
    {
        return id == other.id && rect == other.rect && dock == other.dock;
    }
};
