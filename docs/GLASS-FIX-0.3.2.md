# Tablet Pro Radial 0.3.2 — visible, shaped Glass

Quit older radial versions, run Tablet-Pro-Radial-0.3.2-Glass-Fix-Setup.exe,
and choose Yes to open the new version with Glass enabled. Use the new
Tablet Pro Radial 0.3.2 shortcut. Your saved menus and settings are preserved.

## What changed

Version 0.3.1 accepted a Windows system-backdrop request but could display a
solid gray material. Its native window region did not actually clip that
material, and the main menu artwork was fully opaque.

This version uses Windows.UI.Composition's host backdrop brush. A geometric
clip in the compositor limits the live blur to the menu controls, including
holes and disconnected pieces. A separate non-activating, click-through window
sits directly beneath the transparent Qt overlay. No screen capture is used
by the app. Moving/animating, hiding, and turning Glass off release that surface.

When Glass is on, root buttons, dock buttons, children, and the hub use translucent
fills. Icons remain opaque. Glass off retains the previous solid button artwork.
Windows 11 supports this renderer; unsupported systems use translucent paint.
Windows transparency and power policies still control the native backdrop brush.

## Validation

The new glass_rendered_pixels test uses its own colored checkerboard window.
It checks that real rendered blur removes fine detail, retains background colors,
updates when the background changes, preserves clear gaps and the center hole,
disappears when switched off, and does not steal focus. It also checks root-button
alpha and opaque icon pixels. This supplements the native lifecycle, core,
stylus, editor, and full application smoke checks.

## Source and rebuild

The installed folder contains Tablet-Pro-Radial-0.3.2-Source.zip. Qt 6.11.1 and
MinGW 13.1 build the app. A small C ABI DLL, TabletProGlass.dll, uses the Windows
SDK C++/WinRT headers and is compiled with Visual Studio's x64 C++ tools.
The DLL's C++ runtime is linked statically; no Qt or CRT objects cross its ABI.

CMake builds the bridge automatically on Windows, including MinGW builds, when
Visual Studio C++ tools and a recent Windows SDK are installed. For a standalone
bridge build, run scripts/build-glass-composition.ps1. Keep TabletProGlass.dll
beside TabletProHIDx64.dll under the deployed app's lib directory.

Deploy Qt with windeployqt, retain the existing app-local HID runtime and license
notices, then use installer/build-glass-fix.py --payload PAYLOAD. The installer
supports --verify NEW_EMPTY_FOLDER without installing or changing user settings.
Uninstall preserves user settings, saved radial profiles, and changed/added files.

Windows API reference:
https://learn.microsoft.com/en-us/uwp/api/windows.ui.composition.compositor.createhostbackdropbrush
https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute
