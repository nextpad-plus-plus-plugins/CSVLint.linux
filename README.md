# CSV Lint for Nextpad++ — Linux (GTK4) port

Check syntax, validate data, reformat and convert CSV / fixed-width text
files, inside Nextpad++ on Linux. Port of the macOS port of Bas de Reuver's
[CSV Lint plug-in for Notepad++](https://github.com/BdR76/CSVLint) (GPL-3.0;
the macOS port is a rewrite against the Nextpad++ plugin API, not a
recompile of the C#/WinForms original).

## Features (matching the macOS port)

- **CSV Lint window** (docked panel): Detect columns (automatic inference or
  manual parameters), editable schema.ini metadata with Apply, per-file
  column color toggle, Sort on column, Add column (pad / search&replace /
  split), Reformat (separator, datetime format, decimal separator, quotes,
  align), Validate data with an error report — double-click an error line to
  jump to (and select) the offending value in the editor.
- **Column coloring** with the upstream 12-color palettes (light + dark),
  drawn with Scintilla indicators 20–31 — the Windows plugin's external
  lexer replaced by a self-contained mechanism (the ComparePlus pattern),
  coexisting with whatever lexer owns the buffer.
- **Analyse data report** — per-column statistics into a new tab.
- **Select columns** — subset/reorder into a new tab.
- **Convert data** — SQL (MySQL / MS-SQL / PostgreSQL, batched inserts) /
  XML / JSON into a new, syntax-highlighted tab.
- **Generate metadata** — schema.ini, W3C CSV schema JSON, datadictionary
  CSV, Python pandas, R, PowerShell scripts.
- **schema.ini interop** — definitions saved by Apply are written as
  `[filename]` sections next to the data file (MS-ODBC style, same format
  as the Windows plugin); existing sections are picked up when a file is
  opened.
- **Settings** window (Analyze / Edit / General property-grid parity) and
  the same `CSV Lint.ini` persistence format.

## How the platform layer maps

The engine — separator/type inference, schema.ini parse/serialize, the
datetime mask parser, validation, sort/split/reformat editing, the SQL/XML/
JSON converters and the six metadata generators — is **byte-identical to
the macOS port** (pure C++ throughout). The platform layer maps as:

| macOS | Linux |
|---|---|
| AppKit docked panel (NSTextView boxes + toolbar) | GTK4 box + two GtkTextViews |
| NSAlert / runModalForWindow dialogs (8) | GTK4 modal dialogs, field-for-field |
| NSNotificationCenter text-did-change | GtkTextBuffer "changed" (with a programmatic-set guard — GTK fires it for set_text too, macOS does not) |
| dispatch_after debounce / retry | g_timeout_add |
| NSString file IO (settings ini) | g_file_get/set_contents |
| NSWorkspace openURL | gtk_show_uri |

Host-interface notes (the usual Linux-host differences):

- `NPPM_DMM_REGISTERPANEL` takes wParam=title, lParam=widget (reversed
  from macOS).
- `NPPM_SETCURRENTLANGTYPE` has no handler here; new-tab highlighting uses
  `NPPM_SETBUFFERLANGTYPE(0, lang)`.
- `NPPM_GETEXTPART` returns the extension *with* the leading dot (Windows
  parity; macOS strips it) — normalized in one place.
- The host requires an `isUnicode` export and silently skips plugins
  without it.

## Building

```sh
cmake -B build -S .
cmake --build build -j"$(nproc)"
cmake --install build   # -> ~/.local/share/nextpad++/plugins/CSVLint/
```

Requires `libgtk-4-dev` and the Nextpad++ GTK4 tree checked out alongside
this folder.

### Tests

```sh
ctest --test-dir build --output-on-failure
```

`csv_engine` (138 assertions, no display needed) runs the engine against
the upstream Windows plugin's testdata fixtures, vendored under
`test/testdata/` (datetime masks, quoted/fixed-width parsing, schema.ini
round trips, inference on six fixture files, validation messages, sort/
split/reformat, all converters and generators). `plugin_loader` checks the
dlopen surface, menu shape (11 items, 3 separators) and the FuncItem ABI.

## Linux notes

- No pre-bound keyboard shortcuts (this host's `FuncItem` has no shortcut
  field) — bind them in *Settings ▸ Shortcut Mapper ▸ Plugin commands*.
- In dialogs, press the OK button (or Tab to it) — Return while a dropdown
  has focus opens the dropdown (GTK behavior; macOS popup buttons pass
  Return through to the default button).

## License

GNU General Public License v3, as upstream.
