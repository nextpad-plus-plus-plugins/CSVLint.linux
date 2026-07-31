// CsvLintDialogs.cpp — Linux (GTK4) port of CsvLintDialogs.mm. Same eight
// dialogs, same out-parameter contracts; NSApp runModalForWindow becomes the
// project-standard ModalDlg (hide-on-close + nested GMainLoop + destroy AFTER
// the last widget read — gtk_window_close destroys the tree by default and
// post-run reads would be a UAF).
#include "CsvLintDialogs.h"
#include "CsvSettingsIO.h"
#include <gtk/gtk.h>
#include <cstdlib>
#include <cstring>

// ── shared modal-dialog scaffolding ─────────────────────────────────────────
namespace {

struct ModalDlg {
    GtkWidget *win;
    GtkWidget *vbox;
    GMainLoop *loop = nullptr;
    int result = 0;

    explicit ModalDlg(const char *title) {
        win = gtk_window_new();
        gtk_window_set_title(GTK_WINDOW(win), title);
        gtk_window_set_modal(GTK_WINDOW(win), TRUE);
        gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
        gtk_window_set_hide_on_close(GTK_WINDOW(win), TRUE);
        g_signal_connect(win, "close-request", G_CALLBACK(+[](GtkWindow *, gpointer p) -> gboolean {
            ((ModalDlg *)p)->finish(0);
            return FALSE;   // hide-on-close hides; we destroy after read-back
        }), this);
        GtkEventController *k = gtk_event_controller_key_new();
        g_signal_connect(k, "key-pressed",
            G_CALLBACK(+[](GtkEventControllerKey *, guint kv, guint, GdkModifierType, gpointer p) -> gboolean {
                if (kv == GDK_KEY_Escape) { ((ModalDlg *)p)->finish(0); return TRUE; }
                return FALSE;
            }), this);
        gtk_widget_add_controller(win, k);

        vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_margin_top(vbox, 14);
        gtk_widget_set_margin_bottom(vbox, 12);
        gtk_widget_set_margin_start(vbox, 16);
        gtk_widget_set_margin_end(vbox, 16);
        gtk_window_set_child(GTK_WINDOW(win), vbox);
    }
    ~ModalDlg() { gtk_window_destroy(GTK_WINDOW(win)); }

    void finish(int r) { result = r; if (loop) g_main_loop_quit(loop); }

    int run() {
        loop = g_main_loop_new(nullptr, FALSE);
        gtk_window_present(GTK_WINDOW(win));
        g_main_loop_run(loop);
        g_main_loop_unref(loop);
        loop = nullptr;
        return result;
    }

    // OK/Cancel row; OK finishes with 1 (and is the default widget).
    void okCancel(const char *okLabel = "OK") {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_halign(row, GTK_ALIGN_END);
        gtk_widget_set_margin_top(row, 6);
        GtkWidget *ok = gtk_button_new_with_label(okLabel);
        gtk_widget_add_css_class(ok, "suggested-action");
        g_signal_connect(ok, "clicked", G_CALLBACK(+[](GtkButton *, gpointer p) {
            ((ModalDlg *)p)->finish(1);
        }), this);
        GtkWidget *cancel = gtk_button_new_with_label("Cancel");
        g_signal_connect(cancel, "clicked", G_CALLBACK(+[](GtkButton *, gpointer p) {
            ((ModalDlg *)p)->finish(0);
        }), this);
        gtk_box_append(GTK_BOX(row), ok);
        gtk_box_append(GTK_BOX(row), cancel);
        gtk_box_append(GTK_BOX(vbox), row);
        gtk_window_set_default_widget(GTK_WINDOW(win), ok);
    }
};

GtkWidget *mkLabel(const char *s) {
    GtkWidget *l = gtk_label_new(s);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}
GtkWidget *mkBold(const char *s) {
    GtkWidget *l = gtk_label_new(nullptr);
    gchar *m = g_markup_printf_escaped("<b>%s</b>", s);
    gtk_label_set_markup(GTK_LABEL(l), m);
    g_free(m);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}
GtkWidget *mkEntry(const std::string &text, int chars) {
    GtkWidget *e = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(e), text.c_str());
    gtk_editable_set_width_chars(GTK_EDITABLE(e), chars);
    return e;
}
GtkWidget *mkCheck(const char *s, bool on) {
    GtkWidget *c = gtk_check_button_new_with_label(s);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(c), on);
    return c;
}
GtkWidget *mkDropDown(const char *const *items, int selected) {
    GtkWidget *d = gtk_drop_down_new_from_strings(items);
    if (selected > 0) gtk_drop_down_set_selected(GTK_DROP_DOWN(d), (guint)selected);
    return d;
}
int ddSel(GtkWidget *d) { return (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(d)); }
std::string entryText(GtkWidget *e) {
    const char *t = gtk_editable_get_text(GTK_EDITABLE(e));
    return t ? t : "";
}
bool checkOn(GtkWidget *c) { return gtk_check_button_get_active(GTK_CHECK_BUTTON(c)); }

