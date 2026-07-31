#include "CsvEdit.h"
#include "CsvValidate.h"
#include "DateTimeMask.h"
#include <algorithm>

// ── helpers ─────────────────────────────────────────────────────────────────

static std::string padLeft(const std::string &s, size_t w, char c) {
    return s.size() >= w ? s : std::string(w - s.size(), c) + s;
}
static std::string padRight(const std::string &s, size_t w, char c) {
    return s.size() >= w ? s : s + std::string(w - s.size(), c);
}
static std::string replaceAllE(std::string s, const std::string &from, const std::string &to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}
static std::string zeroPad(long long v, int width) {   // C# ToString("D10")-style
    bool neg = v < 0;
    std::string d = std::to_string(neg ? -v : v);
    d = padLeft(d, (size_t)width, '0');
    return neg ? "-" + d : d;
}

std::string formatDateTime(const ParsedDateTime &dt, const std::string &mask) {
    std::string out;
    size_t mp = 0;
    auto num = [](int v, int width) { return padLeft(std::to_string(v), (size_t)width, '0'); };
    while (mp < mask.size()) {
        char t = mask[mp];
        size_t run = 1;
        while (mp + run < mask.size() && mask[mp + run] == t) run++;
        switch (t) {
            case 'y': out += (run >= 4) ? num(dt.year, 4) : num(dt.year % 100, 2); break;
            case 'M': out += num(dt.month, run >= 2 ? 2 : 1); break;
            case 'd': out += num(dt.day, run >= 2 ? 2 : 1); break;
            case 'H': out += num(dt.hour, run >= 2 ? 2 : 1); break;
            case 'm': out += num(dt.minute, run >= 2 ? 2 : 1); break;
            case 's': out += num(dt.second, run >= 2 ? 2 : 1); break;
            case 'f': out += num(dt.millis, 3).substr(0, run); break;
            default:  out += std::string(run, t); break;
        }
        mp += run;
    }
    return out;
}

/// CopyCommentLinesAtStart (CsvDefinition.cs): copy the first SkipLines lines.
static void copyCommentLinesAtStart(CsvDefinition &csvdef, TextReader &strdata,
                                    std::string &sb, const std::string &CRLF) {
    int skip = csvdef.SkipLines >= 0 ? csvdef.SkipLines : 0;
    std::string line;
    while (skip > 0 && !strdata.EndOfStream()) {
        strdata.ReadLine(line);
        sb += line;
        sb += CRLF;
        skip--;
    }
}

/// CopyCommentLine: comment values arrive as a single-element list.
static void copyCommentLine(const std::vector<std::string> &values, std::string &sb) {
    if (!values.empty()) {
        sb += values[0];
        sb += "\n";
    }
}

// ── public operations ───────────────────────────────────────────────────────

std::string CsvEdit::StringToVariable(std::string strinput) {
    return replaceAllE(strinput, " ", "_");
}

