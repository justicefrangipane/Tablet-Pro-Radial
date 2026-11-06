# Current release: 0.3.10

What's new opens automatically on the first normal launch of each version, after radial/docked/editor startup. It shows the running version's bundled notes and offers Let's go or View update log. A persisted per-version marker prevents repeats on restart or reinstall; opening an older build does not reset a newer build's marker. Smoke runs and the pen helper do not show or mark the welcome. Missing notes do not display another version's content or consume its marker.

The offline Update log is available from the system tray, editor footer, and Updates window. A version picker marks the installed release and shows New, Improved, and Fixed changes for the selected version. The bundled history starts at 0.3.4. The release build requires a matching history entry, and the publisher generates GitHub notes from that entry by default. Tray/update checks cover the history, selection, installed marker, narrow layout, reopening, first launch, upgrade, restart, older-version return, and welcome-to-history navigation.

The approved ivory fan-and-ring artwork is embedded as a multi-resolution Windows icon in the executable and installer. The application window, taskbar, system tray, and Start menu shortcuts use the same artwork. The tray retains its update-available badge. The icon source and conversion script are included with the application source.

Gentle bloom now opens settings in both the floating radial and the docked Continuous rail. A 760 ms bronze fill spreads from the ellipsis and reveals the ivory settings inside the existing geometry. Back and hide cancel the animation; Reduced motion and software rendering switch instantly. Drawing actions are disabled throughout the fill, and settings enable only after it finishes. Hardware-rendered pixel checks cover both layouts, growing fill coverage, completion, unchanged bounds, cancellation, repeated toggles, and Reduced motion changes.

The floating center opens bronze-and-ivory settings in the existing outer ring. Edit mode selects a button for its inspector; right-click also opens direct editing. The default main menu includes labeled Alt, Ctrl, and Shift with hold, drag-until-lift, and double-tap latch gestures. Customized profiles are preserved; exact old defaults migrate automatically, with an undoable default-menu action for other profiles.

Validation adds modifier lifecycle, save/load, default migration, direct editing, actual Qt double-click events, and native Windows key injection into a separate test process. Physical stylus behavior and a second-computer install remain unverified.

The GitHub distribution and update feed use `justicefrangipane/Tablet-Pro-Radial`. See [tray and updates](TRAY-AND-UPDATES.md) and the [release guide](RELEASING.md) for the current Windows workflow. The notes below are the historical 0.3.0 implementation audit, not current release acceptance results.

# Implementation and validation status — v0.3.0

## Delivered

Quiet Gravity is implemented in the native renderer, with distinct source breakup, light attraction, transit, reassembly, and a small damped seating movement. The default dock transition is 1640 ms. Six annular child wedges reveal over 330 ms with a restrained staggered spring, then dissolve into their actual glass/icon texture fragments over 280 ms. Each wedge spans 24 degrees with six-degree gaps, inner/outer radii of 88/136 px, and icon centers at 112 px. Final hit geometry is immediately available; releases in gaps cannot commit. Motion never changes the core's slot meaning. Reduced motion seats the elements immediately.

The implementation includes the C++ model/gesture core, native editor, new JSON profile format, atomic Apply, fixed-slot editing, draft history, preview/runtime separation, Windows adapter, sample profile, and generated optional audio cues. Normal launch opens the runtime menu; `--editor` opens the editor and `--docked` starts at the left edge.

Floating invocation now anchors near the pen/cursor on its screen, with room for a first-level fan. The runtime window spans that screen's available geometry, and the dock stays on its left edge. A region mask follows the menu, toolbar, and children, with the full window used only during docking motion.

The supplied HID DLL is integrated through an isolated helper process with explicit start/stop, validated messages, proximity baselines, selected-side-button rising edges, and parent-lifetime cleanup. Pen settings and status are exposed in the editor. Ctrl+Alt+Space remains available for invocation and for assignment in a pen driver's shortcut settings.

The center hub holds Dock/Return to radial, Edit, and overflow controls. Runtime results/errors are visible. A bounded asynchronous release gate retains target identity until execution; hide, cancel, timeout, and target changes prevent stale dispatch. Test radial documents stage their menu and four appearance/audio preferences until Apply. Optional documented Windows 11 Desktop Acrylic has a translucent fallback and is disabled during animation masks.

## Verified in this environment

- Release build with GCC 13.3, CMake, and Qt 6.8.3 on Linux.
- All four Linux CTest suites pass: `portable_core`, `editor_and_format`, `platform_simulation`, and `stylus_input`.
- Portable core tests: model validation, fixed geometry, ownership, hysteresis, neutral space, nested/backtracking paths, coalesced movement, one-shot release, and cancellation.
- Editor/format integration tests: round trips and invalid input rejection, history, fixed empty slots, Apply and reload, active-session snapshots, final release movement, and clipped-child cancellation.
- The core suite passes 693 invariants. The application suite passes 171 checks, including test-document save/load, atomic writes, staged settings, invalid input, invocation anchors, fan gutters, small viewports, queued actions, and cancellation.
- Platform simulation tests: supported key/modifier validation, one-shot target use, Unicode limits, cancellation, and Qt overlay flags.
- Forty-one stylus checks cover packet decoding, button edges and baselines, selected-button changes, settings, and the supported non-Windows lifecycle behavior. They do not execute the Windows HID DLL.
- Actual Qt Quick smoke runs at 1180×760 and 900×640 using the software renderer: floating menu, voxel transit, completed dock, six Text children, exactly one queued Ctrl+B preview report, center controls, changing dock icon, overflow controls, simulated pen-button toggles, and the last docked group's fan fitting inside the preview.
- Native screenshots in `docs/images/` come from the running application, not mockups.
- The Windows x64 PE executable and its Windows-specific sources were cross-compiled using GCC 13 POSIX and Qt 6.8.3. The distributed cross-build includes its runtime dependencies, including the Microsoft runtime dependencies required by the supplied HID DLL. The accompanying build audit records the package details. This executable has not been run on Windows here.

## Remaining release work

Windows compilation is complete; Windows runtime acceptance remains pending. Verify HID helper startup and shutdown, pen-button delivery, non-activating focus, global shortcut registration, input dispatch/cleanup, multiple displays, DPI, and clean-machine deployment using `WINDOWS-VALIDATION.md`. The separate MSVC PowerShell source-build script has not been executed on Windows. Its local-package route requires the Visual C++ x64 runtime; this is distinct from the distributed cross-build's bundled dependencies.

The GPU scene-graph path compiles; the screenshots exercise its shared geometry through the software fallback. Actual GPU frame time, device loss, and long-running performance need hardware measurements. No audio output device was available, so click/crackle playback needs listening and tuning on the target hardware.

The current release is left-edge only. A small viewport-bound fan-origin adjustment is fixed when a branch opens; its positions and angles never follow the pointer. Branches that still cannot fit are canceled. Accessibility beyond reduced motion, tooltips, and ordinary editor controls still needs a dedicated pass. Runtime selection still depends on pointer input. Offscreen software smoke tests validate layout and rendering, but that platform does not support native window masks and cannot prove Windows click-through behavior.

In the historical 0.3.0 build, held and sticky actions were unimplemented. Version 0.3.6 adds Alt/Ctrl/Shift holds and latches; next-stroke and repeating actions remain deferred. The Windows SendInput partial-insertion test shim also remains to be written. Future work includes legacy import, deeper platform integration, and macOS/iOS hosts. Vendor-specific direct Wacom capture remains deferred; the driver-configured Ctrl+Alt+Space route is provided. Unfolded Bible animation source was not supplied, so this build contains no reused palette code.