// label+control grid row (the WinForms label-column / control-column layout).
GtkWidget *gridRow(GtkWidget *grid, int row, const char *label, GtkWidget *ctrl,
                   const char *help = nullptr) {
    GtkWidget *l = mkLabel(label);
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ctrl, 1, row, 1, 1);
    if (help) {
        gtk_widget_set_tooltip_text(l, help);
        gtk_widget_set_tooltip_text(ctrl, help);
    }
    return ctrl;
}

GtkWidget *mkGrid() {
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 8);
    gtk_grid_set_column_spacing(GTK_GRID(g), 12);
    return g;
}

// Column-name dropdown source (columnTitles parity).
std::vector<std::string> columnTitles(const CsvDefinition &csvdef) {
    std::vector<std::string> cols;
    for (auto &f : csvdef.Fields) cols.push_back(f.Name.empty() ? "?" : f.Name);
    return cols;
}
GtkWidget *mkColumnDropDown(const CsvDefinition &csvdef) {
    std::vector<std::string> names = columnTitles(csvdef);
    std::vector<const char *> arr;
    for (auto &n : names) arr.push_back(n.c_str());
    arr.push_back(nullptr);
    return mkDropDown(arr.data(), 0);
}

} // namespace

// ── shared alert / confirm (NSAlert runModal parity) ────────────────────────

void csvAlert(const std::string &title, const std::string &message) {
    if (!gtk_is_initialized()) return;
    ModalDlg dlg(title.c_str());
    GtkWidget *l = mkLabel(message.c_str());
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 56);
    gtk_box_append(GTK_BOX(dlg.vbox), l);
    GtkWidget *ok = gtk_button_new_with_label("OK");
    gtk_widget_set_halign(ok, GTK_ALIGN_END);
    g_signal_connect(ok, "clicked", G_CALLBACK(+[](GtkButton *, gpointer p) {
        ((ModalDlg *)p)->finish(1);
    }), &dlg);
    gtk_box_append(GTK_BOX(dlg.vbox), ok);
    gtk_window_set_default_widget(GTK_WINDOW(dlg.win), ok);
    dlg.run();
}

bool csvConfirm(const std::string &title, const std::string &message) {
    if (!gtk_is_initialized()) return false;
    ModalDlg dlg(title.c_str());
    GtkWidget *l = mkLabel(message.c_str());
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 56);
    gtk_box_append(GTK_BOX(dlg.vbox), l);
    dlg.okCancel();
    return dlg.run() == 1;
}

bool csvDlgAbout(const std::string &title, const std::string &body) {
    if (!gtk_is_initialized()) return false;
    ModalDlg dlg(title.c_str());
    GtkWidget *l = mkLabel(body.c_str());
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 56);
    gtk_box_append(GTK_BOX(dlg.vbox), l);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(row, GTK_ALIGN_END);
    gtk_widget_set_margin_top(row, 6);
    GtkWidget *ok = gtk_button_new_with_label("OK");
    gtk_widget_add_css_class(ok, "suggested-action");
    g_signal_connect(ok, "clicked", G_CALLBACK(+[](GtkButton *, gpointer p) {
        ((ModalDlg *)p)->finish(1);
    }), &dlg);
    GtkWidget *gh = gtk_button_new_with_label("Visit GitHub");
    g_signal_connect(gh, "clicked", G_CALLBACK(+[](GtkButton *, gpointer p) {
        ((ModalDlg *)p)->finish(2);
    }), &dlg);
    gtk_box_append(GTK_BOX(row), ok);
    gtk_box_append(GTK_BOX(row), gh);
    gtk_box_append(GTK_BOX(dlg.vbox), row);
    gtk_window_set_default_widget(GTK_WINDOW(dlg.win), ok);
    return dlg.run() == 2;
}

