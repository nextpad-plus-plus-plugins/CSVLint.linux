// CsvLintPanel.cpp — docked CSV Lint window + per-file definitions + column
// coloring. Linux (GTK4) port of CsvLintPanel.mm; UI mirrors
// Forms/CsvLintWindow.cs: toolbar (Detect columns, auto-matic, Apply, Colors,
// Sort, Add column, Reformat, Validate data, gear), editable metadata box
// (ini lines), output box (double-click jumps to the error line/value in the
// editor).

#include "CsvLintPanel.h"
#include "Scintilla.h"
#include "engine/CsvDefinition.h"
#include "engine/CsvAnalyze.h"
#include "engine/CsvValidate.h"
#include "engine/CsvSchemaIni.h"
#include "engine/CsvEdit.h"
#include "engine/CsvConvert.h"
#include "engine/CsvGenerateCode.h"
#include "CsvLintDialogs.h"
#include "CsvSettingsIO.h"
#include <functional>
#include <map>
#include <string>
#include <chrono>
#include <cctype>
#include <ctime>

// ── state ───────────────────────────────────────────────────────────────────

static NppData *gNpp = nullptr;

static GtkWidget *sView      = nullptr;
static GtkWidget *sMetaText  = nullptr;   // GtkTextView (editable ini lines)
static GtkWidget *sOutText   = nullptr;   // GtkTextView (read-only output)
static GtkWidget *sApplyBtn  = nullptr;
static GtkWidget *sAutoChk   = nullptr;   // GtkCheckButton "auto-matic"
static GtkWidget *sColorsBtn = nullptr;
static guint      sRecolorPending = 0;    // g_timeout source; 0 = none
static bool       sSettingMetaText = false;  // programmatic set_text guard
// (macOS NSTextDidChangeNotification fires only for USER edits; GTK's
// "changed" fires for programmatic set_text too, so guard those.)

static std::map<std::string, CsvDefinition> sFileCsvDef;   // Main.FileCsvDef
static std::map<std::string, bool>          sFileColorsOff; // "Aa" toggle per file

// 12-color presets from upstream extra/CSVLint_12_colors.xml (RRGGBB).
static const int kLightColors[12] = {
    0xC0EFFF, 0xFFFFC0, 0xFFE0FF, 0xA0FFA0, 0xFFC0E0, 0xA0FFFF,
    0xFFE0C0, 0xD0D0FF, 0xCFFFA0, 0xFFACFF, 0x80FFBF, 0xFFC0C0,
};
static const int kDarkColors[12] = {   // dark-neon fgColors as box tints
    0x80BFFF, 0xFFFF80, 0xFFB0FF, 0x80FF80, 0xFF80BF, 0x80FFFF,
    0xFFBF80, 0xC0C0FF, 0xBFFF80, 0xFF80FF, 0x80FFBF, 0xFF8080,
};
static const int kIndicatorBase = 20;   // indicators 20..31

// ── host helpers ────────────────────────────────────────────────────────────

static intptr_t npp(uint32_t msg, uintptr_t w = 0, intptr_t l = 0) {
    return gNpp->_sendMessage(gNpp->_nppHandle, msg, w, l);
}
static NppHandle curSci() {
    int which = -1;
    npp(NPPM_GETCURRENTSCINTILLA, 0, (intptr_t)&which);
    return which == 0 ? gNpp->_scintillaMainHandle : gNpp->_scintillaSecondHandle;
}
static intptr_t sci(uint32_t msg, uintptr_t w = 0, intptr_t l = 0) {
    return gNpp->_sendMessage(curSci(), msg, w, l);
}

static std::string currentFilePath() {
    char buf[4096] = {0};
    npp(NPPM_GETFULLCURRENTPATH, sizeof(buf) - 1, (intptr_t)buf);
    return buf;
}

static std::string currentExtLower() {
    // Note: this host's NPPM_GETEXTPART returns the extension WITH the
    // leading dot (Windows parity; macOS returns it without) — normalize to
    // dot-less lowercase.
    char buf[256] = {0};
    npp(NPPM_GETEXTPART, sizeof(buf) - 1, (intptr_t)buf);
    std::string e = buf;
    if (!e.empty() && e[0] == '.') e.erase(0, 1);
    for (auto &c : e) c = (char)tolower((unsigned char)c);
    return e;
}

static std::string documentText() {
    intptr_t len = sci(SCI_GETLENGTH);
    if (len <= 0) return "";
    std::string text((size_t)len, '\0');
    Sci_TextRangeFull tr = {{0, (Sci_Position)len}, &text[0]};
    sci(SCI_GETTEXTRANGEFULL, 0, (intptr_t)&tr);
    return text;
}

