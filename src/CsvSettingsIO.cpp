// CsvSettingsIO.cpp — Linux port of CsvSettingsIO.mm. Identical ini format
// and key set; NSString file IO becomes g_file_get/set_contents.
#include "CsvSettingsIO.h"
#include "engine/CsvDefinition.h"
#include <glib.h>
#include <string>
#include <map>
#include <ctime>
#include <cstdio>
#include <cstdlib>

static NppData *sNpp = nullptr;

void csvSettingsIOInit(NppData *data) { sNpp = data; }

static std::string iniPath() {
    if (!sNpp) return "";
    char buf[1024] = {0};
    sNpp->_sendMessage(sNpp->_nppHandle, NPPM_GETPLUGINSCONFIGDIR,
                       sizeof(buf) - 1, (intptr_t)buf);
    if (!buf[0]) return "";
    return std::string(buf) + "/CSV Lint.ini";   // Windows PluginName parity
}

// ── Separators escape/unescape (Settings.cs parity) ─────────────────────────

std::string csvSettingsEscapeSeparators(const std::string &chars) {
    std::string out;
    char hex[8];
    for (unsigned char c : chars) {
        if (c == '\t') { out += "\\t"; }
        else if (c < 32) { snprintf(hex, sizeof(hex), "\\x%02x", c); out += hex; }
        else out += (char)c;
    }
    return out;
}

std::string csvSettingsUnescapeSeparators(const std::string &escaped) {
    std::string s = escaped;
    if (s.empty()) s = ",;\\t|";
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char l = s[i + 1];
            if (l == 't') { out += '\t'; i += 2; continue; }
            if (l == 'r') { out += '\r'; i += 2; continue; }
            if (l == 'n') { out += '\n'; i += 2; continue; }
            if (l == 'a') { out += '\a'; i += 2; continue; }
            if ((l == 'x' && i + 3 < s.size()) || (l == 'u' && i + 5 < s.size())) {
                size_t n = (l == 'x') ? 2 : 4;
                std::string hexs = s.substr(i + 2, n);
                char *endp = nullptr;
                long v = strtol(hexs.c_str(), &endp, 16);
                if (endp && *endp == '\0' && v > 0 && v < 256) {
                    out += (char)v;
                    i += 2 + n;
                    continue;
                }
            }
        }
        out += s[i++];
    }
    return out;
}

// ── TwoDigitYearMax "CurrentYear" resolution (Settings.cs parity) ───────────

static int currentYear() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    return tmv.tm_year + 1900;
}

void csvSettingsApplyTwoDigitYearMax(const std::string &val) {
    CsvSettings &st = csvSettings();
    int yr = atoi(val.c_str());
    if (yr <= 0 || yr >= 9999) {
        st.TwoDigitYearMaxStr = "CurrentYear";
        st.TwoDigitYearMax = currentYear();
    } else {
        st.TwoDigitYearMaxStr = val;
        st.TwoDigitYearMax = yr;
    }
}

// ── load ────────────────────────────────────────────────────────────────────

