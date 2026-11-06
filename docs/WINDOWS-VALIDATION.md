# Windows build and acceptance — v0.3.0

The distributed Windows x64 application has been cross-compiled as a Windows PE
executable with GCC 13 POSIX and Qt 6.8.3. Its package includes the Qt/compiler
dependencies and the Microsoft runtime dependencies needed by the supplied HID
DLL. See the accompanying build audit for the package inventory and checks.
It has not been executed on Windows here. User launch instructions are in
`RUN-WINDOWS.md`; the source-build route below is separate from that package.

## Build a local package

Install Visual Studio 2022 with the Desktop development with C++ workload,
MSVC v143 x64 tools and a Windows SDK; CMake 3.21 or newer; and a Qt 6 MSVC x64
kit with Qt Quick, Quick Controls, and Multimedia. The intended kit is
`C:\Qt\6.8.3\msvc2022_64`. Use an ordinary PowerShell session with CMake on PATH.

From the project folder:

```powershell
.\scripts\build-windows.ps1 -QtRoot 'C:\Qt\6.8.3\msvc2022_64'
```

The script selects Visual Studio 2022 x64, builds Release, runs CTest, then copies
the application and deploys Qt/QML dependencies into a unique folder and ZIP
under `dist`. It checks every native command's exit code and does not package a
failed build/test run. Build files stay in `build\windows-vs2022-x64`. Existing
packages are retained. The script does not install software or request elevation.
For this MSVC source-build route, the destination PC needs the Visual C++ x64
runtime for Visual Studio 2022; compiler-runtime installation is deliberately
not part of the script's packaging. The script itself has not been executed on
Windows here. This requirement does not describe the dependency contents of the
separately distributed cross-build.

## Verified here

The code has been compiled and exercised on Linux using Qt 6.8.3 and GCC 13.3;
all four Linux CTest suites pass. The core suite covers 693 invariants, and 171
editor/application checks cover profile and test-document round trips, staged
settings, anchored invocation, fan geometry, and action lifecycle. Forty-one stylus checks cover packet
decoding, edge recognition, baselines, settings, and the supported non-Windows
lifecycle path.

Platform simulation checks passed for key allowlists, duplicate modifiers,
one-shot target consumption, cancellation, Unicode preservation and bounds,
malformed surrogate rejection, and the overlay's Qt flags. Simulation does not
send input to other applications.

The Windows adapter and isolated HID helper compile in the Windows cross-build,
but have **not** been executed on Windows. The Windows CMake configuration
registers `portable_core`, `editor_and_format`, and `stylus_input`; it does not
provide a Windows native-input test shim. The Linux-only `platform_simulation`
suite does not exercise SendInput. Passing portable tests or compiling a PE
binary is not evidence that pen input, window focus, helper cleanup, clean-PC
deployment, or mixed-DPI behavior works. The offscreen software platform also
does not support native window masks, so its screenshots cannot validate
click-through behavior.

## Hardware acceptance

Record Windows/Qt versions, GPU, pen/tablet model, display scale factors, and the
target application's version alongside each result.

- [ ] **Clean launch:** Run the single EXE installer on a test PC without the Qt SDK on PATH.
  Normal launch opens the floating menu; `--docked` starts at the left edge and
  `--editor` opens the editor. Icons and previews render, sound can be enabled,
  and no missing DLL, plugin, or QML import message appears. Test the supplied
  cross-build and any locally produced MSVC package as separate artifacts.
- [ ] **HID invocation:** Enable pen input, hover in range, then press the
  selected side button. Each down transition toggles the menu once; holding it
  does not repeat. Entering range with the button held, changing button selection
  while held, or enabling input while held must not cause a false invocation.
  Test both button selections and confirm the status reports startup failures.
  Vendor-specific direct Wacom capture is deferred; separately test assigning
  Ctrl+Alt+Space in the pen driver and disabling native input to avoid duplicate
  invocation routes.
- [ ] **Helper lifetime:** Turn pen input off/on, use its restart control, quit
  normally, and end the parent process. The isolated helper ends with its parent
  and does not leave a listener running. Test missing DLL/runtime dependencies
  and failed initialization: status is explicit and no menu action fires.
