#include "CsvDefinition.h"
#include <algorithm>
#include <cctype>

CsvSettings &csvSettings() {
    static CsvSettings s;
    return s;
}

// ── small string helpers (C# parity) ────────────────────────────────────────

static std::string trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string trimChar(const std::string &s, char c) {
    size_t b = 0, e = s.size();
    while (b < e && s[b] == c) b++;
    while (e > b && s[e - 1] == c) e--;
    return s.substr(b, e - b);
}

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static std::string replaceAll(std::string s, const std::string &from, const std::string &to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

static bool tryParseInt(const std::string &s, int &out) {
    std::string t = trim(s);
    if (t.empty()) return false;
    size_t i = (t[0] == '+' || t[0] == '-') ? 1 : 0;
    if (i == t.size()) return false;
    long v = 0;
    for (; i < t.size(); i++) {
        if (t[i] < '0' || t[i] > '9') return false;
        v = v * 10 + (t[i] - '0');
        if (v > 2147483647L) return false;
    }
    out = (t[0] == '-') ? (int)-v : (int)v;
    return true;
}

int naturalCompare(const std::string &a, const std::string &b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        bool da = isdigit((unsigned char)a[i]), db = isdigit((unsigned char)b[j]);
        if (da && db) {
            size_t si = i, sj = j;
            while (i < a.size() && isdigit((unsigned char)a[i])) i++;
            while (j < b.size() && isdigit((unsigned char)b[j])) j++;
            // compare digit runs numerically (strip leading zeros via length-then-lex)
            std::string na = a.substr(si, i - si), nb = b.substr(sj, j - sj);
            size_t za = na.find_first_not_of('0'); na = (za == std::string::npos) ? "0" : na.substr(za);
            size_t zb = nb.find_first_not_of('0'); nb = (zb == std::string::npos) ? "0" : nb.substr(zb);
            if (na.size() != nb.size()) return na.size() < nb.size() ? -1 : 1;
            int c = na.compare(nb);
            if (c != 0) return c < 0 ? -1 : 1;
        } else {
            if (a[i] != b[j]) return (unsigned char)a[i] < (unsigned char)b[j] ? -1 : 1;
            i++; j++;
        }
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

// ── CsvColumn ───────────────────────────────────────────────────────────────

CsvColumn::CsvColumn(int idx) {
    Index = idx;
    Name = "F" + std::to_string(idx);
    MaxWidth = 50;
    DataType = ColumnType::String;
    Mask = "";
    Initialize();
}

CsvColumn::CsvColumn(int idx, const std::string &name, int maxwidth, ColumnType datatype,
                     const std::string &mask) {
    Index = idx;
    Name = name;
    MaxWidth = maxwidth;
    DataType = datatype;
    Mask = mask;
    Initialize();
}

void CsvColumn::Initialize() {
    sTag = "";
    iTag = -1;

    if (DataType == ColumnType::Decimal) {
        // get decimal position or npos if not found
        size_t pos1 = Mask.rfind('.');
        size_t pos2 = Mask.rfind(',');
        long p1 = pos1 == std::string::npos ? -1 : (long)pos1;
        long p2 = pos2 == std::string::npos ? -1 : (long)pos2;
        long p = p1 > p2 ? p1 : p2;

        DecimalSymbol = p1 > p2 ? '.' : ',';
        sTag = p1 > p2 ? ",." : ".,";
        iTag     = (int)((long)Mask.size() - p - 1);
        Decimals = (int)((long)Mask.size() - p - 1);
    }
}

void CsvColumn::UpdateDateTimeMask(const std::string &newmask) {
    Mask = newmask;
    MaxWidth = (int)newmask.size();
    // For any mask part without leading zeros, add 1 to the max length
    static const char *tmp[] = {"dd", "mm", "hh", "DD", "MM", "HH"};
    for (const char *s : tmp) {
        if (newmask.find(s) == std::string::npos &&
            newmask.find(s[0]) != std::string::npos)
            MaxWidth++;
    }
}

void CsvColumn::AddCodedValues(const std::vector<std::pair<std::string, int>> &slcodes) {
    // if any contains CR/LF, then it's probably text not codes
    bool containsCrLf = false;
    int total = 0;
    for (auto &s : slcodes) {
        if (s.first.find('\r') != std::string::npos ||
            s.first.find('\n') != std::string::npos) containsCrLf = true;
        total += s.second;
    }

    if (!containsCrLf && !slcodes.empty() &&
        (int)slcodes.size() <= csvSettings().UniqueValuesMax) {
        // enumeration ratio: each unique value used at least 2 times on average
        double ratio = 1.0 * total / slcodes.size();
        if (ratio >= 2.0) {
            isCodedValue = true;
            CodedList.clear();
            for (auto &s : slcodes) CodedList.push_back(s.first);
            std::sort(CodedList.begin(), CodedList.end(),
                      [](const std::string &a, const std::string &b) {
                          return naturalCompare(a, b) < 0;
                      });
        }
    }
}

// ── CsvDefinition ───────────────────────────────────────────────────────────

void CsvDefinition::AddColumn(const std::string &name, int maxwidth,
                              ColumnType datatype, const std::string &mask) {
    AddColumn((int)Fields.size() + 1, name, maxwidth, datatype, mask);
}

void CsvDefinition::AddColumn(int idx, const std::string &name, int maxwidth,
                              ColumnType datatype, const std::string &mask) {
    std::string m = mask;
    if (datatype == ColumnType::DateTime) {
        // datetime column MUST have a mask; first one becomes the global format
        if (m.empty()) m = DateTimeFormat.empty() ? "yyyy-MM-dd" : DateTimeFormat;
        if (DateTimeFormat.empty()) DateTimeFormat = m;
    }
    if (datatype == ColumnType::Decimal) {
        size_t pos1 = m.rfind('.');
        size_t pos2 = m.rfind(',');
        long p1 = pos1 == std::string::npos ? -1 : (long)pos1;
        long p2 = pos2 == std::string::npos ? -1 : (long)pos2;
        if (p1 >= 0 || p2 >= 0) {
            DecimalSymbol = p1 > p2 ? '.' : ',';
            NumberDigits = (int)((long)m.size() - (p1 > p2 ? p1 : p2) - 1);
        }
    }
    Fields.emplace_back(idx, name, maxwidth, datatype, m);
}

void CsvDefinition::RemoveColumn(int index) {
    Fields.erase(Fields.begin() + index);
}

CsvDefinition CsvDefinition::FromIniLines(const std::string &inilines, std::string *outError) {
    CsvDefinition def;
    if (outError) outError->clear();

    // key/value pairs in file order (order matters: DateTimeFormat and
    // NumberDigits must land before the ColN lines that use them)
    std::vector<std::pair<std::string, std::string>> result;
    std::vector<std::string> dup;

    TextReader rd(inilines);
    std::string line;
    while (rd.ReadLine(line)) {
        std::string nextline = trim(line);
        size_t eq = nextline.find('=');
        if (!nextline.empty() && eq != std::string::npos && eq > 0) {
            std::string key = nextline.substr(0, eq);
            std::string val = nextline.substr(eq + 1);   // value may contain '='
            bool exists = false;
            for (auto &kv : result) if (kv.first == key) { exists = true; break; }
            if (exists) dup.push_back(key);
            else result.emplace_back(key, val);
        }
    }

    if (!dup.empty()) {
        std::string names;
        for (auto &d : dup) names += (names.empty() ? "" : ",") + d;
        if (outError) *outError = "Duplicate key(s) found (" + names + ")";
        return def;
    }

    def.CsvDefInitFromKeys(result);
    return def;
}

void CsvDefinition::CsvDefInitFromKeys(
        const std::vector<std::pair<std::string, std::string>> &inikeys) {
    FieldWidths.clear();

    for (auto &linekv : inikeys) {
        std::string k = toLower(linekv.first);
        std::string val = trim(linekv.second);
        std::string vallow = toLower(val);
        int vint = 0;
        tryParseInt(val, vint);

        // most important, what is the separator
        if (k == "format") {
            if (vallow == "tabdelimited") Separator = '\t';
            if (vallow == "csvdelimited") Separator = ',';
            if (vallow == "fixedlength")  Separator = '\0';
            // custom character, "Delimited(x)"
            if (vallow.size() >= 11 && vallow.compare(0, 10, "delimited(") == 0 &&
                vallow.back() == ')')
                Separator = val[10];
        }

        if (k == "datetimeformat") {
            // external schema.ini mask -> internal C#-style mask
            std::string tmp = val;
            tmp = replaceAll(tmp, "m", "M");
            tmp = replaceAll(tmp, "n", "m");
            tmp = replaceAll(tmp, "h", "H");
            DateTimeFormat = tmp;
        }

        if (k == "decimalsymbol" && !val.empty()) DecimalSymbol = val[0];
        if (k == "numberdigits") NumberDigits = vint;
        if (k == "numberleadingzeros" && vallow.size() > 1) NumberLeadingZeros = vallow[1] == 't';
        if (k == ";skiplines") SkipLines = vint;
        if (k == ";commentchar" && !val.empty()) CommentChar = val[0];

        if (k == "colnameheader") {
            ColNameHeader = !vallow.empty() && vallow[0] == 't';
        } else if (k.size() > 3 && k.compare(0, 3, "col") == 0 && k[3] != '\0' &&
                   k.compare(0, 4, ";col") != 0) {
            // "colN=Name Type Width W"
            int idx = 0;
            tryParseInt(k.substr(3), idx);
            idx -= 1;

            std::string name = "Column" + std::to_string(idx);
            std::string datatypestr;
            int maxwidth = 50;
            ColumnType datatype = ColumnType::String;
            std::string mask;

            // WIDTH must be at end of line
            size_t spc = vallow.rfind(' ');
            size_t pos = vallow.rfind("width");
            if (spc != std::string::npos && pos != std::string::npos && pos == spc - 5) {
                std::string width = vallow.substr(pos);
                val = trim(val.substr(0, pos));
                vallow = toLower(val);
                width = replaceAll(width, "width ", "");
                int n = 0;
                if (tryParseInt(width, n)) maxwidth = n;
            }

            // valid datatype must be at end of line
            spc = vallow.rfind(' ');
            {
                size_t from = (spc != std::string::npos) ? spc : 0;
                size_t p = vallow.find("text", from);
                if (p == std::string::npos) p = vallow.rfind("datetime");
                if (p == std::string::npos) p = vallow.rfind("float");
                if (p == std::string::npos) p = vallow.rfind("int");
                if (p != std::string::npos && spc != std::string::npos && p == spc + 1) {
                    datatypestr = vallow.substr(p);
                    val = trim(val.substr(0, p));
                }
            }

            if (datatypestr == "bit")      datatype = ColumnType::Integer;
            if (datatypestr == "byte")     datatype = ColumnType::Integer;
            if (datatypestr == "short")    datatype = ColumnType::Integer;
            if (datatypestr == "long")     datatype = ColumnType::Integer;
            if (datatypestr == "currency") datatype = ColumnType::Decimal;
            if (datatypestr == "single")   datatype = ColumnType::Decimal;
            if (datatypestr == "double")   datatype = ColumnType::Decimal;
            if (datatypestr == "datetime") datatype = ColumnType::DateTime;
            if (datatypestr == "text")     datatype = ColumnType::String;
            if (datatypestr == "memo")     datatype = ColumnType::String;
            if (datatypestr == "float")    datatype = ColumnType::Decimal;
            if (datatypestr == "integer")  datatype = ColumnType::Integer;
            if (datatypestr == "longchar") datatype = ColumnType::String;
            if (datatypestr == "date")     datatype = ColumnType::DateTime;

            if (datatype == ColumnType::DateTime) mask = DateTimeFormat;
            if (datatype == ColumnType::Decimal) {
                int dec = NumberDigits;
                int dig = maxwidth - dec - 1;
                if (dig < 0) dig = 1;
                mask = std::string(dig, '9') +
                       (DecimalSymbol ? DecimalSymbol : '.') + std::string(dec, '9');
            }

            if (!trim(val).empty()) name = val;

            // strip quotes around name (single stray quote tolerated)
            long quote1 = -1, quote2 = -1;
            size_t q1 = val.find('"'), q2 = val.rfind('"');
            if (q1 != std::string::npos) quote1 = (long)q1;
            if (q2 != std::string::npos) quote2 = (long)q2;
            if (quote1 == quote2) {
                if (quote1 == 0) quote2 = (long)val.size();
                if (quote1 > 0) quote1 = -1;
            }
            if (quote1 > 0 || quote2 > 0)
                name = val.substr(quote1 + 1, quote2 - quote1 - 1);

            AddColumn(idx, name, maxwidth, datatype, mask);
        }

        // comment lines may alter datatype+mask of certain columns (";colN=...")
        if (k.size() > 4 && k.compare(0, 4, ";col") == 0) {
            int idxalt = 0;
            tryParseInt(k.substr(4), idxalt);
            idxalt -= 1;
            std::string v = linekv.second;   // original case (C# uses Val here)
            std::string datatypestr;
            ColumnType datatypealt = ColumnType::String;

            bool isCoded = false;
            std::string codedValues;
            size_t posalt = v.rfind("Enumeration");
            if (posalt != std::string::npos) {
                size_t spcalt = v.find(' ', posalt);
                if (spcalt != std::string::npos) {
                    isCoded = true;
                    codedValues = trim(v.substr(spcalt));
                }
                v = "";
            }

            posalt = v.rfind("DateTime");
            if (posalt != std::string::npos) {
                size_t spcalt = v.find(' ', posalt);
                if (spcalt != std::string::npos) {
                    datatypestr = v.substr(posalt, spcalt - posalt);
                    v = trim(v.substr(spcalt));
                }
            }
            posalt = v.rfind("Float");
            if (posalt != std::string::npos) {
                size_t spcalt = v.find(' ', posalt);
                if (spcalt != std::string::npos) {
                    datatypestr = v.substr(posalt, spcalt - posalt);
                    v = trim(v.substr(spcalt));
                }
            }
            if (datatypestr == "DateTime") datatypealt = ColumnType::DateTime;
            if (datatypestr == "Float")    datatypealt = ColumnType::Decimal;

            if (datatypealt != ColumnType::String || isCoded) {
                for (auto &f : Fields) {
                    if (f.Index == idxalt) {
                        if (isCoded) {
                            f.isCodedValue = true;
                            f.CodedList.clear();
                            size_t start = 0, bar;
                            while ((bar = codedValues.find('|', start)) != std::string::npos) {
                                f.CodedList.push_back(codedValues.substr(start, bar - start));
                                start = bar + 1;
                            }
                            f.CodedList.push_back(codedValues.substr(start));
                        } else {
                            f.DataType = datatypealt;
                            f.Mask = v;
                            f.Initialize();
                        }
                    }
                }
            }
        }
    }

    // rebuild widths for fixed width data
    FieldWidths.clear();
    for (auto &f : Fields) FieldWidths.push_back(f.MaxWidth);
}

std::string CsvDefinition::SplitColumnNamePostfix(const std::string &namein, int *postfix) {
    std::string res = namein;
    if (postfix) *postfix = -1;
    size_t pos1 = namein.rfind('(');
    size_t pos2 = namein.rfind(')');
    if (pos1 != std::string::npos && pos2 != std::string::npos &&
        pos1 < pos2 && pos2 == namein.size() - 1) {
        std::string strnr = namein.substr(pos1 + 1, pos2 - pos1 - 1);
        int inr = 0;
        if (tryParseInt(strnr, inr) && !trim(strnr).empty()) {
            res = trim(namein.substr(0, pos1));
            if (postfix) *postfix = inr;
        }
    }
    return res;
}

std::string CsvDefinition::GetUniqueColumnName(const std::string &fieldname, int *postfix) const {
    int pf = -1;
    std::string namepart = SplitColumnNamePostfix(fieldname, &pf);
    for (auto &col : Fields) {
        if (namepart == col.Name && pf == -1) pf = 2;
        int inr = -1;
        std::string colname2 = SplitColumnNamePostfix(col.Name, &inr);
        if (namepart == colname2 && inr >= pf) pf = inr + 1;
    }
    if (postfix) *postfix = pf;
    return namepart;
}

std::string CsvDefinition::GetIniLines() {
    std::string res;

    if (Separator == '\t') res += "Format=TabDelimited\r\n";
    if (Separator == ',')  res += "Format=CSVDelimited\r\n";
    if (Separator == '\0') res += "Format=FixedLength\r\n";
    if (res.empty()) res += std::string("Format=Delimited(") + Separator + ")\r\n";

    res += std::string("ColNameHeader=") + (ColNameHeader ? "True" : "False") + "\r\n";

    if (!DateTimeFormat.empty()) {
        // internal C#-style mask -> external schema.ini mask
        std::string tmp = DateTimeFormat;
        tmp = replaceAll(tmp, "m", "n");
        tmp = replaceAll(tmp, "M", "m");
        tmp = replaceAll(tmp, "H", "h");
        res += "DateTimeFormat=" + tmp + "\r\n";
    }

    if (DecimalSymbol != '\0') res += std::string("DecimalSymbol=") + DecimalSymbol + "\r\n";

    // most common nr of fractional digits for float/decimal columns
    // (first-seen order on ties — C# Dictionary enumeration parity)
    std::vector<std::pair<int, int>> decimalOccurance;
    for (auto &fld : Fields) {
        if (fld.DataType == ColumnType::Decimal) {
            bool found = false;
            for (auto &kv : decimalOccurance)
                if (kv.first == fld.Decimals) { kv.second++; found = true; break; }
            if (!found) decimalOccurance.emplace_back(fld.Decimals, 1);
        }
    }
    NumberDigits = 0;
    int deccommon = 0;
    for (auto &deckey : decimalOccurance)
        if (deccommon < deckey.second) {
            NumberDigits = deckey.first;
            deccommon = deckey.second;
        }
    if (NumberDigits > 0) res += "NumberDigits=" + std::to_string(NumberDigits) + "\r\n";

    if (SkipLines > 0) res += ";SkipLines=" + std::to_string(SkipLines) + "\r\n";
    if (CommentChar != '\0') res += std::string(";CommentChar=") + CommentChar + "\r\n";

    // either all column names are in quotes or none
    bool quotename = false;
    for (auto &fld : Fields)
        if (fld.Name.find(' ') != std::string::npos) { quotename = true; break; }

    for (size_t i = 0; i < Fields.size(); i++) {
        CsvColumn &col = Fields[i];
        std::string def = col.Name;
        std::string com;

        if (quotename) def = "\"" + col.Name + "\"";

        if (col.isCodedValue) {
            std::string codedlist;
            for (size_t c = 0; c < col.CodedList.size(); c++)
                codedlist += (c ? "|" : "") + col.CodedList[c];
            com = ";Col" + std::to_string(i + 1) + "=" + def + " Enumeration " + codedlist + "\r\n";
        }

        if (col.DataType == ColumnType::String)  def += " Text";
        if (col.DataType == ColumnType::Unknown) def += " Text";
        if (col.DataType == ColumnType::Integer) def += " Integer";
        if (col.DataType == ColumnType::Decimal) {
            def += " Float";
            if (col.Decimals != NumberDigits)
                com = ";Col" + std::to_string(i + 1) + "=" + def + " " + col.Mask + "\r\n";
        }
        if (col.DataType == ColumnType::DateTime) {
            if (col.Mask == DateTimeFormat) {
                def += " DateTime";
            } else {
                com = ";Col" + std::to_string(i + 1) + "=" + def + " DateTime " + col.Mask + "\r\n";
                def += " Text";
            }
        }

        def += " Width " + std::to_string(col.MaxWidth);
        res += "Col" + std::to_string(i + 1) + "=" + def + "\r\n";
        if (!com.empty()) res += com;
    }

    return res;
}

int CsvDefinition::SkipCommentLinesAtStart(TextReader &strdata) {
    int res = 0;
    int skip = SkipLines;
    std::string line;

    while (skip > 0 && !strdata.EndOfStream()) {
        strdata.ReadLine(line);
        skip--;
        res++;
    }
    while (strdata.Peek() == (int)(unsigned char)CommentChar && !strdata.EndOfStream()) {
        strdata.ReadLine(line);
        res++;
    }
    ParseCurrentLine += res;
    return res;
}

std::vector<std::string> CsvDefinition::ParseNextLine(TextReader &strdata, bool &iscomment) {
    std::vector<std::string> res;
    iscomment = false;
    std::string value;
    const CsvSettings &st = csvSettings();

    if (Separator == '\0') {
        std::string line;
        strdata.ReadLine(line);
        ParseCurrentLine++;

        // fixed width columns
        int pos = 0;
        int fieldcount = (int)FieldWidths.size() - 1;
        for (int i = 0; i <= fieldcount; i++) {
            int w = FieldWidths[i];
            if (pos + w > (int)line.size()) w = (int)line.size() - pos;
            if (w < 0) w = 0;
            std::string fixval = line.substr(pos, w);
            if (st.TrimValues) {
                fixval = trim(fixval);
                fixval = RemoveQuotesToString(fixval);
            }
            res.push_back(fixval);
            pos += w;
            if (i == fieldcount && (int)line.size() > pos) {
                fixval = line.substr(pos);
                if (st.TrimValues) {
                    fixval = trim(fixval);
                    fixval = RemoveQuotesToString(fixval);
                }
                res.push_back(fixval);
            }
        }
    } else {
        bool quote = false;
        iscomment = (strdata.Peek() == (int)(unsigned char)CommentChar) && CommentChar != '\0';
        bool wasquoted = false;
        bool bNextCol = false;
        bool isEOL = false;
        char quote_char = st.DefaultQuoteChar;
        bool whitespace = true;

        while (!strdata.EndOfStream()) {
            char cur = (char)strdata.Read();
            int nextI = strdata.Peek();
            char next = nextI < 0 ? '\0' : (char)nextI;

            if (iscomment) {
                if (cur == '\r' && next == '\n') { strdata.Read(); bNextCol = true; isEOL = true; }
                else if (cur == '\n' || cur == '\r') { bNextCol = true; isEOL = true; }
                else if (cur != '\0') value.push_back(cur);
            } else if (!quote) {
                if (cur == quote_char && whitespace) { quote = true; wasquoted = true; whitespace = false; value.clear(); }
                else if (cur == Separator) { bNextCol = true; }
                else if (cur == '\r' && next == '\n') { strdata.Read(); bNextCol = true; isEOL = true; }
                else if (cur == '\n' || cur == '\r') { bNextCol = true; isEOL = true; }
                else if (cur != '\0') value.push_back(cur);

                if (whitespace && cur != ' ') whitespace = false;
            } else {
                if (cur == quote_char && next == quote_char) { value.push_back(cur); strdata.Read(); }
                else if (cur == quote_char) quote = false;
                else {
                    value.push_back(cur);
                    if (cur == '\n' || (cur == '\r' && next != '\n')) ParseCurrentLine++;
                }
            }

            if (bNextCol) {
                std::string csvval = value;
                if (!wasquoted && csvval == st.NullKeyword) csvval = "";
                if (st.TrimValues) csvval = trim(csvval);
                res.push_back(csvval);
                value.clear();
                bNextCol = false;
                wasquoted = false;
                whitespace = true;
            }

            if (isEOL) {
                ParseCurrentLine++;
                break;
            }
            isEOL = false;
        }

        // last value, or file ends with separator so very last value is empty
        if (!value.empty() || (!isEOL && strdata.EndOfStream())) {
            std::string v = value;
            if (!wasquoted && v == st.NullKeyword) v = "";
            if (st.TrimValues) v = trim(v);
            res.push_back(v);
        }
    }

    return res;
}

std::string CsvDefinition::ConstructHeader() const {
    std::string res;
    for (size_t c = 0; c < Fields.size(); c++) {
        std::string nam = Fields[c].Name;
        if (Separator == '\0') {
            if ((int)nam.size() < Fields[c].MaxWidth)
                nam += std::string(Fields[c].MaxWidth - nam.size(), ' ');
            res += nam;
        } else {
            nam = ApplyQuotesToString(nam, Separator, ColumnType::String);
            if (c > 0) res += Separator;
            res += nam;
        }
    }
    return res;
}

std::string CsvDefinition::ConstructLine(const std::vector<std::string> &values,
                                         bool iscomment) const {
    std::string res;
    if (iscomment) {
        if (!values.empty()) res = values[0];
        return res;
    }
    for (size_t c = 0; c < Fields.size(); c++) {
        std::string val = c < values.size() ? values[c] : "";
        if (Separator == '\0') {
            int w = Fields[c].MaxWidth;
            bool numeric = Fields[c].DataType == ColumnType::Integer ||
                           Fields[c].DataType == ColumnType::Decimal;
            if ((int)val.size() < w) {
                std::string pad(w - val.size(), ' ');
                val = numeric ? pad + val : val + pad;
            }
            res += val;
        } else {
            val = ApplyQuotesToString(val, Separator, Fields[c].DataType);
            if (c > 0) res += Separator;
            res += val;
        }
    }
    return res;
}

std::string CsvDefinition::GetColumnWidths(bool abspos) const {
    std::string res;
    int colwidth = 0;
    for (size_t c = 0; c < Fields.size(); c++) {
        if (abspos) colwidth += Fields[c].MaxWidth;
        else        colwidth  = Fields[c].MaxWidth;
        res += std::to_string(colwidth) + (c < Fields.size() - 1 ? ", " : "");
    }
    return res;
}

// ── quote helpers (CsvEdit.cs) ──────────────────────────────────────────────

std::string ApplyQuotesToString(std::string strinput, char separator, ColumnType dataType) {
    const CsvSettings &st = csvSettings();
    int applyCode = st.ReformatQuotes;

    bool apl = strinput.find(separator) != std::string::npos ||
               strinput.find(st.DefaultQuoteChar) != std::string::npos ||
               strinput.find('\r') != std::string::npos ||
               strinput.find('\n') != std::string::npos;

    if (!apl && applyCode > 0) {
        apl = (applyCode == 1 && strinput.find(' ') != std::string::npos)
           || (applyCode == 2 && dataType == ColumnType::String)
           || (applyCode == 3 && dataType != ColumnType::Integer && dataType != ColumnType::Decimal)
           || (applyCode == 4);
    }

    if (apl) {
        if (strinput.find(st.DefaultQuoteChar) != std::string::npos) {
            std::string q1(1, st.DefaultQuoteChar), q2(2, st.DefaultQuoteChar);
            strinput = replaceAll(strinput, q1, q2);
        }
        strinput = st.DefaultQuoteChar + strinput + st.DefaultQuoteChar;
    }
    return strinput;
}

std::string RemoveQuotesToString(const std::string &strinput) {
    const CsvSettings &st = csvSettings();
    std::string res = strinput;
    if (res.size() > 1) {
        if (res.front() == st.DefaultQuoteChar && res.back() == st.DefaultQuoteChar) {
            res = trimChar(res, st.DefaultQuoteChar);
            if (res.find(st.DefaultQuoteChar) != std::string::npos) {
                std::string q1(1, st.DefaultQuoteChar), q2(2, st.DefaultQuoteChar);
                res = replaceAll(res, q2, q1);
            }
        }
    }
    return res;
}
