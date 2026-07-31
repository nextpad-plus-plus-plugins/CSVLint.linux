#include "CsvSchemaIni.h"
#include <fstream>
#include <algorithm>
#include <cctype>

static const char *INI_NAME = "schema.ini";

static std::string dirName(const std::string &p) {
    size_t s = p.rfind('/');
    return s == std::string::npos ? "" : p.substr(0, s);
}
static std::string fileName(const std::string &p) {
    size_t s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}
static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}
static std::string stripCr(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

std::vector<std::pair<std::string, std::string>>
CsvSchemaIni::ReadIniSection(const std::string &filePath) {
    std::vector<std::pair<std::string, std::string>> inilines;
    std::string path = dirName(filePath);
    if (path.empty()) return inilines;
    std::string section = "[" + lower(fileName(filePath)) + "]";
    std::ifstream f(path + "/" + INI_NAME);
    if (!f) return inilines;

    std::string line;
    bool bSec = false;
    while (std::getline(f, line)) {
        line = stripCr(line);
        if (line.empty()) continue;
        if (line[0] == '[') {
            if (bSec) break;                 // next section reached
            bSec = lower(line) == section;
        } else if (bSec) {
            // C# parity: Split('=') and take part [1] only
            std::string key = line, val;
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                key = line.substr(0, eq);
                size_t eq2 = line.find('=', eq + 1);
                val = eq2 == std::string::npos ? line.substr(eq + 1)
                                               : line.substr(eq + 1, eq2 - eq - 1);
            }
            bool exists = false;
            for (auto &kv : inilines) if (kv.first == key) { exists = true; break; }
            if (!exists) inilines.emplace_back(key, val);
        }
    }
    return inilines;
}

std::string CsvSchemaIni::ReadIniSectionLines(const std::string &filePath) {
    std::string res;
    for (auto &kv : ReadIniSection(filePath))
        res += kv.first + "=" + kv.second + "\r\n";
    return res;
}

bool CsvSchemaIni::WriteIniSection(const std::string &filePath, const std::string &inikeys,
                                   std::string &errmsg) {
    errmsg.clear();
    std::string path = dirName(filePath);
    std::string inifile = path + "/" + INI_NAME;
    std::string section = "[" + lower(fileName(filePath)) + "]";

    // keep every line that is NOT in our section; remember where it was
    std::vector<std::string> inilines;
    long idx = -1;
    {
        std::ifstream f(inifile);
        if (f) {
            std::string line;
            bool bSec = false;
            while (std::getline(f, line)) {
                line = stripCr(line);
                if (!line.empty() && line[0] == '[') {
                    bSec = lower(line) == section;
                    if (bSec) idx = (long)inilines.size();
                }
                if (!bSec) inilines.push_back(line);
            }
        }
    }
    if (idx == -1) {
        if (!inilines.empty() && !inilines.back().empty()) inilines.push_back("");
        idx = (long)inilines.size();
    }

    // splice in the new section (header + inikeys lines)
    std::vector<std::string> newsec;
    newsec.push_back("[" + fileName(filePath) + "]");
    {
        std::string cur;
        for (char c : inikeys) {
            if (c == '\r') continue;
            if (c == '\n') { newsec.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) newsec.push_back(cur);
    }

    std::ofstream out(inifile, std::ios::trunc);
    if (!out) {
        errmsg = "Unable to write " + inifile;
        return false;
    }
    for (long i = 0; i < (long)inilines.size(); i++) {
        if (i == idx)
            for (auto &s : newsec) out << s << "\n";
        out << inilines[i] << "\n";
    }
    if (idx >= (long)inilines.size())
        for (auto &s : newsec) out << s << "\n";
    return true;
}
