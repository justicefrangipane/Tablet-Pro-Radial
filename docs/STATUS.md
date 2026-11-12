# Local candidate: 0.3.25

The floating center expands by 8% over 90 ms while a pen, finger or mouse is held down. Release starts the 420 ms settings ripple and bronze fill while the center relaxes over 180 ms. A long hold does not open settings early; dragging cancels the press feedback and retains movement-only behavior. Docked settings now fill in 360 ms. Closing still reverses the current animation, and Reduced motion skips movement.

Native Glass now has its own visible region, independent of the larger input region used for pointer capture. It stays enabled through settings, docking/undocking, child transitions and center drags. Docking coverage follows the rendered fragments; the soft blur contour uses a bounded sampling surface while live blur and menu artwork stay at native density. Translated contours are cached, including at fractional display scales. The hub and wedge regions read the same center to avoid intermediate rebuilds while QML updates its axes separately. Display recovery and the user's Glass preference remain authoritative.

All ten Windows suites pass (55.65 seconds), including 114 native settings/display checks and 91 rendered Glass checks. Synthetic pen events verify expansion over a long hold, release-triggered opening and drag cancellation. Pixel tests verify actual background blur during motion and unchanged pixels outside the menu. Motion Glass updates measured 11 ms at the 95th percentile and 43 ms maximum on this machine. The complete-app check passes with live Glass, unchanged shape/texture build counts during dragging, stable ripple coverage, and both dock transition directions. Packaged startup and installation receipts remain outside the source archive; physical pen feel remains a hands-on acceptance check.

Not published. Exact 0.3.25 notes require Justice or Bassem approval.

# Previous local candidate: 0.3.24

The floating center menu uses the selected Press and release ripple: a 960 ms reversible timeline expands the center, holds briefly, then moves the outer wedges by up to 6 logical pixels as bronze fills outward. Center input stays fixed, icon textures remain cached, and one temporary native mask contains the movement. Docked settings retain their contained 760 ms liquid fill; Reduced motion skips the ripple.

Docking now shares one layout between the model, animation guide and QML housing. The obsolete wider guide is replaced by the actual inset rail border and rounded caps, including when root slots are empty. The housing and header fade in around the landed icons, with input disabled until the transition completes. Undocking follows the same path in reverse. Landing glows fade out at completion.

All ten Windows suites pass (57.99 seconds). Tests cover rendered guide placement at three viewport sizes and 100%, 150% and 200% density, sparse menus, identical endpoint icon pixels, native frame registration, ripple timing/reversal, stable hit targets and texture reuse. Native checks also verify the housing handoff in reverse and display recovery. Packaged startup and installation receipts remain outside the source archive; physical pen feel remains a hands-on acceptance check.

Not published. Exact 0.3.24 notes require Justice or Bassem approval.

# Previous local candidate: 0.3.23

The sprite atlas now rasterizes vector paths and glyphs at the destination display's effective device pixel ratio. Texture coordinates and animation geometry remain logical, including fractional scaling, dock particles and retiring child fragments. Display changes request a refresh; ordinary position changes retain the cached artwork. The software fallback uses its paint device's pixel ratio. Radial captions and the child-center title increase from 9 to 10 logical pixels without enlarging the wheel.

All ten Windows suites pass (58.60 seconds). Density checks compare icon and caption edge detail with enlarged 1x artwork at 125%, 150%, 200% and 300%, check identical cached frames and logical bounds, and return to 100%. A separate native Windows run uses installed fonts in addition to offscreen coverage. Packaged startup results are recorded outside the source archive.

Not published. Exact 0.3.23 notes require Justice or Bassem approval.

# Previous local candidate: 0.3.22

Floating parent taps and outward pulls replace the current wheel with a persistent child ring at the same center. Opening-contact release cannot select a child beneath the original parent. Nested parents retain the center and Back stack. Center dragging moves both the root anchor and open child ring. Dock fans and held-at-pen parent ownership retain their existing behavior.

Position-only changes now update cached sprite/particle geometry instead of rebuilding the icon atlas and GPU texture. During a drag the native overlay uses one stable capture region with Glass disabled; the body mask and Glass return on release or cancellation. Center gestures use active translation, avoiding movement-relative pointer calculations as the hub follows the input.