std::string CsvEdit::ReformatDataFile(const std::string &text, CsvDefinition &csvdef,
                                      const std::string &reformatSeparator, bool updateSeparator,
                                      const std::string &reformatDatTime,
                                      const std::string &reformatDecimal,
                                      const std::string &replaceCrLf, bool align,
                                      const std::string &CRLF) {
    const CsvSettings &st = csvSettings();

    // align vertically widths
    std::vector<int> alignwidths;
    ColumnType tmpColumnType = ColumnType::String;

    if (align) {
        int applyCode = st.ReformatQuotes;
        for (auto &f : csvdef.Fields) {
            int algwid = f.MaxWidth;
            if (algwid < (int)f.Name.size()) algwid = (int)f.Name.size();
            if ((applyCode == 2 && f.DataType == ColumnType::String) ||
                (applyCode == 3 && f.DataType != ColumnType::Integer &&
                 f.DataType != ColumnType::Decimal) ||
                (applyCode == 4))
                algwid += 2;   // room for the added quotes
            alignwidths.push_back(algwid);
        }
    }

    TextReader strdata(text);
    int linenr = 0;
    std::string datanew;

    char newSep = updateSeparator && !reformatSeparator.empty() ? reformatSeparator[0]
                                                                : csvdef.Separator;

    // to fixed width: no room for a header line (columns can be 1-2 chars wide)
    bool skipheader = updateSeparator && newSep == '\0' && csvdef.ColNameHeader;

    // from fixed width to separated: add a header line
    if (updateSeparator && newSep != '\0' && !csvdef.ColNameHeader) {
        for (size_t c = 0; c < csvdef.Fields.size(); c++) {
            datanew += csvdef.Fields[c].Name;
            if (c < csvdef.Fields.size() - 1) datanew += newSep;
        }
        datanew += "\n";
    }

    copyCommentLinesAtStart(csvdef, strdata, datanew, CRLF);

    while (!strdata.EndOfStream()) {
        bool iscomm = false;
        std::vector<std::string> values = csvdef.ParseNextLine(strdata, iscomm);

        if (iscomm) {
            copyCommentLine(values, datanew);
            continue;
        }
        linenr++;
        if (linenr == 1 && skipheader) continue;

        for (size_t c = 0; c < values.size(); c++) {
            std::string val = values[c];
            int wid = (int)val.size();
            bool alignleft = true;

            if (c < csvdef.Fields.size()) {
                CsvColumn &fld = csvdef.Fields[c];

                if (fld.DataType == ColumnType::DateTime && !reformatDatTime.empty()) {
                    ParsedDateTime dt;
                    if (tryParseDateTimeExact(val, fld.Mask, st.TwoDigitYearMax, dt))
                        val = formatDateTime(dt, reformatDatTime);
                }
                if (fld.DataType == ColumnType::Decimal && !reformatDecimal.empty()) {
                    for (auto &ch : val)
                        if (ch == csvdef.DecimalSymbol) ch = reformatDecimal[0];
                }

                tmpColumnType = fld.DataType;
                wid = align ? alignwidths[c] : fld.MaxWidth;
                alignleft = !(tmpColumnType == ColumnType::Integer ||
                              tmpColumnType == ColumnType::Decimal);
                if (linenr == 1 && csvdef.ColNameHeader) alignleft = true;
            }

            if (newSep == '\0') {
                datanew += alignleft ? padRight(val, wid, ' ') : padLeft(val, wid, ' ');
            } else {
                if (replaceCrLf != "\r\n") {
                    if (val.find('\r') != std::string::npos ||
                        val.find('\n') != std::string::npos) {
                        val = replaceAllE(val, "\r\n", replaceCrLf);
                        val = replaceAllE(val, "\n", replaceCrLf);
                        val = replaceAllE(val, "\r", replaceCrLf);
                    }
                }
                val = ApplyQuotesToString(val, newSep, tmpColumnType);
                if (c > 0) datanew += newSep;
                datanew += align ? (alignleft ? padRight(val, wid, ' ')
                                              : padLeft(val, wid, ' '))
                                 : val;
            }
        }
        datanew += CRLF;
    }

    return datanew;
}

/// SortableString (CsvEdit.cs) — type-aware sortable key.
static std::string SortableString(std::string val, const CsvColumn &csvcol) {
    const CsvSettings &st = csvSettings();
    if (csvcol.isCodedValue) {
        long idx = -1;
        for (size_t i = 0; i < csvcol.CodedList.size(); i++)
            if (csvcol.CodedList[i] == val) { idx = (long)i; break; }
        return zeroPad(1 + idx, 10);
    } else if (csvcol.DataType == ColumnType::Integer || csvcol.DataType == ColumnType::Decimal) {
        if (csvcol.DataType == ColumnType::Decimal) {
            std::string thous = csvcol.DecimalSymbol == '.' ? "," : ".";
            val = replaceAllE(val, thous, "");
            size_t decpos = val.find(csvcol.DecimalSymbol);
            if (decpos == std::string::npos) {
                val += csvcol.DecimalSymbol;
                decpos = val.size() - 1;
            }
            long paddec = csvcol.Decimals - (long)(val.size() - decpos - 1);
            if (paddec > 0) val = padRight(val, val.size() + paddec, '0');
        }
        if (val.find('-') == std::string::npos) {
            return "9" + padLeft(val, (size_t)csvcol.MaxWidth, '0');
        } else {
            std::string ret;
            for (char c : val) {
                char digitinvert = c;
                if (c >= '0' && c <= '9') digitinvert = (char)(48 + 57 - c);
                else if (c == '-') digitinvert = '9';
                ret += digitinvert;
            }
            return "0" + padLeft(ret, (size_t)csvcol.MaxWidth, '9');
        }
    } else if (csvcol.DataType == ColumnType::DateTime) {
        ParsedDateTime dt;
        if (tryParseDateTimeExact(val, csvcol.Mask, st.TwoDigitYearMax, dt))
            return formatDateTime(dt, "yyyyMMddHHmmss") + zeroPad(dt.millis, 3);
        return "00000000000000000";   // invalid date sorts to front
    }
    return val;   // string or any other value
}

