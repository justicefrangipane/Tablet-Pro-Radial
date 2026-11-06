# Tablet Pro Radial

A native Windows radial menu and editor with pen-button invocation, a docked rail, live Glass backgrounds, and updates from this repository.

## Download for Windows

**[Download the latest Windows x64 installer](https://github.com/justicefrangipane/Tablet-Pro-Radial/releases/latest)**

Open the latest release and download the file ending in **Windows-x64-Setup.exe**. Run it on each computer. Setup installs for your Windows account and includes the Qt runtime; administrator access and a separate Qt installation are not required. The installer is currently unsigned.

The current application version is **0.3.10**. This is the version used by the installer and updater.

## Get updates

Right-click the bronze-and-ivory fan icon beside the Windows clock and choose **Updates**, or use **Updates** in the editor. The same logo appears in the taskbar, Start menu shortcuts, app executable, and installer. The app is preconfigured for **justicefrangipane/Tablet-Pro-Radial**.

Choose **Check now → Download update → Install update**. Automatic checks also notify you when a newer stable release is available. Downloads are checked against GitHub's SHA-256 digest before installation. Your saved menus and preferences are kept.

Choose **Update log** from the tray, editor, or Updates window to browse changes by version. The installed version is marked, and the included history works offline. Changes are grouped under **New**, **Improved**, and **Fixed**.

After installing a new version, **What's new** opens automatically on its first launch in radial, docked, or editor mode. It shows that version's changes once per Windows account. Choose **Let's go** to continue or **View update log** for the full history. Regular restarts and reinstalling the same version do not repeat it.

For the older 0.3.3 build, enter `justicefrangipane/Tablet-Pro-Radial` in its Updates dialog once, then check for updates. You can also install the latest release directly.

## Start using Radial

Use **Ctrl+Alt+Space** or your configured pen button to show or hide the menu. Tap the center **ellipsis / Menu** to open **Dock, Edit, Sound, Glass, Hide, and Quit**. **Gentle bloom** spreads bronze from the center, revealing ivory settings in the existing outer ring over 760 ms. Tap **Back** to return to tools, including during the fill. The settings stay within the original wheel footprint. Closing the editor leaves Radial in the tray; **Quit** exits the app. Use only one Radial or Tablet Pro Studio instance at a time to avoid overlapping pen shortcuts.

Save your custom menu with **Save test radial…**, and use **Apply draft** to activate editor changes. See [Windows quick start](docs/RUN-WINDOWS.md) and [tray and updates](docs/TRAY-AND-UPDATES.md).

The main default menu contains **Brush, Eraser, Alt, Ctrl, Shift, and Tools**. The Tools group retains selection, color, text, and editing shortcuts.

- **Hold** a modifier button to keep the key down.
- **Drag out** to lock it for the current contact; lifting the stylus releases it.
- **Double-tap** to keep it on after lifting; tap it again to release. A small lock marks this state.

Held keys release when the menu is hidden, the editor opens, the target app changes, or Radial exits. They are not saved across restarts. Existing customized menus are preserved; **Use new default menu** loads the new layout as an undoable draft. Unchanged old defaults upgrade automatically.

For quick edits, choose **Menu → Edit**, then tap the button to open its inspector. Select **Modifier key** as its behavior to assign Alt, Ctrl, or Shift, then **Apply draft**.

![Bronze radial and settings ring](docs/images/bronze-radial.png)

The docked **Continuous rail** keeps the tools and settings inside one 44 px housing. Tap **•••** at the top: Gentle bloom flows downward from the ellipsis, filling the rail with bronze and revealing ivory settings. Tap the back arrow to return to drawing. Neither animation expands the menu or its input boundary. Settings become interactive when the fill finishes; underlying drawing actions are disabled as soon as it begins. **Reduced motion** and the software renderer switch immediately.

<details>
<summary>See the Continuous rail</summary>

| Drawing tools | Settings |
| --- | --- |
| <img src="docs/images/continuous-rail-tools.png" width="60" alt="Cool blue-gray tool rail"> | <img src="docs/images/continuous-rail-settings.png" width="60" alt="Bronze settings rail with warm ivory icons"> |

</details>

![Editor](docs/images/editor.png)

## What is implemented

- Six fixed root slots, clear Alt/Ctrl/Shift labels, and nested groups with six fixed child slots. Removing a button leaves its slot empty; other gestures retain their positions.
- Press, drag outward, choose a child, and release once to commit. A straight neutral corridor, angular gaps, backtracking, pointer ownership, and cancellation protect repeatable movement.
- Quiet Gravity: wedge surfaces and their icons break into small shaded voxels; light collects at the left rail, fragments travel toward each destination, and the reconstructed buttons settle with a restrained snap. Undocking reverses the same path.
- A separate editor with a menu hierarchy, icon/behavior inspector, root/group insertion, slot movement, subtree removal, undo/redo, Open, Save as, Discard, and Apply.
- Independent draft preview and applied runtime. Preview gestures report their action inside the editor. Apply validates and atomically persists a new profile; an active gesture finishes against its original snapshot.
- Shortcut, text, and held modifier actions, generated optional click/crackle cues, reduced motion, and persisted preferences. Sound starts muted.
- A separate non-activating Windows overlay with Ctrl+Alt+Space invocation, foreground-target checks, bounded input dispatch, and explicit failure reporting. Other operating systems use simulation.
- An isolated Windows HID helper using the supplied Tablet Pro library, with first/second side-button selection, rising-edge invocation, no repeated toggles while held, and cleanup when the parent exits. Native Wacom background capture is not assumed; the driver shortcut route uses the same menu.

## Build and publish an update

See the [release guide](docs/RELEASING.md). The Windows release uses Qt 6.11.1 / MinGW 13.1, plus Visual Studio C++ and the Windows SDK for the Glass bridge. Update the version in `CMakeLists.txt` and add its entry to `docs/update-history.json`, run the release build, then publish the verified installer with a matching `vMAJOR.MINOR.PATCH` tag. The bundled update log and default GitHub release notes use that same history file.

Source is included in every installer and attached separately to each release. The repository contains the editable application source; generated builds and installers are release assets.

## Build with CMake

With a desktop Qt kit available:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/tabletpro_radial
```

The core has no Qt dependency and can be built independently:

```sh
cmake -S . -B build-core -DTABLETPRO_BUILD_DESKTOP=OFF
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

## Try the editor

1. Select a node in **Menu structure**. Change its icon and behavior in **Button inspector**. Modifier keys also show Alt, Ctrl, or Shift on the menu. You can tap a preview button to select it, or right-click a runtime button to edit it directly.
2. Select **Root menu** to add a root button, or select a group to add one of its six children. Occupied slots cannot be overwritten. A newly created group must receive a child before it can be applied.
3. Open Tools, then press the Text icon in the preview and drag outward. Six child icons appear. Continue to a child and release; the status reports the action. Drag back through the parent to backtrack, or press Escape to cancel.
4. Use **Dock to rail** or the replay control to inspect Quiet Gravity. Try the same Text gesture from the rail, moving toward the canvas.
5. **Save as** exports a profile; **Apply draft** publishes and persists the runtime version. These are separate operations. **Discard** restores the applied version and can itself be undone.
6. On Windows, focus your drawing application and invoke the overlay with your selected pen side button or **Ctrl+Alt+Space**. Release those keyboard modifiers before completing a gesture. The runtime's editor control returns to the editor.

The included shortcuts are examples, not universal drawing-app mappings. Edit them for your application. Text actions support literal text; layout-dependent punctuation key shortcuts are deferred. No shell commands, scripts, or legacy AHK macros execute through this format.

The editor preview scales its canvas to fit smaller windows; its badge shows the zoom. This does not change runtime button sizes or gesture angles.

![Six Text children](docs/images/nested.png)

## Project map

| Location | Responsibility |
| --- | --- |
| `core/` | Qt-free model validation and deterministic gesture decisions |
| `app/` | Profile codec, editor transactions, preview/runtime sessions, audio, startup |
| `ui/` | Native icons, batched scene-graph voxel renderer, software fallback, pointer capture |
| `qml/` | Desktop editor and runtime overlay presentation |
| `platform/` | Windows window/input behavior; explicit simulation elsewhere |
| `vendor/tabletpro/` | User-supplied HID binaries and provenance; loaded only in the isolated helper |
| `examples/artist.radial.json` | Exported schema-1 starter profile |
| `tests/` | Core, editor/format, and non-Windows platform-simulation checks |
| `scripts/` | Windows build, validation, and packaging entry point |

See [architecture](docs/ARCHITECTURE.md), [profile format](docs/PROFILE-FORMAT.md), [implementation status](docs/STATUS.md), and [existing app audit](docs/EXISTING-APP-AUDIT.md).

## Scope retained for later

Legacy Tablet Pro import belongs in a separate translator into the new model. macOS native dispatch, iOS/custom-keyboard hosting, direct Wacom background capture, additional dock edges, automatic layout adaptation, repeating actions, arbitrary macros, and app-specific profile switching are not implemented in this version. The core and platform boundary leave room for them without embedding those concerns into gesture geometry.

The Unfolded Bible palette source was not available in the supplied files. The motion and renderer here are new code; no reuse of that source is claimed.