static std::string editorEOL() {
    switch (sci(SCI_GETEOLMODE)) {
        case SC_EOL_CR: return "\r";
        case SC_EOL_LF: return "\n";
        default:        return "\r\n";
    }
}

static void setDocumentText(const std::string &text) {
    sci(SCI_SETTEXT, 0, (intptr_t)text.c_str());
}

static std::string elapsedSince(std::chrono::steady_clock::time_point t0) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    char buf[32];
    snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld.%03lld",
             (long long)(ms / 3600000), (long long)((ms / 60000) % 60),
             (long long)((ms / 1000) % 60), (long long)(ms % 1000));
    return buf;
}

static void setOutput(const std::string &s) {
    if (!sOutText) return;
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(sOutText));
    gtk_text_buffer_set_text(b, s.c_str(), -1);
}

// ── column coloring (indicators — replaces the Windows external lexer) ──────

static int rgbToSci(int rrggbb) {   // Scintilla colours are 0xBBGGRR
    return ((rrggbb & 0xFF) << 16) | (rrggbb & 0xFF00) | ((rrggbb >> 16) & 0xFF);
}

static void setupIndicators() {
    bool dark = npp(NPPM_ISDARKMODEENABLED) != 0;
    const int *palette = dark ? kDarkColors : kLightColors;
    for (int i = 0; i < 12; i++) {
        int ind = kIndicatorBase + i;
        sci(SCI_INDICSETSTYLE, ind, INDIC_FULLBOX);
        sci(SCI_INDICSETFORE, ind, rgbToSci(palette[i]));
        sci(SCI_INDICSETALPHA, ind, dark ? 70 : 130);
        sci(SCI_INDICSETOUTLINEALPHA, ind, 0);
        sci(SCI_INDICSETUNDER, ind, 1);
    }
}

static void clearColors() {
    intptr_t len = sci(SCI_GETLENGTH);
    for (int i = 0; i < 12; i++) {
        sci(SCI_SETINDICATORCURRENT, kIndicatorBase + i);
        sci(SCI_INDICATORCLEARRANGE, 0, len);
    }
}

/// Offset-aware value scanner for coloring: same quote/separator rules as the
/// parser, but tracking byte ranges instead of extracting values.
static void applyColumnColors() {
    std::string path = currentFilePath();
    auto it = sFileCsvDef.find(path);
    if (it == sFileCsvDef.end()) return;
    CsvDefinition &def = it->second;

    intptr_t len = sci(SCI_GETLENGTH);
    clearColors();

    if (sFileColorsOff[path]) return;
    if (def.Fields.empty()) return;
    if (def.Fields.size() == 1 && def.Fields[0].MaxWidth >= 9999) return;   // not tabular
    if (len > csvSettings().AutoSyntaxLimit) return;

    std::string text = documentText();
    const char quoteChar = csvSettings().DefaultQuoteChar;
    const bool sepColor = csvSettings().SeparatorColor;
    size_t p = 0;
    int skip = def.SkipLines;

    auto fill = [&](size_t start, size_t end, int col) {
        if (end <= start) return;
        sci(SCI_SETINDICATORCURRENT, kIndicatorBase + (col % 12));
        sci(SCI_INDICATORFILLRANGE, start, end - start);
    };

    while (p < text.size()) {
        // skip-lines and comment lines stay uncolored
        bool lineSkipped = false;
        if (skip > 0) {
            skip--;
            lineSkipped = true;
        } else if (def.CommentChar != '\0' && text[p] == def.CommentChar) {
            lineSkipped = true;
        }
        if (lineSkipped) {
            while (p < text.size() && text[p] != '\n') p++;
            if (p < text.size()) p++;
            continue;
        }

        if (def.Separator == '\0') {
            // fixed width: cumulative widths
            size_t lineStart = p;
            size_t lineEnd = p;
            while (lineEnd < text.size() && text[lineEnd] != '\n' && text[lineEnd] != '\r')
                lineEnd++;
            size_t colStart = lineStart;
            for (size_t c = 0; c < def.FieldWidths.size() && colStart < lineEnd; c++) {
                size_t colEnd = colStart + (size_t)def.FieldWidths[c];
                if (colEnd > lineEnd) colEnd = lineEnd;
                fill(colStart, colEnd, (int)c);
                colStart = colEnd;
            }
            // overflow tail = one extra column
            if (colStart < lineEnd) fill(colStart, lineEnd, (int)def.FieldWidths.size());
            p = lineEnd;
            if (p < text.size() && text[p] == '\r') p++;
            if (p < text.size() && text[p] == '\n') p++;
        } else {
            // delimited: walk one line (quoted values may span lines)
            int col = 0;
            size_t valStart = p;
            bool inQuote = false;
            bool atValueStart = true;
            while (p < text.size()) {
                char ch = text[p];
                if (inQuote) {
                    if (ch == quoteChar) {
                        if (p + 1 < text.size() && text[p + 1] == quoteChar) p++;   // escaped ""
                        else inQuote = false;
                    }
                } else if (ch == quoteChar && atValueStart) {
                    inQuote = true;
                    atValueStart = false;
                } else if (ch == def.Separator) {
                    // SeparatorColor: extend the box through the separator char
                    fill(valStart, sepColor ? p + 1 : p, col);
                    col++;
                    valStart = p + 1;
                    atValueStart = true;
                } else if (ch == '\n' || ch == '\r') {
                    break;
                } else if (ch != ' ') {
                    atValueStart = false;
                }
                p++;
            }
            fill(valStart, p, col);
            if (p < text.size() && text[p] == '\r') p++;
            if (p < text.size() && text[p] == '\n') p++;
        }
    }
}

