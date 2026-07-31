#include "CsvValidate.h"
#include "DateTimeMask.h"

static std::string trimV(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string trimCharV(const std::string &s, char c) {
    size_t b = 0, e = s.size();
    while (b < e && s[b] == c) b++;
    while (e > b && s[e - 1] == c) e--;
    return s.substr(b, e - b);
}

void CsvValidate::ValidateData(TextReader &strdata, CsvDefinition &csvdef,
                               const std::string &elapsed) {
    const CsvSettings &st = csvSettings();

    // Exception: nothing to validate
    if (csvdef.Fields.size() == 1 && csvdef.Fields[0].DataType == ColumnType::String &&
        csvdef.Fields[0].MaxWidth >= 9999) {
        _log.emplace_back("Nothing to inspect, not tabular data (" + csvdef.Fields[0].Name + ").",
                          -1, -1);
        return;
    }

    // check any empty fieldnames
    std::string nameempty;
    for (size_t i = 0; i < csvdef.Fields.size(); i++)
        if (csvdef.Fields[i].Name.empty()) nameempty += std::to_string(i + 1) + ", ";
    if (!nameempty.empty()) {
        nameempty.erase(nameempty.size() - 2);
        _log.emplace_back("empty column names (column " + nameempty + ")", -1, 0);
    }

    // duplicate column names (first-seen order)
    {
        std::vector<std::pair<std::string, int>> nameCounts;
        for (auto &c : csvdef.Fields) {
            bool found = false;
            for (auto &kv : nameCounts)
                if (kv.first == c.Name) { kv.second++; found = true; break; }
            if (!found) nameCounts.emplace_back(c.Name, 1);
        }
        std::string msgdup;
        for (auto &kv : nameCounts)
            if (kv.second > 1) msgdup += (msgdup.empty() ? "" : ", ") + kv.first;
        if (!msgdup.empty())
            _log.emplace_back("duplicate column names (" + msgdup + ")", -1, 0);
    }

    int counterr = 0;
    int lineCount = 0;

    csvdef.SkipCommentLinesAtStart(strdata);

    while (!strdata.EndOfStream()) {
        bool iscomm = false;
        std::vector<std::string> values = csvdef.ParseNextLine(strdata, iscomm);
        lineCount++;

        if (!iscomm) {
            std::string err;

            // too many or too few columns
            if (values.size() != csvdef.Fields.size()) {
                err += std::string("Too ") +
                       (values.size() > csvdef.Fields.size() ? "many" : "few") + " columns, ";
                counterr++;
            }

            for (size_t i = 0; i < values.size(); i++) {
                std::string val = values[i];

                // adjust for quoted values
                std::string valtrim = trimV(val);
                if (!valtrim.empty() && valtrim[0] == st.DefaultQuoteChar) {
                    val = trimV(val);
                    val = trimCharV(val, st.DefaultQuoteChar);
                }
                if (st.TrimValues) val = trimV(val);

                if (!val.empty()) {
                    if (i < csvdef.Fields.size()) {
                        if (lineCount == 1 && csvdef.ColNameHeader) {
                            // column header
                            if (val != csvdef.Fields[i].Name) {
                                err += "unexpected column name \"" + val + "\", ";
                                counterr++;
                            }
                        } else {
                            std::string evalerr = EvaluateDataValue(val, csvdef.Fields[i], (int)i);
                            if (!evalerr.empty()) {
                                err += evalerr;
                                counterr++;
                            }
                        }
                    }
                }
            }

            if (!err.empty()) {
                err.erase(err.size() - 2);   // remove last ", "
                _log.emplace_back(err, csvdef.ParseCurrentLine, 1);
            }
        }
    }

    std::string line = "Inspected " + std::to_string(lineCount) + " lines, " +
                       (_log.empty() ? "no" : std::to_string(counterr)) +
                       " data errors found, time elapsed " + elapsed;
    _log.emplace_back(line, -1, -1);
}

std::string CsvValidate::EvaluateDataValue(std::string val, const CsvColumn &coldef, int idx) {
    const CsvSettings &st = csvSettings();
    std::string err;
    int colnr = idx + 1;

    // ignore null values
    if (val == st.NullKeyword) val = "";

    if ((int)val.size() > coldef.MaxWidth) {
        err += "Column " + std::to_string(colnr) + " value \"" + val + "\" is too long, ";
    } else {
        bool valid = true;
        std::string typ, msg;

        if (coldef.isCodedValue) {
            bool member = false;
            for (auto &cv : coldef.CodedList)
                if (cv == val) { member = true; break; }
            if (!member) {
                msg = "is not a valid enumeration member";
                valid = false;
            }
        }

        if (valid) {
            switch (coldef.DataType) {
                case ColumnType::Integer:
                    typ = "integer";
                    valid = EvaluateInteger(val);
                    break;
                case ColumnType::Decimal:
                    typ = "decimal";
                    valid = EvaluateDecimal(val, coldef, msg);
                    break;
                case ColumnType::DateTime:
                    typ = "datetime";
                    valid = EvaluateDateTime(val, coldef, msg);
                    break;
                default:
                    break;
            }
        }

        if (!valid) {
            if (msg.empty())
                err += "Column " + std::to_string(colnr) + " value \"" + val +
                       "\" not a valid " + typ + " value, ";
            else
                err += "Column " + std::to_string(colnr) + " value \"" + val + "\" " + msg + ", ";
        }
    }

    return err;
}

bool CsvValidate::EvaluateInteger(std::string val) {
    val = trimV(val);

    bool isNumeric = true;
    int sign = 0;
    for (size_t i = 0; i < val.size(); i++) {
        char ch = val[i];
        if (ch < '0' || ch > '9') {
            if (i == 0 && (ch == '+' || ch == '-')) {
                sign = 1;
            } else {
                isNumeric = false;
                break;
            }
        }
    }

    if ((int)val.size() - sign > csvSettings().IntegerDigitsMax) isNumeric = false;
    return isNumeric;
}

bool CsvValidate::EvaluateDecimal(std::string val, const CsvColumn &coldef, std::string &err) {
    err = "";
    val = trimV(val);

    // decimal columns always have a 2-char sTag; be defensive anyway
    if (coldef.sTag.size() < 2) return false;

    bool isDecimal = true;
    int digits = 0;
    long sign = -1;      // -1 = no sign character
    long decsep = -1;
    long thosep = -1;

    for (long i = (long)val.size() - 1; i >= 0; i--) {
        char ch = val[i];
        if (ch >= '0' && ch <= '9') {
            digits++;
        } else {
            if (ch == '+' || ch == '-') {
                if (i > 0) {
                    isDecimal = false;
                    break;
                }
                sign = i;
            } else if (decsep == -1 && ch == coldef.sTag[1]) {
                // decimal character, cannot appear more than once
                if (thosep != -1) {
                    err += "incorrect position of thousand separator";
                    isDecimal = false;
                    break;
                }
                if (digits > coldef.iTag) {
                    isDecimal = false;
                    if (!err.empty()) err += " and ";
                    err += "has too many decimals";
                }
                decsep = i;
                thosep = i;
            } else if (ch == coldef.sTag[0]) {
                // thousand separator, must be 3+1 characters apart
                if (thosep == -1) thosep = (long)val.size();
                if (thosep - i != 3 + 1) {
                    err += (decsep == -1 ? "incorrect decimal separator"
                                         : "incorrect position of thousand separator");
                    isDecimal = false;
                    break;
                }
                thosep = i;
            } else {
                isDecimal = false;
                break;
            }
        }
    }

    // example ".25" or "-.5"
    if (decsep - sign == 1 && csvSettings().DecimalLeadingZero) {
        err += "missing leading zero not allowed";
        isDecimal = false;
    }

    return isDecimal;
}

bool CsvValidate::EvaluateDateTime(const std::string &val, const CsvColumn &coldef,
                                   std::string &err) {
    err = "";
    ParsedDateTime dateValue;
    bool isDate = false;

    if (tryParseDateTimeExact(val, coldef.Mask, csvSettings().TwoDigitYearMax, dateValue)) {
        isDate = true;
        // check year range (time-only masks parse with year 0 — skip range check)
        if (dateValue.hasDate) {
            int year = dateValue.year;
            if (year < csvSettings().YearMinimum || year > csvSettings().YearMaximum) {
                isDate = false;
                err = "is out of range";
            }
        }
    }

    return isDate;
}

std::string CsvValidate::Report() const {
    std::string sb;
    for (auto &line : _log) {
        if (line.Severity == 0) sb += "** warning";
        if (line.Severity > 0)  sb += "** error";
        if (line.LineNumber > 0) sb += " line " + std::to_string(line.LineNumber);
        if (line.Severity >= 0) sb += ": ";
        sb += line.Message + "\r\n";
    }
    return sb;
}
