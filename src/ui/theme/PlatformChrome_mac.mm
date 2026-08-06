/**
 * \file PlatformChrome_mac.mm
 * \brief Реализация spotty::PlatformChrome для macOS.
 *
 * Отдельный файл, а не `#ifdef` в PlatformChrome.cpp: нужен Objective-C, и весь остальной
 * файл пришлось бы компилировать им же.
 */
#include "PlatformChrome.h"

#include <QWidget>

#import <AppKit/AppKit.h>

namespace spotty::PlatformChrome {

void applyWindowAppearance(QWidget *window, bool dark)
{
    if (!window || !window->isWindow())
        return;

    // winId() на macOS возвращает NSView окна, а не NSWindow: оформление задаётся окну,
    // поэтому идём от вида к его окну. У ещё не показанного виджета окна нет.
    auto *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *nativeWindow = view ? [view window] : nil;
    if (!nativeWindow)
        return;

    // NSAppearance по имени появился в 10.14 вместе с самим тёмным режимом. На более
    // старых системах тёмного заголовка не существует, и оставить светлый — правильно.
    if (@available(macOS 10.14, *)) {
        nativeWindow.appearance =
            [NSAppearance appearanceNamed:(dark ? NSAppearanceNameDarkAqua
                                                : NSAppearanceNameAqua)];
    }
}

} // namespace spotty::PlatformChrome