All nine Windows suites pass (53.76 seconds), including mouse, pen and touch center drags, long sample sequences, persistent tap/slide parents, nested Back, child actions, and 80 rendered movement frames without an atlas rebuild. Native packaged smoke also exercises the motion mask, restored Glass, texture reuse and in-place child bounds. Physical pen drag feel remains a hands-on acceptance check.

Not published. Exact 0.3.22 notes require Justice or Bassem approval.

# Previous local candidate: 0.3.21

In-app Install launches the checksum-verified installer with the owning app PID and original layout, then follows the normal quit path to release modifiers and stop pen helpers. Setup waits for exit, skips the redundant install/open prompts, displays progress, and reopens the installed app in the requested mode. The older two-argument handoff also skips prompts and reopens radial. Manual setup retains its choices; verification never installs or reopens.

Dirty drafts retain Apply/Discard/Keep editing. Dismissing the prompt cancels the pending install so it can be retried. The layout is captured before the prompt opens the editor. Failed or tampered installer launches leave the app open; repeat Install clicks cannot launch duplicates.

All nine Windows suites pass (54.35 seconds). Install-button tests download a checksum-verified executable fixture through the updater, click the real Updates dialog, and inspect a detached process receipt for PID/layout. They cover radial, docked, editor, protected drafts, cancellation/retry, duplicate clicks and failed launch. Installer workflow tests cover legacy handoff, verification-only behavior, manual cancellation, failed install, and waiting for a real process to exit. These tests do not install over the user's running copy.

Not published. Exact 0.3.21 notes require Justice or Bassem approval.

# Previous local candidate: 0.3.20

Repeated dock/radial changes retain the existing Quiet Gravity animation. Configuring a stopped QVariantAnimation used to recalculate its value at the previous transition's elapsed time; that overwrote the visible progress and could reduce the next duration to one millisecond. Configuration now blocks those intermediate value signals, resets elapsed time, and calculates the duration from a captured visible start. Reversals retain their current position. Reduced motion is unchanged.

All eight Windows suites pass (48.38 seconds). Regression coverage checks four alternating transitions, intermediate progress in both directions, interruption/reversal continuity, and the Reduced motion endpoint. Packaged runtime smoke checks capture animated frames in both directions through the actual side-button handler and full native motion mask. Physical pen acceptance remains a hands-on check.

Not published. Exact 0.3.20 notes still require Justice or Bassem approval.

# Previous local candidate: 0.3.19

Direct pen input classifies the selected barrel/invert button: a short press toggles visibility on release, while a 500 ms hold switches docked/floating mode once and leaves the overlay visible. Out-of-range, owner-listener failure, changing the selected button, stopping input and disabling input cancel a pending gesture. A hold does not emit a short-press action on release. Mode changes may reverse an animation already in progress.

Held parent menus retain the original launching contact through nested navigation and radial-to-fan transitions. Releasing it cancels uncommitted children, including a pen contact still over a child; a completed selection keeps its queued action or modifier. Both the dock's QML opacity and native window mask retain the rail beside a child radial. Editor preview follows the same visibility rule.

All eight Windows test suites pass. Helper-process checks cover short/long barrel and invert presses, duplicate channels, range exit, listener failure, disabling, selecting another button and stopping. Qt touch/pen tests cover the visible dock, parent release during a child pen contact, completed selection, nested fan selection and mode-switch animation reversal. Packaged startup also checks the native window mask includes both rail and child radial. Physical Cintiq/Surface acceptance remains a hands-on check.

Not published. Exact 0.3.19 notes still require Justice or Bassem approval.

# Current release: 0.3.18

The editor exposes a Pen trigger row below its toolbar: an enable switch, an explicitly named Barrel button (default) / Invert or second button choice, the existing keyboard shortcut, and Pen settings for diagnostics. Selection and enable state use the existing persisted preferences and take effect immediately; saved custom selections are retained across restarts. The fresh-install defaults remain enabled with barrel selected. The pen setup dialog uses the same unambiguous labels.

Keyboard-driven UI checks cover changing the trigger, persistence across controller restart, disabling/re-enabling, and fitting the row inside the 900-by-640 minimum editor window.

Dragging out of a modifier now arms a separate hover lifetime as soon as the drag threshold is crossed. Selecting-contact lift and ordinary pen tip-up preserve it; a DLL in-range-to-out-of-range transition, a native active-pen proximity exit, or Qt proximity leave releases it. A hover exit before selecting-contact lift cannot rearm the key. One-stroke tap behavior and docked double-tap latching remain distinct, and a stroke completion preserves unrelated hover locks.

