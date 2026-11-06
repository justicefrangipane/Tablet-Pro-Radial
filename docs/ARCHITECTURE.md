# Native radial architecture — v0.2.0

This implementation separates menu meaning and gesture decisions from appearance, editing, and operating-system input. The portable core is C++20 without Qt dependencies. Qt Quick provides the desktop editor, overlay, rendering, and input adapter.

| Component | Responsibility |
| --- | --- |
| `core/model.*` | Versioned profile, stable IDs, six-slot tree, typed actions, validation |
| `core/gesture.*` | Pointer ownership, fixed geometry, selection, nesting, cancellation, release-only commit |
| `app/MenuSession.*` | Qt-facing gesture session, layout, docking progress, feedback events, preview or runtime dispatch |
| `app/StudioController.*` | Draft/applied revisions, selection, undo/redo, file operations, preferences |
| `app/ProfileCodec.*` | Strict JSON decoding and deterministic encoding |
| `ui/RadialSurface.*` | Icon artwork, voxel motion, light, child reveal, pointer capture |
| `platform/WindowPlatform.*` | Windows overlay behavior, foreground-target checks, shortcut and text delivery |
| `platform/StylusInput.*` | Isolated Windows HID helper, validated button-state transport, edge recognition, and start/stop lifecycle |

## Stable gestures

Every root or group has exactly six slots. Empty slots remain empty; deleting a button does not rotate or compact the remaining gestures. Nodes have stable string IDs and one parent. Validation rejects missing references, duplicate IDs, shared descendants, cycles, invalid action payloads, empty groups, and more than four group levels.

A gesture begins on a known button with its fixed center and outward axis. The actual contact point is recorded separately so a stationary tap near a wedge edge can still commit. Beginning a gesture copies the profile; editing the original cannot change that gesture’s targets or command.

An outward pull crosses a gate 42 logical pixels from the button center. Opening establishes a fixed child origin at that gate. Six child directions are `−75°, −45°, −15°, +15°, +45°, +75°` relative to the outward axis, with display centers 84 pixels from the child origin. Selection operates from 28 to 160 pixels, with angular gaps, a neutral center corridor, and small angular hysteresis. Continuing outward through a child group opens another level. Moving back across the branch’s return plane pops that level.

The core uses analytic segment intersections for gates, return planes, and selection boundaries. Coalesced samples along the same straight segments therefore preserve the selection history instead of skipping opening thresholds. One pointer owns the gesture. Other pointers cannot select or release it. Only release over an eligible action can return a command, once; cancellation, capture loss, and neutral release return none.

## Editor and runtime boundaries

`StudioController` maintains separate draft and applied profiles. Editing records undo snapshots and updates the preview. Save exports the draft; Apply validates it, checks current dispatch support, writes the applied revision with `QSaveFile`, and publishes it to the runtime. Invalid loads preserve the existing draft. Discard is itself undoable.

The preview reports commands without sending external input. The runtime uses the platform adapter. `MenuSession` defers incoming profile revisions until an active gesture ends, preserving both gesture meaning and displayed metadata. Resizing cancels active geometry. A branch that cannot fit is canceled rather than rearranged, including when the final release contains a coalesced move.

Normal launch opens the runtime menu. `--editor` opens the separate editor, and `--docked` starts with the runtime rail seated. The overlay window covers the selected screen's available geometry. Its floating radial center follows the invocation cursor, clamped to a 230-logical-pixel margin, or half the available dimension on smaller screens. The preview retains its default viewport-centered layout. Docking preserves the floating source anchor while the rail remains 35 logical pixels from the available area's left edge.

The runtime window's input mask follows the floating circle or dock, the toolbar, and visible child buttons. Only the transition temporarily uses the full overlay input area. These masks and the non-activating Windows window configuration still require hardware acceptance; the Linux offscreen platform cannot validate native window masking or click-through behavior.

## Pen invocation and helper lifetime

The supplied `TabletProHID` DLL is loaded only inside the same executable's `--pen-helper` process, before any Qt GUI is constructed. The observed DLL interface has no unregister operation, so stopping input terminates that process instead of unloading a module while its worker may still post events. The helper watches its parent process and exits if that parent ends. The main application starts input after loading the UI and stops it during shutdown; tests that merely construct the controller do not start a helper.

The helper validates the DLL interface and packet allocation, copies each 64-byte packet, and releases its DLL-owned allocation. It sends only changes to normalized side-button and proximity state over a bounded, versioned stdout protocol. The GUI validates readiness, sequence numbers, field ranges, and message sizes. Startup timeout, malformed messages, or helper failure stop input and report a status; they do not invoke actions.

Invocation uses the selected button's rising edge while the pen is in range. The first in-range sample establishes a baseline, so entering range with an already-held button does not open the menu. Leaving range, stopping input, and changing the selected button reset or re-establish that baseline. Button 1/2 selection and the enabled setting persist separately from menu profiles.

This is an initial integration of the supplied HID path, not a claim that every pen driver exposes these events. Vendor-specific direct Wacom capture remains deferred; a driver-configured Ctrl+Alt+Space route is provided. The helper currently invokes the menu only. Held drawing modifiers, next-stroke actions, sticky keys, and pointer-lifecycle action execution are not implemented.

## Motion is presentation

Quiet Gravity drives the icon-only radial-to-sidebar transition: source sprites separate into voxels, light guides their travel, and pieces assemble into dock buttons. The renderer consumes geometry and selection state; its animation cannot choose or execute an action. Child reveal is likewise visual, while hit geometry remains fixed.

The surface supplies batched scene-graph rendering and a software painting fallback using the same scene data. Reduced motion resolves to stable endpoints. Optional generated clicks and crackles follow session events and default to muted.

The Unfolded Bible palette source was unavailable. This implementation does not claim to reuse its code or reproduce its animation exactly.

## Format and platform seams

The new JSON format is `tabletpro.radial`, schema version 1. It stores explicit IDs, six-element slot arrays, and typed shortcut/text/disabled actions. Unknown schema versions and incompatible node payloads are rejected. A future Tablet Pro Studio importer should translate legacy data into this model, validate the result, and present an editable draft. Legacy parsing does not belong in the gesture engine.

Windows code isolates non-activating overlay behavior, hotkey invocation, foreground-target capture, and input delivery. Native dispatch rechecks the captured target and tracks inserted keys for partial-send cleanup. Other platforms currently simulate commands. The portable model and gesture core can be reused behind future macOS or iOS adapters, but neither native platform support nor an iOS keyboard extension is implemented here. Those integrations require their own input, windowing, permissions, and lifecycle designs.
