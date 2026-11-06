# Windows releases

Official repository: https://github.com/justicefrangipane/Tablet-Pro-Radial

## Build

1. Increment `project(TabletProRadial VERSION ...)` in `CMakeLists.txt` and add the matching entry at the top of `docs/update-history.json`. Include its release date (`YYYY-MM-DD`), short title, and plain-language `new`, `improved`, or `fixed` arrays. Keep previous entries. The build checks version order, dates, and nonempty changes; packaging also requires the current entry. Never replace an installer already released with the same version. The Qt kit must include Shader Tools to compile the Gentle bloom shader; the compiled shader is embedded in the app.
2. Install Qt 6.11.1 MinGW x64, MinGW 13.1, CMake, Ninja, Python 3, and Visual Studio C++ x64 tools plus Windows SDK. The Glass bridge uses MSVC; the main application uses MinGW.
3. Supply a trusted previously installed or verified payload from the same Qt kit as `RuntimeTemplate`. Only its third-party notices and the three Microsoft runtime DLLs are reused; the app, Glass bridge, HID copy, and Qt deployment are rebuilt. Keep the associated notices with any updated dependency.
4. Run from the repository root:

```powershell
.\scripts\build-release.ps1 -RuntimeTemplate 'C:\path\to\verified\Tablet-Pro-Radial-0.3.4' -Python 'python'
```

The script refuses to overwrite an existing version. It builds and runs all eight Windows CTest suites, deploys Qt, creates the configured installer and source archive, verifies every extracted file, and runs editor/runtime smoke checks with Qt removed from PATH. Output is in `dist/`; screenshots are in `build/windows/release-smoke/`.

The installer contains the matching source archive and third-party notices. Review the source and contents before publication. Exclude private settings, credentials, local handoff notes, and unrelated files. The HID library is the supplied Tablet Pro binary; its provenance is recorded under `vendor/tabletpro/`.

## Publish

Install GitHub CLI and sign in to an account with write access to this repository. Commit and push the reviewed source first. By default, release notes are generated from the current entry in `docs/update-history.json`, the same data embedded in the app's offline Update log. An optional `-NotesFile` overrides the GitHub text; use a file outside the source tree or commit it before publishing.

```powershell
git push origin main
.\scripts\publish-release.ps1
```

This uploads a **draft** release and verifies every asset's uploaded state, byte count, and GitHub SHA-256 digest. To upload, verify, and publish in the same invocation, add `-Publish`. To publish a previously reviewed draft:

```powershell
gh release edit v0.3.10 --repo justicefrangipane/Tablet-Pro-Radial --draft=false --latest
```

Replace the example tag with the version just built. Never use fake newer releases to test the production feed. The updater requires a public stable `vMAJOR.MINOR.PATCH` release and the exact installer name `Tablet-Pro-Radial-MAJOR.MINOR.PATCH-Windows-x64-Setup.exe`.

## Verify the live feed

```powershell
cmake --build build/windows --target radial_live_update_check
# Run with the Qt and MinGW bin folders on PATH:
.\build\windows\radial_live_update_check.exe justicefrangipane/Tablet-Pro-Radial 0.3.3 0.3.4 --download
.\build\windows\radial_live_update_check.exe justicefrangipane/Tablet-Pro-Radial 0.3.4 0.3.4
```

Use the previous and newly published versions for later releases. This invokes the application's actual updater with isolated temporary settings, checks discovery, downloads and validates the real installer, and verifies that the current version does not offer itself. It does not install anything. It is opt-in and is not part of offline CTest runs.

Finally test a normal user installation on Windows: update from an older installed build, retain a saved profile, quit and install, and confirm the new version and profile. Physical pen input and a second computer require their own acceptance checks.

API contract: [GitHub Releases](https://docs.github.com/en/rest/releases/releases) and [release assets](https://docs.github.com/en/rest/releases/assets).
