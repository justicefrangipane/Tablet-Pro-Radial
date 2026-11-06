# Open the new radial menu

This is the Windows x64 local candidate of Tablet Pro Radial 0.3.15. GitHub remains on 0.3.13 until the new notes are approved. The installer contains Qt and the runtime dependencies used by the supplied pen-input library; no Qt installation or administrator setup is required.

1. Close the original Tablet Pro Studio and the 0.2 radial while testing 0.3. The installer leaves 0.2 available.
2. Download the latest installer from [GitHub Releases](https://github.com/justicefrangipane/Tablet-Pro-Radial/releases/latest), then run `Tablet-Pro-Radial-VERSION-Windows-x64-Setup.exe` and follow the installer. Use its Start menu shortcuts to open the floating menu, docked rail, or editor. Use one copy at a time; quit the current copy before opening a different mode.
3. Hover with the pen, then press its first side button. A press hides/shows the menu; holding does not repeatedly toggle it. The menu remembers its floating/docked state while running.
4. Select an action by pressing its icon and releasing. Hold a parent such as Tools for **300 ms** to open its child radial at the latest pen-tip position. Slide out to open it at your finger instead, then release over a child or lift in the center and tap one. The pen can select while the launching finger remains down. Tap the center for Back; Esc closes the menu.
5. Tap the centered **•••** in the wheel for Dock, Edit, Sound, Glass, Hide, and Quit. Drag inside that circle to move the wheel. **Back** restores the tools. The docked 44 px rail uses its top **•••** control for the same settings.
6. Choose **Edit**, then tap a button to open its inspector. Choose **Make parent** to keep its current action as the first child, **Add child** for another action, or **Add parent** for a nested menu. Named child slots open their inspectors. Choose **Modifier key** to assign Alt, Ctrl, or Shift; **Apply draft** activates changes. **Pen button** in the editor opens input setup.

Parent menus work in both layouts, including existing customized parent buttons. The original wheel or rail steps aside until the child menu closes. Popups stay inside the current overlay screen. Move or hover the pen first: without a known pen location in the last 30 seconds, a hold opens near the parent button. The editor preview uses its center as the simulated pen tip. A modifier tapped in a child radial applies to the next pen stroke, even if opened from the dock.

The default main menu includes **Alt, Ctrl, and Shift**. In floating radial mode, tap to arm the next pen stroke (marked with a circled **1**); its pen-up releases the modifier. Tap again to cancel. You can combine modifiers for that stroke. In docked mode, hold for a momentary modifier, drag out to lock until lift, or double-tap to latch until tapped again. Hiding the menu, pressing its pen trigger/shortcut, editing, target changes, and exit release owned modifiers. Customized menus are preserved; **Use new default menu** loads the new layout as an undoable draft.

For future versions, open **Updates** from the tray or editor. The official repository is already configured. Choose **Check now**, **Download update**, then **Install update**.

The installer installs only for your Windows account. Its uninstaller removes the application files and shortcuts, while preserving saved profiles and preferences.

## Choose the input route

**Tablet Pro pen input:** Enabled by default. This uses the HID library supplied with the original Studio files. In Pen button settings, select First or Second side button. After enabling or reconnecting, move into hover with the side button released, then press. A ready status means registration succeeded; it does not prove the particular pen is sending supported events.

**Driver shortcut:** For Wacom, Xencelabs, or another driver with configurable side buttons, assign `Ctrl+Alt+Space` to the desired button in that driver. Turn off **Use Tablet Pro pen input** so one press is not handled by both routes. This shortcut invokes the same floating/docked menu. Release the shortcut modifiers before committing a menu action.

If the menu does not respond, first try `Ctrl+Alt+Space` on the keyboard. If that works, the application invocation path is working and the pen binding is the remaining step. Open Pen button settings to inspect status or select Reconnect pen. If the shortcut is already in use, the setup panel reports the registration error.

## Editing and launch options

Drag a button onto a parent in the menu list to move it into that parent's next empty child slot. Choose **Arrange buttons** to drag directly in the radial or docked preview. Valid parents turn green. The list scrolls when you hold the drag near its top or bottom. Drop outside buttons or press Esc to cancel; Undo restores a completed move. A moved parent retains all its children. Full parents and loops are refused. If a source parent becomes empty, add a child or remove it before applying. Choose **Done arranging** to test hold/slide gestures again, then **Apply draft** to use the layout.

The editor shortcut (or `tabletpro_radial.exe --editor`) opens the editor directly. Edit the draft, test it in the preview, then select **Apply draft** to publish it to the runtime. **Save test radial…** writes a reusable `Test.radial.json`; **Save** (or Ctrl+S) updates the current test file. It includes layout, nested actions, sound, timing, reduced motion, and glass. Opening a test file stages its menu and settings until Apply. The starter shortcuts are examples; set them for your drawing application.

Command-line modes are `--radial` (default), `--docked`, and `--editor`. Settings and the applied profile are stored separately from the original Studio application. This build does not import or modify the original profiles.

## Feedback and glass

To try the supplied sound-enabled preset, Open `examples/Test.radial.json` and select Apply draft. Saving a file and applying it to the runtime are separate operations.

Shortcut and text buttons wait briefly for pen-generated mouse buttons and driver shortcut keys to release before sending. If they remain held for 700 ms, the command is canceled with an explanation. Changing the target application or hiding the menu also cancels pending input.

Glass uses a Windows Composition host-backdrop brush with translucent fills and crisp icons. A soft fade extends 25% of the body's width beyond its outline (11 logical pixels for the dock, 50.5 for the radial). This decoration passes input through; the clickable body stays the same. Display layout, scaling, and compositor changes rebuild the graphics and masks after a short settling delay. A hidden menu stays hidden.

## Validation limits

The release build script runs the eight Windows test suites, verifies the installer payload, and runs the packaged editor/runtime smoke checks without Qt on PATH. Native modifier injection is tested against a disposable receiver. Physical pen input, focus behavior in drawing programs, mixed-DPI displays, and installation on another computer require device testing. The installer is unsigned.

The supplied Wacom bridge was not connected because it explicitly requires Studio to own keyboard focus. Direct Wacom background capture needs a different integration; use the driver shortcut route for this build.
