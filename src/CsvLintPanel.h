// CsvLintPanel — the docked CSV Lint window (port of Forms/CsvLintWindow.cs)
// plus the per-file definition registry and the indicator-based column
// coloring that replaces the Windows external lexer.
#pragma once
#include <gtk/gtk.h>
#include "NppPluginInterfaceLinux.h"

/// One-time wiring; `data` must outlive the plugin.
void csvPanelInit(NppData *data);

/// The dockable content widget (created lazily).
GtkWidget *csvPanelView(void);

/// NPPN_BUFFERACTIVATED — port of Main.CSVChangeFileTab(): look up / infer the
/// definition for the current file, refresh the panel, recolor.
void csvPanelOnBufferActivated(void);

/// NPPN_FILECLOSED — drop the stored definition (Main.RemoveCSVdef).
void csvPanelOnFileClosed(uintptr_t bufferID);

/// SCN_MODIFIED (insert/delete) — debounced recoloring.
void csvPanelOnModified(void);

/// NPPN_SHUTDOWN — release UI and timers.
void csvPanelShutdown(void);

/// Menu: Analyse data report (statistics into a new file).
void csvPanelAnalyseReport(void);

/// Menu: Select columns (subset/reorder into a new file).
void csvPanelSelectColumns(void);

/// Menu: Convert data (SQL/XML/JSON into a new file).
void csvPanelConvertData(void);

/// Plugin menu "Generate metadata" (MetaDataGenerateForm + CsvGenerateCode).
void csvPanelGenerateMetadata(void);

/// Plugin menu "Settings" + panel gear button: settings window, then save +
/// recolor with the new values.
void csvPanelShowSettings(void);
