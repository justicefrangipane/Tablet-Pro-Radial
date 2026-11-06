# Tablet Pro Radial 0.3.1 — Glass background fix

Close the previous radial app, run Tablet-Pro-Radial-0.3.1-Glass-Fix-Setup.exe,
and choose to open the new version with Glass enabled when setup finishes.
Use the new “Tablet Pro Radial 0.3.1” desktop shortcut afterward.

The installer uses a separate per-user 0.3.1 folder. Saved menus and preferences
stay in their existing locations. Older versions remain installed. To change
Glass later, open the center ellipsis menu and toggle Glass background.

## Change

The Windows compositor material is now hosted by a small native window behind
the radial, with an explicit native region matching the controls. The full
monitor Qt overlay never receives a system backdrop. Holes and gaps remain
clear. Moving/animating the controls removes the material until a stable shape
is available. Turning Glass off, hiding, or destroying the overlay removes the
native surface. Windows 10 and unsupported renderers retain translucency.
Windows accessibility/power policies can still change the material appearance.

## Validation

Built locally on Windows x64 with Qt 6.11.1 and MinGW 13.1.
The portable core, editor/format, stylus input, and backdrop lifecycle suites
pass. The native backdrop suite checks 20 conditions, including native clipping
of ring holes and disconnected regions, focus preservation, movement, resizing,
and hide/destroy/reopen behavior.
The full QML smoke check also passes: editor rendering, docking, floating,
submenu expansion, dissolve, overflow controls, and pen setup. The floating
overlay PNG has a fully transparent outside corner (RGBA 0,0,0,0).

Windows tests use isolated temporary profile/settings directories. Smoke mode
uses simulated commands and cannot type into another application. Its docking
check waits for the animation to finish instead of assuming a fixed delay.
Actual pen hardware and appearance under every Windows transparency policy have
not been validated by the automated tests.

## Files and rebuild

The installed folder contains Tablet-Pro-Radial-0.3.1-Source.zip with the full
modified source. Qt libraries remain separate and replaceable. Attribution is
under licenses. The original NSIS builder is retained for reference; this
installer uses the small .NET Framework bootstrap in installer/GlassFixSetup.cs.

Build with CMake/Ninja and a matching Qt/MinGW kit, then run ctest. Deploy with
windeployqt --release --compiler-runtime --no-translations --qmldir qml --dir
PAYLOAD PAYLOAD/tabletpro_radial.exe, include the supplied HID DLL and its Visual
C++ runtime under lib, and run installer/build-glass-fix.py --payload PAYLOAD.

The setup supports --verify NEW_EMPTY_FOLDER for extraction/checksum testing
without installing, creating shortcuts, changing settings, or launching the app.
Uninstall is available in Windows Installed apps. It removes packaged files
only when unchanged, and preserves saved radial files and added files.