static void scheduleRecolor() {
    if (sRecolorPending) {
        g_source_remove(sRecolorPending);
        sRecolorPending = 0;
    }
    sRecolorPending = g_timeout_add(400, +[](gpointer) -> gboolean {
        sRecolorPending = 0;
        setupIndicators();
        applyColumnColors();
        return G_SOURCE_REMOVE;
    }, nullptr);
}

// ── panel content updates ───────────────────────────────────────────────────

static std::string metaText() {
    if (!sMetaText) return "";
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(sMetaText));
    GtkTextIter a;
    GtkTextIter z;
    gtk_text_buffer_get_bounds(b, &a, &z);
    gchar *t = gtk_text_buffer_get_text(b, &a, &z, FALSE);
    std::string s = t ? t : "";
    g_free(t);
    return s;
}

/// SetCsvDefinition: show the definition (with the "unable to detect" banner)
/// in the metadata box; enable/disable Apply.
static void setCsvDefinitionText(CsvDefinition &csvdef, bool applybtn) {
    std::string msg;
    if (csvdef.ScanState == CsvScanState::TooBig) {
        msg += "; *********************************\r\n";
        msg += "; File is too large for CsvLint to analyze\r\n";
        msg += "; *********************************\r\n";
    } else if (csvdef.Fields.size() == 1 &&
               csvdef.Fields[0].DataType == ColumnType::String &&
               csvdef.Fields[0].MaxWidth >= 9999) {
        msg += "; *********************************\r\n";
        msg += "; Unable to detect column separator\r\n";
        msg += "; *********************************\r\n";
    }
    std::string text = msg + csvdef.GetIniLines();
    if (sMetaText) {
        GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(sMetaText));
        sSettingMetaText = true;
        gtk_text_buffer_set_text(b, text.c_str(), -1);
        sSettingMetaText = false;
    }
    if (sApplyBtn) gtk_widget_set_sensitive(sApplyBtn, applybtn);
}

/// Main.UpdateCSVChanges: store per-file definition, recolor, optionally save
/// the schema.ini section.
static void updateCSVChanges(CsvDefinition &csvdef, bool saveini) {
    std::string path = currentFilePath();
    sFileCsvDef[path] = csvdef;

    setupIndicators();
    applyColumnColors();

    if (saveini) {
        std::string errmsg;
        CsvDefinition stored = csvdef;   // GetIniLines is non-const (NumberDigits recompute)
        if (!CsvSchemaIni::WriteIniSection(path, stored.GetIniLines(), errmsg))
            csvAlert("Error saving schema.ini", errmsg);
    }
}

/// Main.CheckValidCsvDef
static bool checkValidCsvDef(const CsvDefinition &csvdef, const char *action) {
    if (csvdef.Fields.empty() ||
        (csvdef.Fields[0].DataType == ColumnType::String && csvdef.Fields[0].MaxWidth >= 9999)) {
        std::string msg = std::string("Cannot ") + action +
            " without valid csv metadata.\nOpen the CSV Lint window, "
            "press [Detect columns] and try again.";
        csvAlert("Missing csv metadata", msg);
        return false;
    }
    return true;
}

static CsvDefinition definitionFromMetaText(std::string *outError) {
    return CsvDefinition::FromIniLines(metaText(), outError);
}

// ── toolbar actions ─────────────────────────────────────────────────────────