// ── SortForm ────────────────────────────────────────────────────────────────

bool csvDlgSort(const CsvDefinition &csvdef, int &sortColumn, bool &sortAscending,
                bool &sortValue) {
    ModalDlg dlg("Sort data");
    GtkWidget *grid = mkGrid();
    gtk_box_append(GTK_BOX(dlg.vbox), grid);

    static const char *dirs[] = {"Ascending", "Descending", nullptr};
    static const char *vals[] = {"Sort on value", "Sort on length of value", nullptr};
    GtkWidget *colDd = gridRow(grid, 0, "Sort on column:", mkColumnDropDown(csvdef));
    GtkWidget *dirDd = gridRow(grid, 1, "Order:", mkDropDown(dirs, 0));
    GtkWidget *valDd = gridRow(grid, 2, "Sort by:", mkDropDown(vals, 0));

    dlg.okCancel();
    if (dlg.run() != 1) return false;
    sortColumn = ddSel(colDd);
    sortAscending = ddSel(dirDd) == 0;
    sortValue = ddSel(valDd) == 0;
    return true;
}

// ── ColumnSplitForm ("Add column") ──────────────────────────────────────────

bool csvDlgColumnSplit(const CsvDefinition &csvdef, int &splitCode, int &splitColumn,
                       std::string &param1, std::string &param2, bool &removeOrg) {
    ModalDlg dlg("Add new column");
    GtkWidget *grid = mkGrid();
    gtk_box_append(GTK_BOX(dlg.vbox), grid);

    static const char *ops[] = {
        "Pad with character (par.1=char, par.2=length)",
        "Search and replace (par.1=search, par.2=replace)",
        "Split valid and invalid values",
        "Split on character (par.1=char, par.2=Nth)",
        "Split on position (par.2=position)",
        nullptr,
    };
    GtkWidget *colDd = gridRow(grid, 0, "Column:", mkColumnDropDown(csvdef));
    GtkWidget *opDd  = gridRow(grid, 1, "Operation:", mkDropDown(ops, 0));
    GtkWidget *p1    = gridRow(grid, 2, "Parameter 1:", mkEntry("", 22));
    GtkWidget *p2    = gridRow(grid, 3, "Parameter 2:", mkEntry("", 22));
    GtkWidget *rem   = mkCheck("Remove original column", false);
    gtk_grid_attach(GTK_GRID(grid), rem, 1, 4, 1, 1);

    dlg.okCancel();
    if (dlg.run() != 1) return false;
    splitCode = ddSel(opDd) + 1;
    splitColumn = ddSel(colDd);
    param1 = entryText(p1);
    param2 = entryText(p2);
    removeOrg = checkOn(rem);
    return true;
}

// ── ReformatForm ────────────────────────────────────────────────────────────