Successful action selection and modifier selection dismiss the floating overlay after gesture cleanup. A narrowly scoped selection-dismissal flag preserves held modifiers through both QML and native window-hide handlers; explicit Hide and other cancellation paths still release them. Delayed pointer cancellation from a still-held parent-launch finger cannot cancel a completed selection. The dock stays visible and child menus close back to it. Tests exercise these paths through QML and real Windows key state; physical interaction acceptance remains a user-device check.

Published November 9, 2026 after Justice approved the exact release notes (SHA-256 `3f4d7dbae4377be9760eae335d0d2482c3d49c8ca3ef46074a489f1de3d306e7`). Release tag `v0.3.18` targets commit `7a2b43857d880c207f113041ea3c506cde32b815`; all three uploaded assets match their local SHA-256 digests. GitHub's Portable core check passed. The real application updater discovers 0.3.18 from 0.3.15, downloads and verifies the installer, and reports 0.3.18 as up to date. Future updates still require approval of their exact notes.

# Previous local candidate: 0.3.17

The existing TabletProHIDx64.dll is retained (SHA-256 `76764131dd9c8fcb51ec2c2d9cc87b032157e4f46015dfa1dd610751f0020eac`, identical to Studio's current `lib` copy). Two isolated listeners request continuous packets using `(sink, 0, 1, mode)`: integrated mode 0 and external/generic mode 1. All sixteen signed fields survive IPC; analog updates are coalesced, but every contact, proximity and button transition is sent immediately. Each listener has independent validation, state and failure handling.

DLL tip transitions now reach the one-stroke modifier observer. A second observed input channel may complete a stroke when the first channel misses the release; an unmatched release and the menu-selection lift cannot complete a stroke. Native Windows tests cover real held-key release through the DLL signal boundary. Hide, trigger and existing cancellation behavior remain, and listener loss or tracked-device removal releases owned modifiers.

The persistent Windows observer registers both pen HID usages, retains per-device tip state, and maps report coordinates through that device's display mapping and Qt monitor scaling. Mouse and touch cannot replace the pen position. The main radial now uses the tracked pen location as well as held parent menus. Raw out-of-range clears the active pen location. This does not move an already-open menu beneath a drawing stroke or relocate a parent popup across overlay screens.

Physical Cintiq Pro 17 evidence: the initial background check recorded 11 DLL tip-downs and 11 tip-ups, matching 11 native HID down/up pairs, with hover positions and full packets. A subsequent visible monitor also received packets. This verifies event reception, not end-to-end acceptance in every drawing application. Surface Pro 11 physical testing, MPP side-button behavior, mixed-monitor/rotated display accuracy, and driver-specific tilt units remain unverified. The legacy DLL does not identify a device in its packets or report capability flags; zero data is not a capability claim. Native position/contact tracking retains device identity separately. No Wacom driver settings or focus-harvesting workaround are installed.

`--pen-diagnostics <report.json>` opens an observation-only window with both listener counters and pen position, writing a bounded local JSON report. It does not start Radial's shortcuts or modifier injection. Pen setup also includes live listener counts. The supplied `lib for chatgpt.zip` was 22 bytes with zero entries; the adjacent current Studio DLL matched the shipped binary. The adjacent older `lib.zip` contains a different 2021 DLL, which was not substituted for the 2025 binary.

This candidate is not published. Exact release notes require Justice or Bassem's approval before uploads or update-feed changes.

# Previous local candidate: 0.3.16

Sliding outward uses the original fan renderer and gesture engine in both layouts. Hold for 300 ms retains the full circular menu at the latest pen tip. Nested fan parents can hold to open a radial, and parents inside a held radial can slide into a fan while retaining the same pointer until release. Fan selection keeps directional opening gates, neutral cancellation, nested backtracking, viewport fitting, and coalesced-input handling.

This candidate is not published. Exact release notes require Justice or Bassem's approval before uploads or update-feed changes.

# Published release: 0.3.15

Editor buttons can be dragged from the outline or named child controls onto an existing parent in the outline or preview. Arrange buttons enables direct dragging in either the radial or docked preview; Done arranging restores gesture simulation. Drop feedback shows the target and validation result, and edge scrolling keeps lower list parents reachable. Mouse and touch drags, Escape cancellation, outside drops, edge scrolling, and single-step Undo are exercised through the actual QML.

Reparenting preserves node/action IDs and entire descendant trees, empties only the source slot, and fills the first free destination slot. It does not update the applied runtime until Apply. Validation rejects self/descendant loops, full parents, invalid IDs, ordinary-action destinations, and excessive subtree depth. An emptied source parent remains editable, with guidance before Apply. Save/reload, subtree preservation, Undo/Redo, and rejected moves have controller coverage.

This release includes the 0.3.14 parent-menu work below. Justice approved the prepared 0.3.15 notes and publication on November 6, 2026. All eight Windows suites, all 1,587 installer files, packaged startup checks, and GitHub's portable-core checks passed. The published installer, source archive, checksums, and exact notes were verified. The real updater downloaded and validated 0.3.15 from 0.3.13 and reported 0.3.15 as up to date. The earlier 0.3.14 local candidate was not separately published.

# Parent-menu implementation: 0.3.14

Parents open full circular child menus after a 300 ms hold at the latest pen position, or after sliding 42 logical pixels at the contact position. Existing groups need no profile migration. Nested parents support tap, hold, slide and center Back. Original artwork steps aside while the popup is open; its Glass uses the radial diffusion width. All menu geometry is clamped to the current overlay screen. A missing or expired pen position falls back to the launch contact; cross-monitor popup relocation is not implemented.

The Windows pen-position observer filters pen-promoted mouse messages and maps their native monitor handle to Qt coordinates. Ordinary mouse and touch do not replace the pen location. The popup uses independent pen, touch and mouse handlers, allowing the pen to choose while a launching touch is still held. Cancel, resize, target changes and hide clear pending holds and menus. Child modifier taps arm one stroke even when launched from the dock.

The editor offers Add parent, Add child, Make parent, named child slots, and Back to parent. Converting an action preserves it as the first child. Insertions fill empty slots, occupied slots select existing children, and Undo/Redo and save/load retain the tree. Four parent levels remain the limit.

Verification covers hold timing, slide location, cancellation, independent pen/touch input through the actual QML, nested navigation, child modifiers, native Windows pen-coordinate mapping at the connected display scale, and editor persistence. Physical simultaneous pen/finger behavior in drawing applications and mixed-monitor devices still require acceptance testing. Future update notes require Justice or Bassem's approval before uploads or publication.

# Published release: 0.3.13

Docked settings highlights use the control's background insets: 3 logical pixels on either side and 2 above/below each action. This keeps active, hover and pressed fills inside the continuous rail border while preserving the 44-by-44 pointer targets and active-state dots.

The center circle is a circular mouse/touch/pen drag target with a centered ellipsis and no rectangular hover background. Tap opens settings; a drag does not. Tooltips are separate topmost, input-transparent windows with screen bounds and position updates. Dock artwork is inset inside the rounded native mask.

Floating modifiers arm the next external pen stroke, ignore the selection lift, and release on its pen-up. A second tap cancels. Docked hold/contact-lock/double-tap latch remains. Hide, trigger, target loss, mode changes, recovery and exit release owned keys. The observer uses raw pen Tip Switch reports with a pen-promoted mouse fallback; it does not redirect input or install a keyboard hook.

Glass diffuses smoothly beyond the body by 25% of its width (11 dock / 50.5 radial logical pixels). A cached opacity texture uses a linear-time Euclidean distance transform and a Composition SOURCE_IN effect to fade the live backdrop, without enlarging the Qt hit region. Screen layout, scaling and compositor events coalesce into a non-activating graphics/resource/mask/Glass rebuild. Pending recovery never reopens a dismissed overlay.

Closing settings reverses Gentle bloom from its current progress, in both layouts. Toggling during either direction continues from that frame with proportional duration. Tool and setting input stays blocked during the transition. Hide and docking changes clear animation state; Reduced motion and software rendering switch instantly.

The release passed all eight Windows suites, a larger-scale rail preview, packaged startup checks and verification of all 1,587 installer files. It was installed locally and its files matched the tested package. Justice authorized GitHub publication on November 6, 2026; the publisher verified the uploaded installer, source archive and checksums before making 0.3.13 the latest stable release. Physical pen hardware and actual Crystal connect/disconnect remain acceptance checks. Future update notes still require Justice or Bassem approval before posting or upload.

# Previous release: 0.3.10

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