static void detectColumnsNow(bool userRequested) {
    bool autodetect = gtk_check_button_get_active(GTK_CHECK_BUTTON(sAutoChk));
    char sep = '\0';
    std::string widths;
    bool header = false;
    int skip = 0;
    char comm = csvSettings().CommentCharacter;

    // manual override auto-detect (DetectColumnsForm)
    if (!autodetect) {
        if (!csvDlgDetectColumns(sep, widths, header, skip, comm)) return;
    }

    setOutput("");
    auto t0 = std::chrono::steady_clock::now();

    std::string text = documentText();
    CsvDefinition csvdef = CsvAnalyze::InferFromData(
        text, autodetect, sep, widths, header, skip, comm,
        userRequested, /*isCsv*/true);

    updateCSVChanges(csvdef, false);
    setCsvDefinitionText(csvdef, true);
    setOutput("Detecting columns from data is ready, time elapsed " + elapsedSince(t0));
}

static void onDetectColumns() { detectColumnsNow(true); }

static void onApplyMeta() {
    std::string err;
    CsvDefinition csvdef = definitionFromMetaText(&err);
    if (!err.empty()) {
        csvAlert("Error in schema.ini", err);
        return;
    }
    if (!csvdef.Fields.empty()) {
        updateCSVChanges(csvdef, true);
        setCsvDefinitionText(csvdef, false);
    }
}

static void onToggleColors() {
    std::string path = currentFilePath();
    sFileColorsOff[path] = !sFileColorsOff[path];
    setupIndicators();
    applyColumnColors();
}

static void onValidateData() {
    setOutput("");
    std::string err;
    CsvDefinition csvdef = definitionFromMetaText(&err);
    if (!err.empty() || !checkValidCsvDef(csvdef, "validate data")) return;

    auto t0 = std::chrono::steady_clock::now();
    std::string text = documentText();
    TextReader rd(text);
    CsvValidate csvval;
    csvval.ValidateData(rd, csvdef, elapsedSince(t0));
    setOutput(csvval.Report());
}

static void onAutoDetectToggled() {
    csvSettings().AutoDetectColumns = gtk_check_button_get_active(GTK_CHECK_BUTTON(sAutoChk));
    csvSettingsSave();
}

static void onSortClicked() {
    std::string err;
    CsvDefinition csvdef = definitionFromMetaText(&err);
    if (!err.empty() || !checkValidCsvDef(csvdef, "sort data")) return;

    int idx = 0;
    bool asc = true;
    bool val = true;
    if (!csvDlgSort(csvdef, idx, asc, val)) return;

    setOutput("");
    auto t0 = std::chrono::steady_clock::now();
    std::string text = documentText();
    std::string sortErr;
    std::string outText = CsvEdit::SortData(text, csvdef, idx, asc, val, editorEOL(), &sortErr);
    if (!sortErr.empty()) {
        csvAlert("Sort on column error", sortErr);
        return;
    }
    setDocumentText(outText);
    std::string colname = csvdef.Fields[idx].Name;
    setOutput("Sort data " + std::string(asc ? "a" : "de") + "scending on column '" +
              colname + "' is ready, time elapsed " + elapsedSince(t0) + "\r\n");
    setupIndicators();
    applyColumnColors();
}

static void onAddColumnClicked() {
    std::string err;
    CsvDefinition csvdef = definitionFromMetaText(&err);
    if (!err.empty() || !checkValidCsvDef(csvdef, "add new column")) return;

    int cod = 0;
    int idx = 0;
    std::string par1;
    std::string par2;
    bool rem = false;
    if (!csvDlgColumnSplit(csvdef, cod, idx, par1, par2, rem)) return;

    setOutput("");
    auto t0 = std::chrono::steady_clock::now();
    std::string text = documentText();
    CsvDefinition csvnew;
    std::string outText = CsvEdit::ColumnSplit(text, csvdef, idx, cod, par1, par2, rem,
                                               editorEOL(), csvnew);
    setDocumentText(outText);

    std::string msg;
    if (cod == 1) msg = "was padded with \"" + par1 + "\"";
    if (cod == 2) msg = "search \"" + par1 + "\" replace with \"" + par2 + "\"";
    if (cod == 3) msg = "was split on valid and invalid values";
    if (cod == 4) msg = "was split on character " + par1;
    if (cod == 5) msg = "was split on position " + par1;
    if (rem)      msg = msg + (msg.empty() ? "" : "and original column ") + "was removed";
    msg = "Column \"" + csvdef.Fields[idx].Name + "\" " + msg + "\r\n";
    msg += "Add new column is ready, time elapsed " + elapsedSince(t0) + "\r\n";
    setOutput(msg);

    // refresh definition from the changed data (Windows re-runs detect)
    detectColumnsNow(true);
    setOutput(msg);
}