static std::string trimWs(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool parseBool(const std::string &v, bool dflt) {
    if (v.empty()) return dflt;
    return v == "True" || v == "true" || v == "1";
}

void csvSettingsLoad(void) {
    std::string path = iniPath();
    if (path.empty()) return;
    gchar *contents = nullptr;
    if (!g_file_get_contents(path.c_str(), &contents, nullptr, nullptr))
        return;   // first run: keep defaults

    // Flat key->value map; section headers only matter for writing.
    std::map<std::string, std::string> kv;
    std::string all = contents;
    g_free(contents);
    size_t pos = 0;
    while (pos <= all.size()) {
        size_t nl = all.find('\n', pos);
        std::string line = trimWs(all.substr(pos, nl == std::string::npos
                                                      ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? all.size() + 1 : nl + 1;
        if (line.empty() || line[0] == '[' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[trimWs(line.substr(0, eq))] = trimWs(line.substr(eq + 1));
    }
    auto has = [&](const char *k) { return kv.count(k) > 0; };
    auto str = [&](const char *k) { return kv[k]; };
    auto num = [&](const char *k, int dflt) {
        return has(k) && !kv[k].empty() ? atoi(kv[k].c_str()) : dflt;
    };
    auto bl = [&](const char *k, bool dflt) { return has(k) ? parseBool(kv[k], dflt) : dflt; };
    auto ch = [&](const char *k, char dflt) {
        return has(k) && !kv[k].empty() ? kv[k][0] : dflt;
    };

    CsvSettings &st = csvSettings();
    // Analyze
    st.CommentCharacter   = ch("CommentCharacter", st.CommentCharacter);
    st.DecimalDigitsMax   = num("DecimalDigitsMax", st.DecimalDigitsMax);
    st.DecimalLeadingZero = bl("DecimalLeadingZero", st.DecimalLeadingZero);
    st.ErrorTolerance     = num("ErrorTolerance", st.ErrorTolerance);
    st.ErrorTolerancePerc = 0.01f * st.ErrorTolerance;
    st.IntegerDigitsMax   = num("IntegerDigitsMax", st.IntegerDigitsMax);
    st.UniqueValuesMax    = num("UniqueValuesMax", st.UniqueValuesMax);
    st.YearMaximum        = num("YearMaximum", st.YearMaximum);
    st.YearMinimum        = num("YearMinimum", st.YearMinimum);
    // Edit
    st.ReformatQuotes     = num("ReformatQuotes", st.ReformatQuotes);
    st.TrimValues         = bl("TrimValues", st.TrimValues);
    if (has("TwoDigitYearMax")) csvSettingsApplyTwoDigitYearMax(str("TwoDigitYearMax"));
    // General
    st.AutoSyntaxLimit    = num("AutoSyntaxLimit", st.AutoSyntaxLimit);
    st.DefaultQuoteChar   = ch("DefaultQuoteChar", st.DefaultQuoteChar);
    if (has("NullKeyword")) st.NullKeyword = str("NullKeyword");
    st.SeparatorColor     = bl("SeparatorColor", st.SeparatorColor);
    if (has("Separators")) st.Separators = csvSettingsUnescapeSeparators(str("Separators"));
    st.TransparentCursor  = bl("TransparentCursor", st.TransparentCursor);
    // UserDialogs
    st.AutoDetectColumns  = bl("AutoDetectColumns", st.AutoDetectColumns);
    st.DataConvertBatch   = num("DataConvertBatch", st.DataConvertBatch);
    if (st.DataConvertBatch < 1) st.DataConvertBatch = 1;
    if (has("DataConvertName")) st.DataConvertName = str("DataConvertName");
    st.DataConvertSQL     = num("DataConvertSQL", st.DataConvertSQL);
    st.DataConvertType    = num("DataConvertType", st.DataConvertType);
    st.MetadataType       = num("MetadataType", st.MetadataType);
}

// ── save ────────────────────────────────────────────────────────────────────

void csvSettingsSave(void) {
    std::string path = iniPath();
    if (path.empty()) return;
    CsvSettings &st = csvSettings();
    auto B = [](bool b) { return b ? "True" : "False"; };

    std::string s;
    s += "; CSV Lint plug-in settings\r\n";
    s += "\r\n[Analyze]\r\n";
    s += std::string("CommentCharacter=") + st.CommentCharacter + "\r\n";
    s += "DecimalDigitsMax=" + std::to_string(st.DecimalDigitsMax) + "\r\n";
    s += std::string("DecimalLeadingZero=") + B(st.DecimalLeadingZero) + "\r\n";
    s += "ErrorTolerance=" + std::to_string(st.ErrorTolerance) + "\r\n";
    s += "IntegerDigitsMax=" + std::to_string(st.IntegerDigitsMax) + "\r\n";
    s += "UniqueValuesMax=" + std::to_string(st.UniqueValuesMax) + "\r\n";
    s += "YearMaximum=" + std::to_string(st.YearMaximum) + "\r\n";
    s += "YearMinimum=" + std::to_string(st.YearMinimum) + "\r\n";
    s += "\r\n[Edit]\r\n";
    s += "ReformatQuotes=" + std::to_string(st.ReformatQuotes) + "\r\n";
    s += std::string("TrimValues=") + B(st.TrimValues) + "\r\n";
    s += "TwoDigitYearMax=" + st.TwoDigitYearMaxStr + "\r\n";
    s += "\r\n[General]\r\n";
    s += "AutoSyntaxLimit=" + std::to_string(st.AutoSyntaxLimit) + "\r\n";
    s += std::string("DefaultQuoteChar=") + st.DefaultQuoteChar + "\r\n";
    s += "NullKeyword=" + st.NullKeyword + "\r\n";
    s += std::string("SeparatorColor=") + B(st.SeparatorColor) + "\r\n";
    s += "Separators=" + csvSettingsEscapeSeparators(st.Separators) + "\r\n";
    s += std::string("TransparentCursor=") + B(st.TransparentCursor) + "\r\n";
    s += "\r\n[UserDialogs]\r\n";
    s += std::string("AutoDetectColumns=") + B(st.AutoDetectColumns) + "\r\n";
    s += "DataConvertBatch=" + std::to_string(st.DataConvertBatch) + "\r\n";
    s += "DataConvertName=" + st.DataConvertName + "\r\n";
    s += "DataConvertSQL=" + std::to_string(st.DataConvertSQL) + "\r\n";
    s += "DataConvertType=" + std::to_string(st.DataConvertType) + "\r\n";
    s += "MetadataType=" + std::to_string(st.MetadataType) + "\r\n";

    g_file_set_contents(path.c_str(), s.c_str(), (gssize)s.size(), nullptr);
}