bool csvDlgReformat(const CsvDefinition &csvdef, std::string &newSeparator,
                    bool &updateSeparator, std::string &newDateTime,
                    std::string &newDecimal, std::string &replaceCrLf, bool &alignVert) {
    ModalDlg dlg("Reformat data");
    GtkWidget *grid = mkGrid();
    gtk_box_append(GTK_BOX(dlg.vbox), grid);

    static const char *sepNames[] = {"Comma ,", "Semicolon ;", "Tab", "Pipe |",
                                     "Fixed width", nullptr};
    static const char *decNames[] = {"(unchanged)", "Point .", "Comma ,", nullptr};

    GtkWidget *updSep = mkCheck("Change separator to:", false);
    gtk_grid_attach(GTK_GRID(grid), updSep, 0, 0, 1, 1);
    // preselect something different from the current separator
    GtkWidget *sepDd = mkDropDown(sepNames, csvdef.Separator == ',' ? 1 : 0);
    gtk_grid_attach(GTK_GRID(grid), sepDd, 1, 0, 1, 1);

    GtkWidget *dtField = gridRow(grid, 1, "Datetime format:", mkEntry("", 22));
    gtk_entry_set_placeholder_text(GTK_ENTRY(dtField), "e.g. yyyy-MM-dd (empty = unchanged)");
    GtkWidget *decDd = gridRow(grid, 2, "Decimal separator:", mkDropDown(decNames, 0));
    GtkWidget *crlfField = gridRow(grid, 3, "Replace CR/LF in values by:", mkEntry(" ", 8));
    GtkWidget *align = mkCheck("Align columns vertically", false);
    gtk_grid_attach(GTK_GRID(grid), align, 1, 4, 1, 1);

    dlg.okCancel();
    if (dlg.run() != 1) return false;

    static const char *seps[] = {",", ";", "\t", "|", "\0"};
    int sidx = ddSel(sepDd);
    newSeparator = sidx == 4 ? std::string(1, '\0') : seps[sidx];
    updateSeparator = checkOn(updSep);
    newDateTime = entryText(dtField);
    int didx = ddSel(decDd);
    newDecimal = didx == 0 ? "" : (didx == 1 ? "." : ",");
    replaceCrLf = entryText(crlfField);
    alignVert = checkOn(align);
    return true;
}

// ── DetectColumnsForm (manual parameters) ───────────────────────────────────

bool csvDlgDetectColumns(char &sep, std::string &widths, bool &header, int &skip,
                         char &comm) {
    ModalDlg dlg("Detect columns parameters");
    GtkWidget *grid = mkGrid();
    gtk_box_append(GTK_BOX(dlg.vbox), grid);

    static const char *sepNames[] = {"Comma ,", "Semicolon ;", "Tab", "Pipe |",
                                     "Fixed width", nullptr};
    GtkWidget *sepDd = gridRow(grid, 0, "Separator:", mkDropDown(sepNames, 0));
    GtkWidget *widField = gridRow(grid, 1, "Fixed widths (comma sep.):", mkEntry("", 18));
    gtk_entry_set_placeholder_text(GTK_ENTRY(widField), "e.g. 8, 6, 12");
    GtkWidget *head = mkCheck("First line contains column names", true);
    gtk_grid_attach(GTK_GRID(grid), head, 1, 2, 1, 1);
    GtkWidget *skipField = gridRow(grid, 3, "Skip first lines:", mkEntry("0", 5));
    GtkWidget *commField = gridRow(grid, 4, "Comment character:", mkEntry("#", 5));

    dlg.okCancel();
    if (dlg.run() != 1) return false;

    static const char sepChars[] = {',', ';', '\t', '|', '\0'};
    sep = sepChars[ddSel(sepDd)];
    widths = entryText(widField);
    header = checkOn(head);
    int skipVal = atoi(entryText(skipField).c_str());
    skip = skipVal >= 0 ? skipVal : 0;
    std::string cs = entryText(commField);
    comm = cs.empty() ? '\0' : cs[0];
    return true;
}

// ── ColumnsSelectForm ───────────────────────────────────────────────────────

namespace {
struct ColSelCtx {
    GtkWidget *list = nullptr;               // GtkListBox
    std::vector<GtkWidget *> checks;         // one check button per row
    std::vector<int> order;                  // original column index per row
    void move(int delta) {
        GtkListBoxRow *sel = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
        if (!sel) return;
        int row = gtk_list_box_row_get_index(sel);
        int dst = row + delta;
        if (dst < 0 || dst >= (int)checks.size()) return;
        std::swap(checks[row], checks[dst]);
        std::swap(order[row], order[dst]);
        // physically move the dragged row: re-insert its child at dst
        GtkWidget *check = gtk_list_box_row_get_child(sel);
        g_object_ref(check);
        gtk_list_box_remove(GTK_LIST_BOX(list), GTK_WIDGET(sel));
        gtk_list_box_insert(GTK_LIST_BOX(list), check, dst);
        g_object_unref(check);
        gtk_list_box_select_row(GTK_LIST_BOX(list),
                                gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), dst));
    }
    void allNone() {
        bool anyOff = false;
        for (GtkWidget *c : checks)
            if (!gtk_check_button_get_active(GTK_CHECK_BUTTON(c))) { anyOff = true; break; }
        for (GtkWidget *c : checks)
            gtk_check_button_set_active(GTK_CHECK_BUTTON(c), anyOff);
    }
};
} // namespace