static void onReformatClicked() {
    setOutput("");
    std::string err;
    CsvDefinition csvdef = definitionFromMetaText(&err);
    if (!err.empty() || !checkValidCsvDef(csvdef, "reformat data")) return;

    std::string newSep;
    std::string newDt;
    std::string newDec;
    std::string replCrLf;
    bool updSep = false;
    bool alignVert = false;
    if (!csvDlgReformat(csvdef, newSep, updSep, newDt, newDec, replCrLf, alignVert)) return;

    auto t0 = std::chrono::steady_clock::now();
    std::string text = documentText();
    std::string outText = CsvEdit::ReformatDataFile(text, csvdef, newSep, updSep, newDt,
                                                    newDec, replCrLf, alignVert, editorEOL());
    setDocumentText(outText);

    const CsvSettings &st = csvSettings();
    std::string msg;
    if (updSep) {
        std::string oldsep = csvdef.Separator == '\0' ? "{Fixed width}"
                             : (csvdef.Separator == '\t' ? "{Tab}"
                                                          : std::string(1, csvdef.Separator));
        std::string newsepTxt = (newSep.empty() || newSep[0] == '\0') ? "{Fixed width}"
                                : (newSep == "\t" ? "{Tab}" : newSep);
        if (!newSep.empty() && newSep[0] == '\0') csvdef.ColNameHeader = false;
        msg += "Reformat column separator from " + oldsep + " to " + newsepTxt + "\r\n";
        csvdef.Separator = newSep.empty() ? '\0' : newSep[0];
    }
    if (!newDt.empty()) {
        msg += "Reformat datetime format from \"" + csvdef.DateTimeFormat + "\" to \"" +
               newDt + "\"\r\n";
        csvdef.DateTimeFormat = newDt;
        for (auto &col : csvdef.Fields)
            if (col.DataType == ColumnType::DateTime) col.UpdateDateTimeMask(newDt);
    }
    if (!newDec.empty()) {
        msg += "Reformat decimal separator from " + std::string(1, csvdef.DecimalSymbol) +
               " to " + newDec + "\r\n";
        csvdef.DecimalSymbol = newDec[0];
    }
    if (st.TrimValues || st.ReformatQuotes > 0) {
        msg += "General settings: ";
        if (st.TrimValues) msg += "Trim all values";
        if (st.ReformatQuotes > 0) {
            std::string quotetxt = "None / Minimal";
            if (st.ReformatQuotes == 1) quotetxt = "Values with spaces";
            if (st.ReformatQuotes == 2) quotetxt = "All string values";
            if (st.ReformatQuotes == 3) quotetxt = "All non-numeric values";
            if (st.ReformatQuotes == 4) quotetxt = "All values";
            if (st.TrimValues) msg += ", ";
            msg += "apply quotes: " + quotetxt;
        }
        msg += "\r\n";
    }
    msg += "Reformat data is ready, time elapsed " + elapsedSince(t0) + "\r\n";
    setOutput(msg);

    updateCSVChanges(csvdef, false);
    setCsvDefinitionText(csvdef, true);
}

// ── output box: double-click jumps to the error line/value ──────────────────

static void outputDoubleClicked(double wx, double wy) {
    GtkTextView *tv = GTK_TEXT_VIEW(sOutText);
    gint bx = 0;
    gint by = 0;
    gtk_text_view_window_to_buffer_coords(tv, GTK_TEXT_WINDOW_WIDGET,
                                          (gint)wx, (gint)wy, &bx, &by);
    GtkTextIter it;
    gtk_text_view_get_iter_at_location(tv, &it, bx, by);

    // the clicked line's text
    GtkTextIter a = it;
    GtkTextIter z = it;
    gtk_text_iter_set_line_offset(&a, 0);
    if (!gtk_text_iter_ends_line(&z)) gtk_text_iter_forward_to_line_end(&z);
    GtkTextBuffer *buf = gtk_text_view_get_buffer(tv);
    gchar *lt = gtk_text_buffer_get_text(buf, &a, &z, FALSE);
    std::string lineText = lt ? lt : "";
    g_free(lt);

    // log line format: "** error line 123: Column 4 value "x" ..."
    size_t startR = lineText.find("** error line");
    size_t colonR = lineText.find(':');
    if (startR == std::string::npos || colonR == std::string::npos || startR >= colonR)
        return;

    int linenumber = atoi(lineText.substr(startR + 13, colonR - startR - 13).c_str());
    if (linenumber <= 0) return;

    std::string errval;
    size_t valR = lineText.find(" value \"");
    if (valR != std::string::npos) {
        size_t vpos = valR + 8;
        size_t endR = lineText.find('"', vpos);
        if (endR != std::string::npos)
            errval = lineText.substr(vpos, endR - vpos);
    }

    sci(SCI_GOTOLINE, linenumber - 1);
    if (!errval.empty()) {
        intptr_t cur = sci(SCI_GETCURRENTPOS);
        intptr_t anc = sci(SCI_GETANCHOR);
        sci(SCI_SETTARGETSTART, (uintptr_t)(cur > anc ? cur : anc));
        sci(SCI_SETTARGETEND, (uintptr_t)sci(SCI_GETLINEENDPOSITION, linenumber - 1));
        sci(SCI_SETSEARCHFLAGS, 0);
        intptr_t selpos = sci(SCI_SEARCHINTARGET, errval.size(), (intptr_t)errval.c_str());
        if (selpos != -1) sci(SCI_SETSELECTION, selpos, selpos + (intptr_t)errval.size());
    }
    // give focus back to the editor so the selection is visible
    sci(SCI_GRABFOCUS);
}