std::string CsvEdit::SortData(const std::string &text, CsvDefinition &csvdef,
                              int SortIdx, bool AscDesc, bool OnValue,
                              const std::string &CRLF, std::string *outError) {
    if (outError) outError->clear();
    if (SortIdx > (int)csvdef.Fields.size() - 1) {
        if (outError)
            *outError = "Sort on column index out of bounds, index is " +
                        std::to_string(SortIdx) + " and column count is " +
                        std::to_string(csvdef.Fields.size());
        return text;
    }

    TextReader strdata(text);
    std::vector<std::pair<std::string, std::string>> sortlines;
    int linecount = 0;
    CsvColumn &csvcol = csvdef.Fields[SortIdx];

    std::string sbsort;
    copyCommentLinesAtStart(csvdef, strdata, sbsort, CRLF);

    bool iscomm = false;
    if (csvdef.ColNameHeader) {
        std::vector<std::string> values = csvdef.ParseNextLine(strdata, iscomm);
        sbsort += csvdef.ConstructLine(values, iscomm);
        sbsort += CRLF;
    }

    std::string sortkey;
    while (!strdata.EndOfStream()) {
        std::vector<std::string> values = csvdef.ParseNextLine(strdata, iscomm);

        // comment lines keep the previous sort key, staying roughly in place
        if (!iscomm) {
            std::string val = SortIdx < (int)values.size() ? values[SortIdx] : "";
            sortkey = OnValue ? SortableString(val, csvcol)
                              : padLeft(std::to_string(val.size()), 8, '0');
        }
        std::string line = csvdef.ConstructLine(values, iscomm);
        sortlines.emplace_back(sortkey + zeroPad(linecount, 10), line);
        linecount++;
    }

    std::stable_sort(sortlines.begin(), sortlines.end(),
                     [AscDesc](const std::pair<std::string, std::string> &a,
                               const std::pair<std::string, std::string> &b) {
                         return AscDesc ? a.first < b.first : a.first > b.first;
                     });

    for (auto &rec : sortlines) {
        sbsort += rec.second;
        sbsort += CRLF;
    }
    return sbsort;
}

