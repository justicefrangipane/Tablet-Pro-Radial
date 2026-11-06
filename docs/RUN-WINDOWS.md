# Open the new radial menu

This is the Windows x64 development build of Tablet Pro Radial 0.3.4. It contains Qt and the runtime dependencies used by the supplied pen-input library; no Qt installation or administrator setup is required.

1. Close the original Tablet Pro Studio and the 0.2 radial while testing 0.3. The installer leaves 0.2 available.
2. Download the latest installer from [GitHub Releases](https://github.com/justicefrangipane/Tablet-Pro-Radial/releases/latest), then run `Tablet-Pro-Radial-VERSION-Windows-x64-Setup.exe` and follow the installer. Use its Start menu shortcuts to open the floating menu, docked rail, or editor. Use one copy at a time; quit the current copy before opening a different mode.
3. Hover with the pen, then press its first side button. A press hides/shows the menu; holding does not repeatedly toggle it. The menu remembers its floating/docked state while running.
4. Select an action by pressing its icon and releasing. For a group such as Text, press and drag outward to reveal six children, then release over one. Return through the parent to backtrack.
5. The wheel center contains **Send to dock**, **Edit**, and **•••**. In docked mode these controls sit above the rail, and the first icon changes to **Send to radial**. The overflow menu contains **Sound**, **Glass background**, **Hide**, and **Quit**. Edit opens the profile editor; **Pen button** opens input setup.

For future versions, open **Updates** from the tray or editor. The official repository is already configured. Choose **Check now**, **Download update**, then **Install update**.

The installer installs only for your Windows account. Its uninstaller removes the application files and shortcuts, while preserving saved profiles and preferences.

## Choose the input route

**Tablet Pro pen input:** Enabled by default. This uses the HID library supplied with the original Studio files. In Pen button settings, select First or Second side button. After enabling or reconnecting, move into hover with the side button released, then press. A ready status means registration succeeded; it does not prove the particular pen is sending supported events.

**Driver shortcut:** For Wacom, Xencelabs, or another driver with configurable side buttons, assign `Ctrl+Alt+Space` to the desired button in that driver. Turn off **Use Tablet Pro pen input** so one press is not handled by both routes. This shortcut invokes the same floating/docked menu. Release the shortcut modifiers before committing a menu action.

If the menu does not respond, first try `Ctrl+Alt+Space` on the keyboard. If that works, the application invocation path is working and the pen binding is the remaining step. Open Pen button settings to inspect status or select Reconnect pen. If the shortcut is already in use, the setup panel reports the registration error.

## Editing and launch options

The editor shortcut (or `tabletpro_radial.exe --editor`) opens the editor directly. Edit the draft, test it in the preview, then select **Apply draft** to publish it to the runtime. **Save test radial…** writes a reusable `Test.radial.json`; **Save** (or Ctrl+S) updates the current test file. It includes layout, nested actions, sound, timing, reduced motion, and glass. Opening a test file stages its menu and settings until Apply. The starter shortcuts are examples; set them for your drawing application.

Command-line modes are `--radial` (default), `--docked`, and `--editor`. Settings and the applied profile are stored separately from the original Studio application. This build does not import or modify the original profiles.

## Feedback and glass

To try the supplied sound-enabled preset, Open `examples/Test.radial.json` and select Apply draft. Saving a file and applying it to the runtime are separate operations.

Buttons wait briefly for pen-generated mouse buttons and driver shortcut keys to release before sending. If they remain held for 700 ms, the command is canceled with an explanation. Changing the target application or hiding the menu also cancels pending input.

Glass uses a Windows Composition host-backdrop brush with clipped menu geometry, translucent fills, and crisp icons. The 0.3.2 Glass fix is retained.

## Validation limits

The release build script runs the six Windows test suites, verifies the installer payload, and runs the packaged editor/runtime smoke checks without Qt on PATH. Physical pen input, focus behavior in drawing programs, mixed-DPI displays, and installation on another computer require device testing. The installer is unsigned.

The supplied Wacom bridge was not connected because it explicitly requires Studio to own keyboard focus. Direct Wacom background capture needs a different integration; use the driver shortcut route for this build.
