/**
 * \file PlatformChrome.cpp
 * \brief Реализация spotty::PlatformChrome для Windows и систем без такой возможности.
 *
 * \note Вариант для macOS лежит в PlatformChrome_mac.mm: там нужен Objective-C, и собрать
 *       его обычным компилятором C++ нельзя. Этот файл на macOS определения не даёт.
 */
#include "PlatformChrome.h"

#include <QWidget>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace spotty::PlatformChrome {

#if defined(Q_OS_WIN)

namespace {

/**
 * \brief Тёмный заголовок окна: `DWMWA_USE_IMMERSIVE_DARK_MODE`.
 *
 * Два номера, потому что до Windows 10 20H1 атрибут имел номер 19, а начиная с неё — 20.
 * Заранее различить сборки нельзя (`GetVersionEx` врёт без манифеста совместимости),
 * поэтому пробуем сначала нынешний, затем старый. Промах возвращает ошибку и ничего не
 * портит.
 */
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kUseImmersiveDarkModeLegacy = 19;

using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);

/**
 * \brief Адрес `DwmSetWindowAttribute`, найденный при первом обращении.
 *
 * Через GetProcAddress, а не компоновкой с dwmapi.lib: библиотека не нужна ни для чего
 * другого, а её отсутствие не должно мешать запуску.
 */
DwmSetWindowAttributeFn resolveDwmSetWindowAttribute()
{
    static DwmSetWindowAttributeFn function = [] {
        HMODULE module = LoadLibraryW(L"dwmapi.dll");
        return module ? reinterpret_cast<DwmSetWindowAttributeFn>(
                            GetProcAddress(module, "DwmSetWindowAttribute"))
                      : nullptr;
    }();
    return function;
}

} // namespace

void applyWindowAppearance(QWidget *window, bool dark)
{
    if (!window || !window->isWindow())
        return;

    DwmSetWindowAttributeFn setAttribute = resolveDwmSetWindowAttribute();
    if (!setAttribute)
        return;

    auto handle = reinterpret_cast<HWND>(window->winId());
    BOOL value = dark ? TRUE : FALSE;

    if (FAILED(setAttribute(handle, kUseImmersiveDarkMode, &value, sizeof(value))))
        setAttribute(handle, kUseImmersiveDarkModeLegacy, &value, sizeof(value));

    // Заголовок уже нарисован и сам себя не перерисует: без этого смена темы на лету
    // оставляет прежний цвет до первого изменения размера окна.
    SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

#elif !defined(Q_OS_MACOS)

void applyWindowAppearance(QWidget *, bool)
{
    // X11 и Wayland: заголовок рисует оконный менеджер по своей теме, и приложению его не
    // отдают. Оставить как есть — единственное, что здесь возможно.
}

#endif

} // namespace spotty::PlatformChrome