bool csvDlgSelectColumns(const CsvDefinition &csvdef, std::vector<int> &selIdx) {
    ModalDlg dlg("Select columns");
    ColSelCtx ctx;

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(dlg.vbox), hbox);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, 250, 265);
    ctx.list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ctx.list), GTK_SELECTION_SINGLE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), ctx.list);
    gtk_box_append(GTK_BOX(hbox), scroll);

    for (size_t i = 0; i < csvdef.Fields.size(); i++) {
        GtkWidget *c = mkCheck(csvdef.Fields[i].Name.c_str(), true);
        gtk_list_box_insert(GTK_LIST_BOX(ctx.list), c, -1);
        ctx.checks.push_back(c);
        ctx.order.push_back((int)i);
    }

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_valign(btns, GTK_ALIGN_START);
    GtkWidget *up = gtk_button_new_with_label("Up");
    g_signal_connect(up, "clicked", G_CALLBACK(+[](GtkButton *, gpointer p) {
        ((ColSelCtx *)p)->move(-1);
    }), &ctx);
    GtkWidget *down = gtk_button_new_with_label("Down");
    g_signal_connect(down, "clicked", G_CALLBACK(+[](GtkButton *, gpointer p) {
        ((ColSelCtx *)p)->move(+1);
    }), &ctx);
    GtkWidget *all = gtk_button_new_with_label("All/None");
    g_signal_connect(all, "clicked", G_CALLBACK(+[](GtkButton *, gpointer p) {
        ((ColSelCtx *)p)->allNone();
    }), &ctx);
    gtk_box_append(GTK_BOX(btns), up);
    gtk_box_append(GTK_BOX(btns), down);
    gtk_box_append(GTK_BOX(btns), all);
    gtk_box_append(GTK_BOX(hbox), btns);

    dlg.okCancel();
    if (dlg.run() != 1) return false;

    selIdx.clear();
    for (size_t i = 0; i < ctx.checks.size(); i++)
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(ctx.checks[i])))
            selIdx.push_back(ctx.order[i]);
    return !selIdx.empty();
}

// ── DataConvertForm ─────────────────────────────────────────────────────────

bool csvDlgDataConvert(int &convertType, std::string &tableName, int &sqlDialect,
                       int &batchSize) {
    ModalDlg dlg("Convert data");
    GtkWidget *grid = mkGrid();
    gtk_box_append(GTK_BOX(dlg.vbox), grid);

    static const char *types[] = {"SQL insert script", "XML", "JSON", nullptr};
    static const char *sqls[]  = {"MySQL / MariaDB", "MS-SQL", "PostgreSQL", nullptr};
    GtkWidget *typeDd = gridRow(grid, 0, "Convert to:",
        mkDropDown(types, (convertType >= 0 && convertType <= 2) ? convertType : 0));
    GtkWidget *nameField = gridRow(grid, 1, "Table/record name:", mkEntry(tableName, 22));
    gtk_entry_set_placeholder_text(GTK_ENTRY(nameField), "(empty = use file name)");
    GtkWidget *sqlDd = gridRow(grid, 2, "SQL type:",
        mkDropDown(sqls, (sqlDialect >= 0 && sqlDialect <= 2) ? sqlDialect : 0));
    GtkWidget *batchField = gridRow(grid, 3, "SQL batch size:",
                                    mkEntry(std::to_string(batchSize), 8));

    dlg.okCancel();
    if (dlg.run() != 1) return false;
    convertType = ddSel(typeDd);
    tableName = entryText(nameField);
    sqlDialect = ddSel(sqlDd);
    int batch = atoi(entryText(batchField).c_str());
    batchSize = batch > 0 ? batch : 1000;
    return true;
}

// ── MetaDataGenerateForm ────────────────────────────────────────────────────