// ── view construction ───────────────────────────────────────────────────────

static GtkWidget *makeButton(const char *title, void (*fn)()) {
    GtkWidget *b = gtk_button_new_with_label(title);
    g_signal_connect_swapped(b, "clicked", G_CALLBACK(fn), nullptr);
    return b;
}

static GtkWidget *makeTextArea(GtkWidget *parent, bool editable, GtkWidget **outScroll) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), editable);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv), editable);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 4);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), tv);
    gtk_box_append(GTK_BOX(parent), scroll);
    *outScroll = scroll;
    return tv;
}

void csvPanelInit(NppData *data) {
    gNpp = data;
}

GtkWidget *csvPanelView(void) {
    if (sView) return sView;

    sView = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(sView, "npp-panel-content");
    gtk_widget_set_margin_top(sView, 6);
    gtk_widget_set_margin_bottom(sView, 4);
    gtk_widget_set_margin_start(sView, 8);
    gtk_widget_set_margin_end(sView, 8);

    // toolbar
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(sView), bar);

    gtk_box_append(GTK_BOX(bar), makeButton("Detect columns", onDetectColumns));

    sAutoChk = gtk_check_button_new_with_label("auto-matic");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(sAutoChk),
                                csvSettings().AutoDetectColumns);
    g_signal_connect_swapped(sAutoChk, "toggled", G_CALLBACK(onAutoDetectToggled), nullptr);
    gtk_box_append(GTK_BOX(bar), sAutoChk);

    sApplyBtn = makeButton("Apply", onApplyMeta);
    gtk_widget_set_sensitive(sApplyBtn, FALSE);
    gtk_box_append(GTK_BOX(bar), sApplyBtn);

    sColorsBtn = makeButton("Colors", onToggleColors);
    gtk_widget_set_margin_start(sColorsBtn, 8);
    gtk_box_append(GTK_BOX(bar), sColorsBtn);
    gtk_box_append(GTK_BOX(bar), makeButton("Sort", onSortClicked));
    gtk_box_append(GTK_BOX(bar), makeButton("Add column", onAddColumnClicked));
    gtk_box_append(GTK_BOX(bar), makeButton("Reformat", onReformatClicked));
    GtkWidget *validate = makeButton("Validate data", onValidateData);
    gtk_widget_set_margin_start(validate, 8);
    gtk_box_append(GTK_BOX(bar), validate);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(bar), spacer);

    GtkWidget *gear = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_widget_set_tooltip_text(gear, "Settings");
    g_signal_connect_swapped(gear, "clicked", G_CALLBACK(+[]() {
        csvPanelShowSettings();
    }), nullptr);
    gtk_box_append(GTK_BOX(bar), gear);

    // text areas side by side (equal halves, like the macOS width constraint)
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_set_homogeneous(GTK_BOX(content), TRUE);
    gtk_widget_set_vexpand(content, TRUE);
    gtk_box_append(GTK_BOX(sView), content);

    GtkWidget *metaScroll = nullptr;
    GtkWidget *outScroll = nullptr;
    sMetaText = makeTextArea(content, true, &metaScroll);
    sOutText  = makeTextArea(content, false, &outScroll);

    // Apply enabled once the metadata text is edited (txtSchemaIni_KeyDown)
    GtkTextBuffer *mb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(sMetaText));
    g_signal_connect(mb, "changed", G_CALLBACK(+[](GtkTextBuffer *, gpointer) {
        if (!sSettingMetaText && sApplyBtn) gtk_widget_set_sensitive(sApplyBtn, TRUE);
    }), nullptr);

    // double-click in the output box jumps to the error line/value
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed",
        G_CALLBACK(+[](GtkGestureClick *, gint n_press, gdouble x, gdouble y, gpointer) {
            if (n_press == 2) outputDoubleClicked(x, y);
        }), nullptr);
    gtk_widget_add_controller(sOutText, GTK_EVENT_CONTROLLER(click));

    return sView;
}

