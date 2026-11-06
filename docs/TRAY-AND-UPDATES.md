# Tablet Pro Radial 0.3.6

The blue radial icon beside the Windows clock stays available while the editor
and overlay are hidden. Windows may initially place it under the hidden-icons
arrow. Click to show/hide the radial; right-click for Open editor, Updates, or Quit.
Closing the editor keeps Radial available in the tray. Quit closes the application,
removes its icon, and stops its pen listener and hotkey. Unapplied drafts offer
Apply, Discard, or Keep editing before quitting. Updates and Quit are also in the
editor's bottom bar. The Glass rendering fix from 0.3.2 is included.

## Continuous rail

The docked menu uses a single 44 px housing with fine separators. Tap the top
ellipsis to replace the drawing tools with Send to radial, Edit, Sound, Glass,
Hide, and Quit. The open settings use a bronze background and warm ivory icons;
the drawing tools retain their cool blue-gray appearance. Sound and Glass show
an illuminated indicator when enabled. The back arrow restores the tools.

Both states have the same width, including the Windows input and glass boundary.
Settings use 44 px targets and do not leave hidden drawing actions underneath.
The floating radial keeps its centered controls; its settings panel shares the
new warm palette. Saved profiles, button assignments, and preferences are retained.

## GitHub updates

Open Updates from the tray or editor and click Check now. This release is
preconfigured for `justicefrangipane/Tablet-Pro-Radial`:
https://github.com/justicefrangipane/Tablet-Pro-Radial/releases/latest

Older 0.3.3 users can enter that repository in Updates once. An existing saved
repository choice overrides the packaged default. The repository choice and
automatic-check preference are saved.

Automatic checks run after startup and at six-hour intervals after a successful
check (30-minute retry after errors). Check now bypasses that interval. An orange
dot and tray notification indicate a newer version. The Updates window shows
release notes, Download update, progress/cancel, and Install update. Downloads
must match GitHub's size and SHA-256 digest; the checksum is checked again before
launching. Installing is an explicit user action. Radial resolves unapplied
drafts, launches setup, then exits; setup waits for that process before proceeding.
Saved menu files, applied profiles, and appearance preferences are preserved.

Offline errors, request limits, malformed metadata, and unavailable repositories
are shown as errors rather than 'up to date'. Automatic checks can be disabled.
Only public, stable releases with numeric `vMAJOR.MINOR.PATCH` tags are supported.
Private repositories and prereleases are not supported in this version. The app
sends no GitHub token or app/profile data. GitHub receives ordinary HTTPS requests.
The update trust model is HTTPS plus the selected GitHub repository and its asset
digest. The installer is not Authenticode-signed; account security still matters.

## Publish future updates

1. Use the public `justicefrangipane/Tablet-Pro-Radial` repository. See RELEASING.md.
2. Increment the version in CMakeLists.txt; it supplies the app and setup versions.
3. Build/test the app, then deploy it with windeployqt (including Qt Widgets, Network,
   the Schannel TLS plugin, TabletProGlass.dll and the existing HID dependencies).
4. Package with `python installer/build-glass-fix.py --payload PATH --repository OWNER/REPO`.
   Omitting --repository uses `justicefrangipane/Tablet-Pro-Radial`. Existing users'
   repository preference overrides the packaged default.
5. Create a GitHub release tagged `vMAJOR.MINOR.PATCH`. Upload the exact installer
   name `Tablet-Pro-Radial-MAJOR.MINOR.PATCH-Windows-x64-Setup.exe`. Use a draft until
   upload and validation are complete, then publish it as the latest stable release.
   GitHub's asset API must include `state: uploaded`, byte size, and `digest: sha256:...`.
   Include concise release notes; source is embedded in the installer.
6. Check from an older installed version: version appears, download verifies,
   Install update waits for the app to exit, setup succeeds, settings survive,
   and the new version no longer offers itself.

The release scripts upload a draft, verify each asset digest and size, then
publish the reviewed version. Automated live acceptance can check and download
the real release without installing it. Actual installation, settings retention,
and physical pen behavior still require a normal Windows device acceptance run.

## Validation

`ctest --test-dir build/windows --output-on-failure` includes tray menu/quit draft
tests and update parser/network tests: comparison, source changes, opt-out,
throttling, redirects, size limits, corrupted downloads, cancellation, and modified
installer rejection. The packaged app supports `--smoke --runtime-smoke --output DIR`.
Installer `--verify NEW_EMPTY_DIRECTORY` extracts and verifies the payload without
installing. It includes the matching source archive.

References: https://doc.qt.io/qt-6/qsystemtrayicon.html and
https://docs.github.com/en/rest/releases/releases#get-the-latest-release