bool csvDlgGenerateMetadata(int &metadataType) {
    ModalDlg dlg("Generate metadata");

    gtk_box_append(GTK_BOX(dlg.vbox), mkLabel("Generate metadata or script"));

    static const char *titles[] = {
        "Schema ini", "W3C CSV schema JSON", "Datadictionary CSV",
        "Python script", "R-script", "PowerShell",
    };
    GtkWidget *radios[6];
    GtkWidget *group = nullptr;
    // metadata group (0..2), then the script section (3..5)
    gtk_box_append(GTK_BOX(dlg.vbox), mkBold("Generate metadata"));
    for (int i = 0; i < 6; i++) {
        if (i == 3) gtk_box_append(GTK_BOX(dlg.vbox), mkBold("Generate script"));
        GtkWidget *r = gtk_check_button_new_with_label(titles[i]);
        if (group) gtk_check_button_set_group(GTK_CHECK_BUTTON(r), GTK_CHECK_BUTTON(group));
        else group = r;
        gtk_widget_set_margin_start(r, 14);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(r), i == metadataType);
        gtk_box_append(GTK_BOX(dlg.vbox), r);
        radios[i] = r;
    }

    dlg.okCancel();
    if (dlg.run() != 1) return false;
    for (int i = 0; i < 6; i++)
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(radios[i]))) {
            metadataType = i;
            break;
        }
    return true;
}

// ── Settings window (property-grid parity) ──────────────────────────────────