- [ ] **Focus:** Focus a disposable drawing document. Ctrl+Alt+Space opens the
  overlay without moving keyboard focus. Mouse, touch, pen contact, and hovering
  over it preserve the drawing target. The separate editor accepts normal text.
- [ ] **Native pen:** Open a six-child group by dragging outward, reverse through
  its cancel zone, and release on a leaf. Verify one action per gesture, no
  accidental mouse duplicate, no action from a second contact, and cancellation
  on lost capture, lifted/cancelled pointer, or interrupted gesture.
- [ ] **Mixed DPI:** Repeat floating, docking, and nested selections at 100%,
  150%, and 200% scaling, including two monitors with different scales and a
  monitor left of the primary display. Hit areas match visible buttons; the rail
  sits at the left edge of the chosen display's available area. Floating
  invocation follows the pen/cursor with its edge margin; docking and undocking
  preserve that source anchor. The input mask follows the menu, toolbar, and
  children, and the transition's temporary full-window mask does not leave an
  invisible region blocking the drawing application afterward.
- [ ] **Target loss:** Change foreground apps, open the editor, close the target,
  or lock/unlock during a gesture. It cancels without committing to the new
  target. Each new gesture captures its target again.
- [ ] **Shortcuts and text:** In a disposable document check plain B, Ctrl+Z,
  Ctrl+Shift+Z, arrows, Delete, F keys, non-Latin text, emoji, and newlines. Check
  malformed actions fail explicitly. OEM punctuation shortcuts remain rejected
  until logical-character versus physical-key semantics are defined.
- [ ] **Physical-key conflicts:** Hold either side of Ctrl, Shift, Alt, or Windows, the
  shortcut's main key, or a mouse/pen button. Dispatch waits at most 700 ms for
  physical input to clear, then sends once after a stable release or reports a
  timeout. It never releases those physical keys. Confirm that hiding the menu
  or changing targets while waiting cancels the action.
  Menu actions that deliberately hold modifiers for drawing, arm the next pen
  stroke, or toggle sticky keys are not implemented in this version.
- [ ] **Partial insertion:** In a Windows API test shim, force every SendInput
  return count from 0 to the requested count for shortcuts and Unicode text.
  Cleanup emits only ups matching this adapter's inserted, unmatched downs.
  Force cleanup failure: show an error and block further dispatch. This shim
  still needs to be written; normal CTest does not cover this case.
- [ ] **Blocked input:** A target with higher integrity may reject input; the
  result is an explicit failure, with no automatic elevation or focus stealing.
- [ ] **Lifecycle:** A conflicting Ctrl+Alt+Space registration is reported;
  holding the hotkey does not repeatedly toggle. Exiting frees the hotkey. No
  synthetic modifier remains down after a completed action.
- [ ] **Center controls and fan:** Dock, Edit, and overflow stay inside the radial
  center. Dock changes to Return to radial in docked mode. All six child wedges
  fit without overlap; releasing in an angular gap commits nothing. Check the
  reveal and voxel dissolve in both modes, including rapid reopen.
- [ ] **Test radial file:** Save a changed menu and sound/motion/glass settings
  using Save test radial, modify them, and reopen the saved file. Apply draft
  restores both menu and settings. Ctrl+S updates the same file. Canceling a
  dialog or opening malformed JSON leaves the current menu intact.
- [ ] **Glass:** On a supported Windows 11 PC, toggle Glass background and inspect
  backdrop blur in radial and docked modes. During docking, no full-screen
  backdrop or residual input mask remains. Unsupported systems retain the
  translucent fallback.

The current adapter samples foreground identity every 30 ms and checks again
immediately before SendInput. Windows provides no atomic foreground-check plus
global-input insertion operation; very brief focus changes and the final race
window remain limits to assess in hardware testing.

Implementation details and current token support are in the source checkout's
`tests/platform_notes.md`.

## Build references

- [Qt Windows deployment and windeployqt](https://doc.qt.io/qt-6/windows-deployment.html)
- [CMake Visual Studio 2022 generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2017%202022.html)
- [CTest](https://cmake.org/cmake/help/latest/manual/ctest.1.html)