// ── notifications ───────────────────────────────────────────────────────────

void csvPanelOnBufferActivated(void) {
    if (!gNpp) return;
    std::string filename = currentFilePath();
    if (filename.empty()) return;

    bool is_csv = currentExtLower() == "csv";

    auto it = sFileCsvDef.find(filename);
    if (it == sFileCsvDef.end()) {
        CsvDefinition csvdef;
        std::string inilines = CsvSchemaIni::ReadIniSectionLines(filename);
        if (!inilines.empty()) {
            // metadata from previously saved schema.ini
            std::string err;
            csvdef = CsvDefinition::FromIniLines(inilines, &err);
            csvdef.ScanState = CsvScanState::LoadIni;
        } else {
            std::string text = documentText();
            csvdef = CsvAnalyze::InferFromData(text, true, '\0', "", false, 0,
                                               csvSettings().CommentCharacter,
                                               /*userRequested*/false, is_csv);
        }
        it = sFileCsvDef.emplace(filename, csvdef).first;
    }

    setupIndicators();
    applyColumnColors();

    if (sView && gtk_widget_get_mapped(sView)) {
        setCsvDefinitionText(it->second, false);
        setOutput("");
    }
}

void csvPanelOnFileClosed(uintptr_t bufferID) {
    char buf[4096] = {0};
    if (npp(NPPM_GETFULLPATHFROMBUFFERID, bufferID, (intptr_t)buf) > 0 && buf[0]) {
        sFileCsvDef.erase(buf);
        sFileColorsOff.erase(buf);
    }
}

void csvPanelOnModified(void) {
    std::string path = currentFilePath();
    auto it = sFileCsvDef.find(path);
    if (it == sFileCsvDef.end() || it->second.Fields.empty()) return;
    if (sFileColorsOff[path]) return;
    scheduleRecolor();
}

/// The host dispatches NPPM_MENUCOMMAND asynchronously — request the new
/// file, then run `fn` only once the current buffer actually changed.
struct NewFileCtx {
    intptr_t oldBuf;
    int attempts = 0;
    std::function<void()> fn;
};