bool csvDlgSettings(void) {
    CsvSettings &st = csvSettings();
    ModalDlg dlg("CSV Lint Settings");
    GtkWidget *grid = mkGrid();
    gtk_box_append(GTK_BOX(dlg.vbox), grid);
    int row = 0;
    auto section = [&](const char *title) {
        GtkWidget *l = mkBold(title);
        if (row > 0) gtk_widget_set_margin_top(l, 8);
        gtk_grid_attach(GTK_GRID(grid), l, 0, row++, 2, 1);
    };
    auto chStr = [](char c) { return std::string(1, c); };

    // ── Analyze ─────────────────────────────────────────────────────────────
    section("Analyze");
    GtkWidget *fComment = gridRow(grid, row++, "Comment character:",
        mkEntry(chStr(st.CommentCharacter), 6),
        "Comment character, when the first lines start with this character "
        "they will be skipped as comment lines");
    GtkWidget *fDecMax = gridRow(grid, row++, "Decimal digits maximum:",
        mkEntry(std::to_string(st.DecimalDigitsMax), 6),
        "Maximum amount of decimals for decimal values, if a value has more "
        "then it's considered a text value. Applies to both autodetecting "
        "datatypes and validating data.");
    GtkWidget *cLeadZero = gridRow(grid, row++, "Decimal leading zero:",
        mkCheck("", st.DecimalLeadingZero),
        "Decimal values must have leading zero, set to false to accept "
        "values like .5 or .01");
    GtkWidget *fErrTol = gridRow(grid, row++, "Error tolerance (%):",
        mkEntry(std::to_string(st.ErrorTolerance), 6),
        "Error tolerance percentage, when analyzing allow X % errors. For "
        "example when a column with a 1000 values contains all integers "
        "except for 9 or fewer non-integer values, then it's still "
        "interpreted as an integer column.");
    GtkWidget *fIntMax = gridRow(grid, row++, "Integer digits maximum:",
        mkEntry(std::to_string(st.IntegerDigitsMax), 6),
        "Maximum amount of digits for integer values, if a value has more "
        "then it's considered a text value. Useful to distinguish (bar)codes "
        "and actual numeric values.");
    GtkWidget *fUnique = gridRow(grid, row++, "Unique values maximum:",
        mkEntry(std::to_string(st.UniqueValuesMax), 6),
        "Maximum unique values when reporting or detecting coded values, if "
        "column contains more than it's not reported.");
    GtkWidget *fYearMax = gridRow(grid, row++, "Year maximum:",
        mkEntry(std::to_string(st.YearMaximum), 6),
        "When detecting or validating date or datetime values, years larger "
        "than this value will be considered as out-of-range.");
    GtkWidget *fYearMin = gridRow(grid, row++, "Year minimum:",
        mkEntry(std::to_string(st.YearMinimum), 6),
        "When detecting or validating date or datetime values, years smaller "
        "than this value will be considered as out-of-range.");

    // ── Edit ────────────────────────────────────────────────────────────────
    section("Edit");
    static const char *quoteNames[] = {
        "None, minimal quotes", "Values with spaces", "All string values",
        "All non-numeric values", "All values", nullptr};
    GtkWidget *pQuotes = gridRow(grid, row++, "Reformat, apply quotes:",
        mkDropDown(quoteNames,
                   (st.ReformatQuotes >= 0 && st.ReformatQuotes <= 4) ? st.ReformatQuotes : 0),
        "Reformat dataset, apply quotes option.");
    GtkWidget *cTrim = gridRow(grid, row++, "Trim values:",
        mkCheck("", st.TrimValues),
        "Trim values when editing, sorting or analyzing data. Recommended, "
        "because when disabled the column datatypes will not always be "
        "detected correctly.");
    GtkWidget *fYear2 = gridRow(grid, row++, "Two digit year maximum:",
        mkEntry(st.TwoDigitYearMaxStr, 12),
        "Maximum year for two digit year date values. For example, when set "
        "to 2030 the year values 30 and 31 will be interpreted as 2030 and "
        "1931. Set as CurrentYear for current year.");

    // ── General ─────────────────────────────────────────────────────────────
    section("General");
    GtkWidget *fSyntax = gridRow(grid, row++, "Auto syntax limit (bytes):",
        mkEntry(std::to_string(st.AutoSyntaxLimit), 10),
        "Automatically apply column colors and result syntax highlighting "
        "only when the file is smaller than this size, to prevent freezing "
        "on large files.");
    GtkWidget *fQuoteCh = gridRow(grid, row++, "Default quote character:",
        mkEntry(chStr(st.DefaultQuoteChar), 6),
        "Default quote character, typically double quote \" or single quote '");
    GtkWidget *fNull = gridRow(grid, row++, "Null keyword:",
        mkEntry(st.NullKeyword, 10),
        "A case-sensitive keyword that will be treated as an empty value, "
        "typically NULL, NaN, NA or None depending on your data.");
    GtkWidget *cSepColor = gridRow(grid, row++, "Separator color:",
        mkCheck("", st.SeparatorColor),
        "Include separator in syntax highlighting colors. Set to false and "
        "the separator characters are not colored.");
    GtkWidget *fSeps = gridRow(grid, row++, "Separators:",
        mkEntry(csvSettingsEscapeSeparators(st.Separators), 12),
        "Preferred characters when automatically detecting the separator "
        "character. For special characters like tab, use \\t or hexadecimal "
        "escape sequence \\u0009 or \\x09.");

    dlg.okCancel();
    if (dlg.run() != 1) return false;

    auto firstChar = [](GtkWidget *f, char dflt) {
        std::string s = entryText(f);
        return s.empty() ? dflt : s[0];
    };
    auto intOr = [](GtkWidget *f, int dflt, int minv) {
        std::string s = entryText(f);
        if (s.empty()) return dflt;
        int n = atoi(s.c_str());
        return n < minv ? dflt : n;
    };

    st.CommentCharacter   = firstChar(fComment, '#');
    st.DecimalDigitsMax   = intOr(fDecMax, 20, 0);
    st.DecimalLeadingZero = checkOn(cLeadZero);
    st.ErrorTolerance     = intOr(fErrTol, 1, 0);
    st.ErrorTolerancePerc = 0.01f * st.ErrorTolerance;
    st.IntegerDigitsMax   = intOr(fIntMax, 12, 1);
    st.UniqueValuesMax    = intOr(fUnique, 15, 1);
    st.YearMaximum        = intOr(fYearMax, 2050, 1);
    st.YearMinimum        = intOr(fYearMin, 1900, 1);
    st.ReformatQuotes     = ddSel(pQuotes);
    st.TrimValues         = checkOn(cTrim);
    csvSettingsApplyTwoDigitYearMax(entryText(fYear2));
    st.AutoSyntaxLimit    = intOr(fSyntax, 1024 * 1024, 1024);
    st.DefaultQuoteChar   = firstChar(fQuoteCh, '"');
    st.NullKeyword        = entryText(fNull);
    st.SeparatorColor     = checkOn(cSepColor);
    st.Separators         = csvSettingsUnescapeSeparators(entryText(fSeps));
    return true;
}
