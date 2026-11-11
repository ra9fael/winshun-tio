// qt_window_icon.h - the window's title-bar, taskbar and Alt+Tab icon.
//
// The frames are the ones winshun-plugin-icons rasterised from the Phosphor
// wave-square glyph (icons/, embedded below as :/plugin-icons). Setting the
// application icon is not optional: an exe icon resource alone does not change
// what a window shows, and Win顺 does the same for its own window.
#pragma once
#include <QGuiApplication>
#include <QIcon>

inline void setPluginWindowIcon()
{
    QIcon icon;
    icon.addFile(QStringLiteral(":/plugin-icons/app-16.png"), QSize(16, 16));
    icon.addFile(QStringLiteral(":/plugin-icons/app-20.png"), QSize(20, 20));
    icon.addFile(QStringLiteral(":/plugin-icons/app-24.png"), QSize(24, 24));
    icon.addFile(QStringLiteral(":/plugin-icons/app-32.png"), QSize(32, 32));
    icon.addFile(QStringLiteral(":/plugin-icons/app-40.png"), QSize(40, 40));
    icon.addFile(QStringLiteral(":/plugin-icons/app-48.png"), QSize(48, 48));
    icon.addFile(QStringLiteral(":/plugin-icons/app-64.png"), QSize(64, 64));
    icon.addFile(QStringLiteral(":/plugin-icons/app-256.png"), QSize(256, 256));
    QGuiApplication::setWindowIcon(icon);
}