std::string CsvEdit::ColumnSplit(const std::string &text, CsvDefinition &csvdef,
                                 int ColumnIndex, int SplitCode,
                                 const std::string &Parameter1, const std::string &Parameter2,
                                 bool bRemove, const std::string &CRLF,
                                 CsvDefinition &outNewDef) {
    TextReader strdata(text);
    int linenr = 0;

    // split adds 2 new columns, edit adds 1
    int addmax = SplitCode > 0 ? (SplitCode > 2 ? 2 : 1) : 0;

    int IntPar2 = 0;
    {   // int.TryParse
        try { IntPar2 = std::stoi(Parameter2); } catch (...) { IntPar2 = 0; }
        if (Parameter2.empty()) IntPar2 = 0;
    }
    int IntPar2a = -1 * IntPar2;

    std::string datanew;
    CsvValidate csvvalid;

    CsvDefinition csvnew = csvdef;   // copy to add columns

    if (ColumnIndex < (int)csvdef.Fields.size()) {
        for (int cnew = 0; cnew < addmax; cnew++) {
            int postfix = -1;
            std::string newname = csvnew.GetUniqueColumnName(csvdef.Fields[ColumnIndex].Name,
                                                             &postfix);
            newname = newname + " (" + std::to_string(postfix) + ")";

            int wid = csvdef.Fields[ColumnIndex].MaxWidth;
            if (SplitCode == 5) {   // split on exact position: widths known
                if (IntPar2 > 0) {
                    wid = cnew == 0 ? IntPar2 : csvdef.Fields[ColumnIndex].MaxWidth - IntPar2;
                } else {
                    wid = cnew == 0 ? csvdef.Fields[ColumnIndex].MaxWidth - IntPar2a : IntPar2a;
                }
            }
            CsvColumn newcol(ColumnIndex, newname, wid, ColumnType::String, "");
            csvnew.Fields.insert(csvnew.Fields.begin() + ColumnIndex + cnew + 1, newcol);
        }
    }
    if (bRemove) csvnew.Fields.erase(csvnew.Fields.begin() + ColumnIndex);

    copyCommentLinesAtStart(csvdef, strdata, datanew, CRLF);

    std::vector<std::string> newcols;

    if (csvdef.ColNameHeader) {
        bool ic = false;
        csvdef.ParseNextLine(strdata, ic);   // consume original header
        for (auto &f : csvnew.Fields) newcols.push_back(f.Name);
        datanew += csvnew.ConstructLine(newcols, false);
        datanew += CRLF;
    }

    while (!strdata.EndOfStream()) {
        newcols.clear();
        bool iscomm = false;
        std::vector<std::string> values = csvdef.ParseNextLine(strdata, iscomm);

        if (iscomm) {
            copyCommentLine(values, datanew);
            continue;
        }
        linenr++;

        for (int col = 0; col < (int)values.size(); col++) {
            std::string val = values[col];

            if (col != ColumnIndex || !bRemove) newcols.push_back(val);

            if (col == ColumnIndex && SplitCode > 0) {
                std::string val1 = val, val2;

                if (SplitCode == 1) {
                    // pad with character
                    char pc = Parameter1.empty() ? ' ' : Parameter1[0];
                    val1 = IntPar2 > 0 ? padLeft(val, (size_t)IntPar2, pc)
                                       : padRight(val, (size_t)IntPar2a, pc);
                } else if (SplitCode == 2) {
                    val1 = replaceAllE(val, Parameter1, Parameter2);
                } else if (SplitCode == 3) {
                    // valid/invalid
                    std::string str = csvvalid.EvaluateDataValue(val, csvdef.Fields[ColumnIndex],
                                                                 ColumnIndex);
                    if (!str.empty()) {
                        val1 = "";
                        val2 = val;
                    }
                } else if (SplitCode == 4) {
                    // split on Nth / last-Nth occurrence of a string
                    long pos = -1;
                    if (!Parameter1.empty()) {
                        if (IntPar2 >= 0) {
                            long index = 0;
                            int i = 0;
                            while (++i <= IntPar2) {
                                size_t f = val.find(Parameter1, (size_t)index);
                                if (f == std::string::npos) break;
                                index = (long)f;
                                if (i == IntPar2) { pos = index; break; }
                                index++;
                            }
                        } else {
                            long index = (long)val.size();
                            int i = 0;
                            while (--i >= IntPar2) {
                                size_t f = val.rfind(Parameter1, (size_t)index);
                                if (f == std::string::npos) break;
                                index = (long)f;
                                if (i == IntPar2) { pos = index; break; }
                                if (--index < 0) break;
                            }
                        }
                    }
                    if (pos >= 0) {
                        val1 = val.substr(0, (size_t)pos);
                        val2 = val.substr((size_t)pos + Parameter1.size());
                    }
                } else if (SplitCode == 5) {
                    // split on position
                    if (IntPar2 > 0 && IntPar2 < (int)val.size()) {
                        val1 = val.substr(0, (size_t)IntPar2);
                        val2 = val.substr((size_t)IntPar2);
                    } else if (IntPar2 < 0) {
                        if (IntPar2a < (int)val.size()) {
                            val1 = val.substr(0, val.size() - (size_t)IntPar2a);
                            val2 = val.substr(val.size() - (size_t)IntPar2a);
                        } else {
                            val1 = "";
                            val2 = val;
                        }
                    }
                }

                newcols.push_back(val1);
                if (SplitCode > 2) newcols.push_back(val2);
            }
        }

        datanew += csvnew.ConstructLine(newcols, iscomm);
        datanew += CRLF;
    }

    outNewDef = csvnew;
    return datanew;
}

std::string CsvEdit::SelectColumns(const std::string &text, CsvDefinition &csvdef,
                                   const std::vector<int> &sel_idx, const std::string &CRLF,
                                   CsvDefinition &outNewDef) {
    CsvDefinition csvnew(csvdef.Separator);
    for (size_t c = 0; c < sel_idx.size(); c++) {
        const CsvColumn &coldef = csvdef.Fields[sel_idx[c]];
        csvnew.AddColumn((int)c, coldef.Name, coldef.MaxWidth, coldef.DataType, coldef.Mask);
    }
    csvnew.ColNameHeader = csvdef.ColNameHeader;
    csvnew.DateTimeFormat = csvdef.DateTimeFormat;
    csvnew.DecimalSymbol = csvdef.DecimalSymbol;
    csvnew.NumberDigits = csvdef.NumberDigits;

    TextReader strdata(text);
    int linenr = 0;
    std::string datanew;
    bool skipheader = csvdef.ColNameHeader;

    copyCommentLinesAtStart(csvdef, strdata, datanew, CRLF);

    if (csvdef.ColNameHeader) {
        datanew += csvnew.ConstructHeader();
        datanew += CRLF;
    }

    while (!strdata.EndOfStream()) {
        bool iscomm = false;
        std::vector<std::string> values = csvdef.ParseNextLine(strdata, iscomm);

        if (iscomm) {
            copyCommentLine(values, datanew);
            continue;
        }
        linenr++;
        if (linenr == 1 && skipheader) continue;

        std::vector<std::string> valselect;
        for (int idx : sel_idx)
            if (idx >= 0 && idx < (int)values.size()) valselect.push_back(values[idx]);

        datanew += csvnew.ConstructLine(valselect, false);
        datanew += CRLF;
    }

    outNewDef = csvnew;
    return datanew;
}
