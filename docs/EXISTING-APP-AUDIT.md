# Existing Studio source and support-library audit

Inputs: `01-Tablet-Pro-Studio-2026-09-22.ahk` and the supplied `lib (2).zip`. The script identifies itself as Studio 6.9.2. References below are source line numbers in that script unless a support filename is named. No supplied executable or DLL was run during inspection.

## What the existing app does

| Area | Observed implementation | Consequence for the new design |
| --- | --- | --- |
| Radial structure | Two flat A/B banks with 12 positions, eight outer and four inner; creation at 8270–8300, loading at 13339–13405 | Six stable slots with real recursive groups remain a deliberate new interaction model |
| Docker and stacks | Mini-editor collects reusable TOUCH tiles; stacks cycle actions at one location; saving to radial flattens the selected stack entry (8158–8166, 9813–9817) | Preserve new screen-edge docking and outward submenus rather than treating the old Docker as equivalent |
| App profiles | Executable/title matching, activation and auto-switch policy (3475–3559); registry or INI persistence (16740–16790) | Future profile selection belongs outside gesture geometry; no legacy import in this build |
| Action scope | Chords, modifiers, pointer operations, TOUCH modes, text/image/URL/app actions, macros and repeat (8666–8954) | Current native Shortcut/Text actions cover a subset; held/sticky/next-stroke actions need their own lifecycle before being added |
| Pen invocation | Raw HID callback; barrel/invert paths, quick-action cycling and tilt invocation (1165–1185, 18624–18678, 18922–18945, 19404–19462) | Invocation must be independent from menu selection and rendering |
| Visual/audio assets | Raster themes, icon font metadata, 16 color swatches, short slot sounds | Retain original native icons and procedural voxels; use assets as reference rather than preserving the old editor architecture |

## Input integration used in 0.2

The HID binaries export a registered-message getter and `RegisterReceiveSurfacePenSlimDataEvents(HWND,1,0,genericPen)`. The first two flags have no supplied header documentation; this version preserves the original values and genericPen=0. Static inspection shows registration returns zero on success. The callback receives a pointer to sixteen 32-bit fields (64 bytes); offsets4/8 are invert/barrel, offset16 is in-range. The old callback's field list and `HeapFree` are at18811–18830.

There is no unregister or API-version export in these HID binaries. Consequently the new app loads them only inside a separate helper process. It validates allocation ownership/size, copies the packet, frees it, and sends normalized button/range changes over a private child-process pipe. The parent detects rising edges, suppresses repeated held samples, and cancels/rebaselines after proximity loss. The helper watches the parent process handle and exits if the parent exits or crashes. Raw device coordinates are not used as Qt logical coordinates; invocation uses the operating-system cursor position.

Static inspection also confirms background raw input: the x64 DLL sets `RIDEV_INPUTSINK` (0x100) and registers digitizer usage page0x0D, usage2 on its worker window. Unlike the supplied Wacom wrapper, its subscription is not restricted to foreground Studio windows. The DLL briefly calls `ShowWindow(SW_SHOW)` during initialization; the helper is launched with `STARTF_USESHOWWINDOW` and `SW_HIDE` so that first show remains hidden. Actual focus preservation still requires Windows testing.

The support file `TabletProWacom.pre-focus-pointer.ahk:6–8` states that its WinTab stream works only while Studio owns keyboard focus. Its packet convention is also different: packed flags and sequence, not a heap pointer. The supplied main script contains no integration with that wrapper, and its synthetic buffer must not be passed directly to the old freeing callback. The new runtime therefore does not load that Wacom bridge. Driver mapping to Ctrl+Alt+Space provides the invocation route for this version.

The gestures DLL is experimental touch infrastructure; its initialization call is commented out at439. It is not needed for the new deterministic nested-menu gestures and is not bundled.

## Remaining source gaps

- Main includes `cli-interface.ahk`, `Gdi.ahk`, and `Class_CtlColors.ahk`, which were not in these uploads. `Gdip_All.ahk` is present in the support library.
- `TabletProWacom.ahk`, `TabletProWacomx32.dll`, `TABLET-PRO-WACOM-DLL-INTEGRATION.md`, and the support README are empty.
- HID/Wacom/gesture DLL implementation source and headers are not supplied.
- Real exported profile files and TOUCH presets are not supplied.
- The archive's swatch tutorial demonstrates HEX palette import. It is not the Unfolded Bible palette animation source, which is still unavailable.

These gaps do not prevent the new menu/editor or the isolated supplied-HID route. They limit claims of full existing-app coverage and hardware compatibility.
