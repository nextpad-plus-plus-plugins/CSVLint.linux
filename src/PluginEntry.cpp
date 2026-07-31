// CSVLint.linux — Linux (GTK4) port of the CSV Lint plug-in for Notepad++
// (original Windows plugin by Bas de Reuver, https://github.com/BdR76/CSVLint —
// C#/WinForms; ported from the Nextpad++ macOS port, which is a rewrite
// against the Nextpad++ plugin API).
//
// PluginEntry.cpp — plugin contract: menu items (mirroring the Windows menu
// 1:1), docked-panel registration, notification routing, settings lifecycle.
//
// Column coloring note: the Windows plugin ships a managed ILexer and
// registers "CSVLint" as an external language. This host has no
// external-lexer machinery, so this port colors columns with Scintilla
// INDICATORS instead (the ComparePlus pattern) — self-contained, no host
// changes, coexists with whatever lexer owns the buffer.

#include <gtk/gtk.h>
#include "NppPluginInterfaceLinux.h"
#include "Scintilla.h"
#include "CsvLintPanel.h"
#include "CsvLintDialogs.h"
#include "CsvSettingsIO.h"
#include <string>
#include <cstring>

#define PLUGIN_NAME    "CSV Lint"
#define PLUGIN_VERSION "1.0.0"
static const int NB_FUNC = 11;

static NppData  nppData;
static FuncItem funcItem[NB_FUNC];

static intptr_t   g_panelHandle = 0;
static GtkWidget *sPanelView    = nullptr;
static bool       sPanelVisible = false;

// ═══════════════════════════════════════════════════════════════════════════
//  Host helpers
// ═══════════════════════════════════════════════════════════════════════════

