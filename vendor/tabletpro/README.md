# Supplied Tablet Pro HID binaries

`TabletProHIDx32.dll` and `TabletProHIDx64.dll` are unchanged files from the user's `lib (2).zip`, under `lib/`. They are included for the existing Tablet Pro pen-input path requested for this development build. Their source/header and a standalone license manifest were not in the upload.

Only the architecture-matching DLL is copied beside the Windows application under `lib/`. `StylusInput` loads it by absolute path inside the isolated `--pen-helper` process. The parent app does not load it. The x64 binary requires the Microsoft Visual C++ runtime; the cross-built portable Windows package includes its app-local dependencies.

No supplied Wacom DLL, gestures DLL, test executable, icon font, or themed sound pack is required or bundled by this project. See `docs/EXISTING-APP-AUDIT.md` for the observed ABI and limitations. Binary/device compatibility still requires testing on the target Windows machine.
