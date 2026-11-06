# Native platform adapter

`platform/WindowPlatform.h/.cpp` implement the first Windows adapter. The portable
core does not depend on either file. Non-Windows builds simulate accepted actions
through `shortcutSimulated` or `textSimulated`; they do not control another application.

## Integration

Add the two platform files to the Qt executable, enable AUTOMOC, link Qt6::Quick
and, on Windows, user32. Construct `tabletpro::WindowPlatform` on the GUI thread
after `QGuiApplication` and before loading QML. Keep it alive until windows close.

```cmake
target_sources(tablet_pro_radial PRIVATE
    platform/WindowPlatform.h platform/WindowPlatform.cpp)
target_link_libraries(tablet_pro_radial PRIVATE Qt6::Quick)
if(WIN32)
    target_link_libraries(tablet_pro_radial PRIVATE user32)
endif()
```

The runtime QML window should initially use `visible: false`. Call
`configureOverlay(runtimeWindow)` before showing it. Never call `requestActivate`
on the runtime. The editor must use a separate, normal window, and must not be
passed to `configureOverlay`.

At gesture start, call `captureForegroundTarget()` and retain its nonzero result
in the C++ gesture controller. Connect `targetLost` to gesture cancellation. On a
committed action, call `executeShortcut(key, modifiers, capturedTarget)` and show
`lastError` if false. A successful dispatch consumes the captured target; each
gesture needs a new capture. On cancellation call `releaseTarget()`.
Text actions use `executeText(text, capturedTarget)` with the same capture rules.

Optional `registerToggleHotkey()` registers Ctrl+Alt+Space with repeat suppression.
`toggleRequested(target)` arrives after capturing the foreground target. Do not
send an action while the user still holds the invoking modifiers. The adapter
rejects an action in this case, instead of releasing a user's keys.

## Behavior and limits

- Runtime uses Qt's focus refusal plus WS_EX_NOACTIVATE / WS_EX_TOOLWINDOW,
  non-activating topmost placement, and mouse/pointer activation refusal.
- Foreground identity includes HWND, process ID, and thread ID. Capture rejects
  this application's own windows. A 30 ms watch cancels an observed focus change;
  dispatch checks the foreground again immediately before insertion.
- This is not an atomic target-and-send guarantee. Windows SendInput delivers
  global input. A focus change in the final race window remains possible, and a
  very brief away-and-back change can fall between polling samples. No global
  keyboard/pointer hook is installed. Hardware validation is still required.
- Allowed keys: A–Z, 0–9, F1–F24, Escape, Enter, Space, Tab, Backspace, Delete,
  Insert, Home, End, PageUp, PageDown, ArrowLeft, ArrowRight, ArrowUp, ArrowDown
  (Left/Right/Up/Down aliases accepted). Allowed modifiers:
  Ctrl, Shift, Alt, Meta; Control and Win aliases normalize to Ctrl and Meta.
  Duplicate modifiers, mixed shortcut strings, punctuation keys, and arbitrary
  commands are rejected. Punctuation shortcuts need a later keyboard-layout
  policy, rather than guessing an OEM key. The profile schema can describe Minus,
  Equal, BracketLeft, BracketRight, Backslash, Semicolon, Quote, Comma, Period,
  Slash, and Backquote, but this adapter rejects them. The editor's Apply action
  must call `shortcutSupported` for every shortcut before activating a profile.
- Explicit text actions accept 1–4096 UTF-16 code units, validate surrogate pairs,
  reject NUL, and send Unicode packet down/up pairs. They do not use clipboard,
  keyboard-layout guesses, or a command interpreter. UTF-16 surrogate pairs are
  sent as consecutive units. Newline/control-character interpretation and apps
  that ignore VK_PACKET still require target-application testing.
- Any already-down Ctrl/Shift/Alt/Windows modifier or the requested main key
  rejects insertion. GetAsyncKeyState reports aggregate current state, not the
  provenance of that state; this is deliberately conservative.
- One contiguous SendInput batch includes all downs and reverse-order ups. A
  partial return records only unmatched key-downs in the inserted prefix.
  Unicode packet units and virtual keys are tracked separately. Immediate cleanup
  releases only those keys, with at most three attempts. It
  never issues a blanket modifier reset. If cleanup is blocked, further input is
  disabled; error text asks the user to release stuck keys and restart. No stale
  cleanup is attempted later or during destruction, when a user might have
  physically pressed the same key.
- Elevated applications may reject SendInput through UIPI. Neither the return
  count nor last-error code identifies UIPI specifically. The adapter does not
  elevate itself, claim success on a short insertion, or steal focus to retry.
- There is no arbitrary command execution, legacy AHK interpreter, custom pen
  DLL integration, or global pen-button hook in this adapter.

## Validation status

Reviewed against official Microsoft and Qt documentation. The Linux simulation
compiled with Qt 6.8.3, C++20, GCC 13.3, AUTOMOC, and `-Wall -Wextra -Werror`.
An offscreen smoke executable passed key/modifier allowlist checks, duplicate
alias rejection, one-shot target consumption, cancellation, Unicode preservation,
empty/oversized/NUL/malformed-surrogate rejection, the 4096-unit boundary, and
non-activating/topmost window flags. This does not validate Windows input.

The Windows branch has **not** been compiled or run on Windows. The following
are required Windows acceptance checks before a release:

1. Build with Qt 6 / MSVC and run the normal and high-DPI configurations.
2. Keep a drawing application focused; mouse, touch, and pen contact on the
   runtime must leave that foreground window unchanged. Editor fields must still
   accept normal keyboard input.
3. Verify Ctrl+Z, Ctrl+Shift+Z, plain B, arrow, Delete, and F-key dispatch in a
   disposable document. Reject malformed key tokens and duplicate modifiers.
4. Hold each left/right modifier and the requested main key; dispatch must fail
   without releasing them. Release Ctrl+Alt+Space before completing a gesture.
5. Change focus or close the target during a gesture; it must cancel without
   dispatch. Opening the editor must cancel the runtime target.
6. Exercise SendInput return counts 0 through N in a Windows API test shim; only
   inserted, unmatched downs may be released. Force cleanup failure and verify
   later gestures cannot dispatch. This fault-injection test is not yet present.
7. Use a target elevated above the menu; a blocked insertion must produce an
   honest failure, with no automatic elevation/retry.
8. Verify hotkey conflict reporting, repeat suppression, and release on exit.
9. Insert Latin text, a non-Latin script, emoji surrogate pairs, and newlines in a
   disposable text field. Reject empty, over-limit, NUL, and malformed-surrogate
   text before insertion. Verify a partial Unicode batch releases the unmatched
   Unicode packet unit, never a modifier.

## Official references

- [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)
- [KEYBDINPUT / Unicode packets](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-keybdinput)
- [GetAsyncKeyState](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate)
- [Extended window styles](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles)
- [SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)
- [SetWindowLongPtrW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowlongptrw)
- [WM_MOUSEACTIVATE](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mouseactivate)
- [WM_POINTERACTIVATE](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointeractivate)
- [RegisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey)
- [Qt window flags](https://doc.qt.io/qt-6/qt.html#WindowType-enum)
- [Qt native event filtering](https://doc.qt.io/qt-6/qabstractnativeeventfilter.html)
