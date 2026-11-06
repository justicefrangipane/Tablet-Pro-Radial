# Radial profile, schema 1

Profiles are UTF-8 JSON with `format: "tabletpro.radial"` and `schemaVersion: 1`. The root object also contains `id`, `name`, `rootSlots`, and `nodes`. See `examples/artist.radial.json` for a complete exported profile.

`rootSlots` and each group's `slots` contain exactly six node IDs or `null`. Slot indices are part of the gesture vocabulary. Null entries preserve a position; they are not compacted or reordered at load time.

Every node has `id`, `label`, `icon`, and `kind`. Labels are editor metadata. A node ID may have only one placement in the tree. Cycles, missing references, excessive depth, duplicate IDs, and empty groups are rejected when loading, saving, or applying a finished profile. The editor can temporarily hold an unfinished draft.

An action node has `kind: "action"` and one `action` object:

```json
{
  "id": "text.bold",
  "label": "Bold",
  "icon": "bold",
  "kind": "action",
  "action": {
    "id": "command.text.bold",
    "type": "shortcut",
    "key": "B",
    "modifiers": ["Ctrl"],
    "text": ""
  }
}
```

Action types are `shortcut`, `text`, `modifier`, and `none`. A `modifier` action uses `key: "Alt"`, `"Ctrl"`, or `"Shift"`, with empty `text` and `modifiers` fields. It holds during contact, keeps holding when dragged out until lift, and latches on a double tap until the next tap. Its key name is visible on the menu. Runtime held state is never serialized. Older builds reject this new action type without changing the file. Modifiers are unique members of `Ctrl`, `Shift`, `Alt`, and `Meta`. Unused payload fields must be empty. A text action supplies `text`, with an empty key and modifier array. The model validates key tokens; Apply additionally checks the platform adapter's supported subset. There is no embedded executable code.

A group has `kind: "group"` and `slots`, with no action payload. An action cannot also have child slots. Branches can nest to the core's four-level limit. Files larger than 1 MiB or containing more than 256 nodes are rejected before becoming a draft.

Stable export sorts nodes by ID, retains six-slot arrays, and writes through `QSaveFile`. A failed load leaves the current draft intact. A failed Apply leaves the prior applied profile in effect.

## Portable test file, schema 1

**Save test radial…** opens a native Save As dialog proposing `Test.radial.json` in Documents. It saves the editor's menu draft and four appearance preferences in one UTF-8 JSON document. **Save** / **Ctrl+S** updates the last successfully opened or saved file; before a file is chosen it opens Save As. **Ctrl+Shift+S** always opens Save As. At compact editor widths the button reads **Save test…**, and Ctrl+S remains available for the current file.

The format is an envelope around the unchanged core profile:

```json
{
  "format": "tabletpro.radial.test",
  "schemaVersion": 1,
  "profile": { "format": "tabletpro.radial", "schemaVersion": 1 },
  "settings": {
    "soundEnabled": true,
    "reducedMotion": false,
    "durationMs": 1640,
    "backdropEnabled": true
  }
}
```

The shortened `profile` above must contain all required profile fields in a real file. `examples/Test.radial.json` is a complete starter document with the settings shown above.

`settings` may be omitted entirely. If present, it must contain exactly these four fields: three booleans and an integer `durationMs` from 250 through 2400. Missing fields, unknown fields, wrong types, fractional or out-of-range durations, unknown envelope fields and unsupported document versions reject the whole load. The entire envelope, including its profile, is limited to 1 MiB. Device bindings, pen permissions, foreground targets and startup actions are deliberately excluded.

Opening a test file stages both the menu and its optional settings. It does not execute actions, change the overlay's applied menu, or change live preferences. The editor displays a notice that file settings are pending. **Apply draft** validates the menu, atomically writes the applied profile, then activates the staged settings and persists them. A failed validation or profile write leaves live settings untouched. **Discard** removes the staged changes; opening and discarding are undoable, including imported settings and file association.

The existing Sound and Reduce motion controls continue to adjust live preferences. When imported settings are pending, Apply replaces those live values with the file's staged settings, as the notice states. Saving an imported draft preserves its pending settings. Saving without pending settings captures the current live values. Save never publishes the draft to the overlay, and cannot target the private `applied.radial.json` path.

Plain `tabletpro.radial` schema-1 profiles still open without importing or altering settings. Saving one through Save or Save test radial writes the portable test envelope to the explicitly chosen file. The app's private applied profile remains the original plain format; QSettings continues to hold the applied appearance preferences.

## Future compatibility boundary

The current decoder reads only schema 1 of this new format. A future version must add an explicit migration step. Legacy Tablet Pro/AHK settings should be read by a separate importer that returns this model plus a report of unsupported behavior. The renderer and gesture engine should never parse legacy configuration or execute legacy script strings.