static intptr_t npp(uint32_t msg, uintptr_t w = 0, intptr_t l = 0) {
    return nppData._sendMessage(nppData._nppHandle, msg, w, l);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Docked panel
// ═══════════════════════════════════════════════════════════════════════════

static void ensurePanelView() {
    if (sPanelView) return;
    sPanelView = csvPanelView();
}

static bool panelIsShown() {
    // Ask the widget itself (the host's panel-frame ✕ hides the content
    // without telling us) — the toggle then self-corrects.
    return sPanelView && g_panelHandle > 0 && gtk_widget_get_mapped(sPanelView);
}

static void toggleLintWindow() {
    ensurePanelView();
    if (g_panelHandle == 0) {
        // Linux ABI: wParam = title, lParam = widget (REVERSED from macOS).
        intptr_t h = npp(NPPM_DMM_REGISTERPANEL,
                         (uintptr_t)"CSV Lint",
                         (intptr_t)sPanelView);
        if (h > 0) g_panelHandle = h;
    }
    if (g_panelHandle == 0) {
        csvAlert(PLUGIN_NAME, "The host rejected the panel registration.");
        return;
    }

    bool targetShown = !panelIsShown();
    sPanelVisible = targetShown;
    npp(NPPM_SETMENUITEMCHECK, (uintptr_t)funcItem[0]._cmdID, targetShown ? 1 : 0);
    npp(targetShown ? NPPM_DMM_SHOWPANEL : NPPM_DMM_HIDEPANEL, (uintptr_t)g_panelHandle, 0);
    if (targetShown) csvPanelOnBufferActivated();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Menu commands
// ═══════════════════════════════════════════════════════════════════════════

static void cmdLintWindow()       { toggleLintWindow(); }
static void cmdAnalyseReport()    { csvPanelAnalyseReport(); }
static void cmdSelectColumns()    { csvPanelSelectColumns(); }
static void cmdConvertData()      { csvPanelConvertData(); }
static void cmdGenerateMetadata() { csvPanelGenerateMetadata(); }
static void cmdSettings()         { csvPanelShowSettings(); }

static void cmdDocumentation() {
    // Same URL as the Windows plugin's Documentation menu item.
    gtk_show_uri(nullptr,
                 "https://github.com/BdR76/CSVLint/tree/master/"
                 "docs#csv-lint-plug-in-documentation",
                 GDK_CURRENT_TIME);
}

static void cmdAbout() {
    if (csvDlgAbout("CSV Lint v" PLUGIN_VERSION,
                    "CSV Lint plug-in — check syntax, validate data, reformat and "
                    "convert CSV / fixed-width text files.\n\n"
                    "Original Windows plugin by Bas de Reuver (BdR76).\n"
                    "Linux port for Nextpad++.\n\n"
                    "License: GPL-3.0"))
        gtk_show_uri(nullptr, "https://github.com/BdR76/CSVLint", GDK_CURRENT_TIME);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Plugin contract
// ═══════════════════════════════════════════════════════════════════════════

extern "C" NPP_EXPORT void setInfo(LinuxHostNppData data) {
    // Swap the host's flat NppData for the macOS-shaped one (sentinel
    // handles + routing cpSendMessage) — see LinuxViewBridge.cpp.
    cpBridgeInit(&data);
    nppData._nppHandle             = kHandleNpp;
    nppData._scintillaMainHandle   = kHandleScintillaMain;
    nppData._scintillaSecondHandle = kHandleScintillaSub;
    nppData._sendMessage           = cpSendMessage;

    csvPanelInit(&nppData);
    csvSettingsIOInit(&nppData);
    csvSettingsLoad();

    int idx = 0;
    auto addItem = [&](const char *name, PFUNCPLUGINCMD func) {
        strlcpy(funcItem[idx]._itemName, name, NPP_MENU_ITEM_SIZE);
        funcItem[idx]._pFunc = func;
        funcItem[idx]._init2Check = false;
        idx++;
    };
    // macOS uses an EMPTY name for separators; this host SKIPS empty-named
    // items and renders a FuncItem named exactly "-" as a divider.
    auto addSep = [&]() { addItem("-", nullptr); };

    // 1:1 with the Windows plugin's menu (Main.cs SetCommand table).
    addItem("CSV Lint window",     cmdLintWindow);        // 0
    addSep();                                             // 1
    addItem("Analyse data report", cmdAnalyseReport);     // 2
    addItem("Select columns",      cmdSelectColumns);     // 3
    addSep();                                             // 4
    addItem("Convert data",        cmdConvertData);       // 5
    addItem("Generate metadata",   cmdGenerateMetadata);  // 6
    addSep();                                             // 7
    addItem("Settings",            cmdSettings);          // 8
    addItem("Documentation",       cmdDocumentation);     // 9
    addItem("About",               cmdAbout);             // 10
}

extern "C" NPP_EXPORT const char *getName() {
    return PLUGIN_NAME;
}

extern "C" NPP_EXPORT FuncItem *getFuncsArray(int *nbF) {
    *nbF = NB_FUNC;
    return funcItem;
}

extern "C" NPP_EXPORT void beNotified(SCNotification *n) {
    switch (n->nmhdr.code) {
        case NPPN_READY:
        case NPPN_BUFFERACTIVATED:
            csvPanelOnBufferActivated();
            break;

        case NPPN_FILECLOSED:
            csvPanelOnFileClosed((uintptr_t)n->nmhdr.idFrom);
            break;

        case SCN_MODIFIED:
            if (n->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))
                csvPanelOnModified();
            break;

        case NPPN_SHUTDOWN:
            csvSettingsSave();   // persist dialog-remembered values
            csvPanelShutdown();
            if (g_panelHandle > 0) {
                npp(NPPM_DMM_UNREGISTERPANEL, (uintptr_t)g_panelHandle, 0);
                g_panelHandle = 0;
            }
            sPanelView = nullptr;
            break;
        default:
            break;
    }
}

extern "C" NPP_EXPORT intptr_t messageProc(uint32_t, uintptr_t, intptr_t) {
    return 1;
}

// REQUIRED by this host's loader (Windows parity) — a plugin without it is
// silently skipped at scan time. macOS does not query it.
extern "C" NPP_EXPORT int isUnicode() {
    return 1;
}