static void newFileThen(std::function<void()> fn) {
    NewFileCtx *ctx = new NewFileCtx;
    ctx->oldBuf = npp(NPPM_GETCURRENTBUFFERID);
    ctx->fn = std::move(fn);
    npp(NPPM_MENUCOMMAND, 0, 41001);   // IDM_FILE_NEW
    g_timeout_add(50, +[](gpointer p) -> gboolean {
        NewFileCtx *c = (NewFileCtx *)p;
        if (npp(NPPM_GETCURRENTBUFFERID) != c->oldBuf || ++c->attempts > 20) {
            c->fn();
            delete c;
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }, ctx);
}

void csvPanelAnalyseReport(void) {
    std::string path = currentFilePath();
    auto it = sFileCsvDef.find(path);
    if (it == sFileCsvDef.end()) return;
    CsvDefinition csvdef = it->second;
    if (!checkValidCsvDef(csvdef, "analyse data")) return;

    std::string text = documentText();

    // ScriptInfo comment lines
    std::vector<std::string> comment;
    size_t slash = path.rfind('/');
    comment.push_back("Nextpad++ CSV Lint plug-in");
    comment.push_back("File: " + (slash == std::string::npos ? path : path.substr(slash + 1)));

    std::string report = CsvAnalyze::StatisticalReport(text, csvdef, comment);

    newFileThen([report]() {
        setDocumentText(report);
    });
}

void csvPanelSelectColumns(void) {
    std::string path = currentFilePath();
    auto it = sFileCsvDef.find(path);
    if (it == sFileCsvDef.end()) return;
    CsvDefinition csvdef = it->second;
    if (!checkValidCsvDef(csvdef, "select columns")) return;

    std::vector<int> selIdx;
    if (!csvDlgSelectColumns(csvdef, selIdx)) return;

    std::string text = documentText();
    CsvDefinition csvnew;
    std::string outText = CsvEdit::SelectColumns(text, csvdef, selIdx, editorEOL(), csvnew);

    newFileThen([outText, csvnew]() mutable {
        setDocumentText(outText);
        updateCSVChanges(csvnew, false);   // keyed to the NEW buffer's path now
        if (sView && gtk_widget_get_mapped(sView)) setCsvDefinitionText(csvnew, false);
    });
}

void csvPanelConvertData(void) {
    std::string path = currentFilePath();
    auto it = sFileCsvDef.find(path);
    if (it == sFileCsvDef.end()) return;
    CsvDefinition csvdef = it->second;
    if (!checkValidCsvDef(csvdef, "convert data")) return;

    CsvSettings &st = csvSettings();
    int type = st.DataConvertType;
    std::string tableName = st.DataConvertName;
    int dialect = st.DataConvertSQL;
    int batch = st.DataConvertBatch;
    if (!csvDlgDataConvert(type, tableName, dialect, batch)) return;
    st.DataConvertType = type;
    st.DataConvertName = tableName;
    st.DataConvertSQL = dialect;
    st.DataConvertBatch = batch;

    CsvConvertOptions opt;
    opt.sqlDialect = dialect;
    opt.batchSize = batch;
    opt.tableName = tableName;
    size_t slash = path.rfind('/');
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = base.rfind('.');
    opt.fileNameNoExt = dot == std::string::npos ? base : base.substr(0, dot);
    opt.commentLines = {"Nextpad++ CSV Lint plug-in", "File: " + base};

    std::string text = documentText();
    std::string out;
    int lang = 0;
    switch (type) {
        case 1:  out = CsvConvert::ToXML(text, csvdef, opt);  lang = 9;  break;  // L_XML
        case 2:  out = CsvConvert::ToJSON(text, csvdef, opt); lang = 57; break;  // L_JSON
        default: out = CsvConvert::ToSQL(text, csvdef, opt);  lang = 17; break;  // L_SQL
    }

    bool applyLang = (long)out.size() < csvSettings().AutoSyntaxLimit;
    newFileThen([out, lang, applyLang]() {
        setDocumentText(out);
        // NPPM_SETCURRENTLANGTYPE has no handler on this host — the
        // equivalent is SETBUFFERLANGTYPE with wParam 0 (current buffer).
        if (applyLang) npp(NPPM_SETBUFFERLANGTYPE, 0, lang);
    });
}

void csvPanelGenerateMetadata(void) {
    std::string path = currentFilePath();
    auto it = sFileCsvDef.find(path);
    if (it == sFileCsvDef.end()) return;
    CsvDefinition csvdef = it->second;
    if (!checkValidCsvDef(csvdef, "generate metadata")) return;

    CsvSettings &st = csvSettings();
    int type = st.MetadataType;
    if (!csvDlgGenerateMetadata(type)) return;
    st.MetadataType = type;

    CsvGenerateOptions opt;
    opt.filePath = path;
    size_t slash = path.rfind('/');
    opt.fileName = slash == std::string::npos ? path : path.substr(slash + 1);
    opt.fileDir = slash == std::string::npos ? "" : path.substr(0, slash);
    opt.commentLines = {"Nextpad++ CSV Lint plug-in", "File: " + opt.fileName};
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    opt.exampleYear = tmv.tm_year + 1900;

    std::string out;
    int lang = 0;
    switch (type) {
        case 1:  out = CsvGenerateCode::SchemaJSON(csvdef, opt);        lang = 57; break; // L_JSON
        case 2:  out = CsvGenerateCode::DatadictionaryCSV(csvdef, opt); lang = 0;  break; // plain (C# parity)
        case 3:  out = CsvGenerateCode::PythonPanda(csvdef, opt);       lang = 22; break; // L_PYTHON
        case 4:  out = CsvGenerateCode::RScript(csvdef, opt);           lang = 54; break; // L_R
        case 5:  out = CsvGenerateCode::PowerShell(csvdef, opt);        lang = 53; break; // L_POWERSHELL
        default: out = CsvGenerateCode::SchemaIni(csvdef, opt);         lang = 13; break; // L_INI
    }

    bool applyLang = lang != 0 && (long)out.size() < csvSettings().AutoSyntaxLimit;
    newFileThen([out, lang, applyLang]() {
        setDocumentText(out);
        if (applyLang) npp(NPPM_SETBUFFERLANGTYPE, 0, lang);   // see ConvertData note
    });
}

void csvPanelShowSettings(void) {
    if (!csvDlgSettings()) return;
    csvSettingsSave();
    // Refresh: auto-matic checkbox + column colors (SeparatorColor /
    // AutoSyntaxLimit / DefaultQuoteChar may have changed).
    if (sAutoChk)
        gtk_check_button_set_active(GTK_CHECK_BUTTON(sAutoChk),
                                    csvSettings().AutoDetectColumns);
    setupIndicators();
    applyColumnColors();
}

void csvPanelShutdown(void) {
    if (sRecolorPending) {
        g_source_remove(sRecolorPending);
        sRecolorPending = 0;
    }
    // The widget tree is owned by the host's panel frame; just drop pointers.
    sView = nullptr;
    sMetaText = nullptr;
    sOutText = nullptr;
    sApplyBtn = nullptr;
    sAutoChk = nullptr;
    sColorsBtn = nullptr;
}
