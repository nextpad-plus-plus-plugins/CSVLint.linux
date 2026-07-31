#include "CsvConvert.h"
#include "CsvValidate.h"
#include "CsvEdit.h"
#include "DateTimeMask.h"

// ── helpers (C# parity) ─────────────────────────────────────────────────────

static std::string trimC(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
static std::string trimCharC(const std::string &s, char c) {
    size_t b = 0, e = s.size();
    while (b < e && s[b] == c) b++;
    while (e > b && s[e - 1] == c) e--;
    return s.substr(b, e - b);
}
static std::string repl(std::string s, const std::string &from, const std::string &to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

static std::string SQLSafeName(std::string sqlname, int dialect) {
    if (sqlname.find(' ') != std::string::npos || sqlname.find('\'') != std::string::npos) {
        if (dialect == 1) sqlname = "[" + sqlname + "]";                       // MS-SQL
        else {
            std::string q = dialect == 0 ? "`" : "\"";
            sqlname = q + sqlname + q;
        }
    }
    return sqlname;
}

static std::string XMLSafeName(const std::string &name) {
    std::string out;
    for (char c : name)
        out += (isalnum((unsigned char)c) ? c : '_');
    // collapse double underscores
    std::string res;
    for (char c : out) {
        if (c == '_' && !res.empty() && res.back() == '_') continue;
        res += c;
    }
    return res;
}

/// Unquote + trim a raw value the way every converter does.
static std::string cleanValue(std::string colvalue) {
    const CsvSettings &st = csvSettings();
    std::string strtrim = trimC(colvalue);
    if (!strtrim.empty() && strtrim[0] == st.DefaultQuoteChar) {
        colvalue = trimC(colvalue);
        colvalue = trimCharC(colvalue, st.DefaultQuoteChar);
    }
    if (st.TrimValues) colvalue = trimC(colvalue);
    return colvalue;
}

static std::string isoDateTime(const std::string &val, const std::string &mask) {
    ParsedDateTime dt;
    if (tryParseDateTimeExact(val, mask, csvSettings().TwoDigitYearMax, dt))
        return formatDateTime(dt, "yyyy-MM-ddTHH:mm:ss");   // C# "s" format
    return val;
}

static void copyCommentsAtStart(CsvDefinition &csvdef, TextReader &rd, std::string &sb,
                                const std::string &prefix, const std::string &CRLF) {
    int skip = csvdef.SkipLines >= 0 ? csvdef.SkipLines : 0;
    std::string line;
    while (skip > 0 && !rd.EndOfStream()) {
        rd.ReadLine(line);
        sb += prefix + line + CRLF;
        skip--;
    }
}

static std::string joinList(const std::vector<std::string> &v, const std::string &sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) out += (i ? sep : "") + v[i];
    return out;
}

// ── SQL ─────────────────────────────────────────────────────────────────────

std::string CsvConvert::ToSQL(const std::string &text, CsvDefinition &csvdef,
                              const CsvConvertOptions &opt) {
    std::string sb;
    int SQL_BATCH_SIZE = opt.batchSize > 0 ? opt.batchSize : 1000;
    int dialect = opt.sqlDialect;

    int postfix = -1;
    std::string recidname = csvdef.GetUniqueColumnName("_record_number", &postfix);
    if (postfix > 0)
        recidname = SQLSafeName(recidname + " (" + std::to_string(postfix) + ")", dialect);

    std::string TABLE_NAME = opt.tableName.empty()
                                 ? CsvEdit::StringToVariable(opt.fileNameNoExt)
                                 : opt.tableName;
    TABLE_NAME = SQLSafeName(TABLE_NAME, dialect);

    std::string SQL_TYPE = dialect <= 1 ? (dialect == 0 ? "MySQL / MariaDB" : "MS-SQL")
                                        : "PostgreSQL";

    sb += "-- -------------------------------------\r\n";
    for (auto &str : opt.commentLines) sb += "-- " + str + "\r\n";
    sb += "-- SQL type: " + SQL_TYPE + "\r\n";
    sb += "-- -------------------------------------\r\n";

    sb += "CREATE TABLE " + TABLE_NAME + " (\r\n\t";
    switch (dialect) {
        case 1:  sb += recidname + " int IDENTITY(1,1) PRIMARY KEY,\r\n\t"; break;
        case 2:  sb += recidname + " SERIAL PRIMARY KEY,\r\n\t"; break;
        default: sb += recidname + " int AUTO_INCREMENT NOT NULL,\r\n\t"; break;
    }

    std::string cols = "\t", colscom, enumcols1, enumcols2;

    for (size_t r = 0; r < csvdef.Fields.size(); r++) {
        CsvColumn &fld = csvdef.Fields[r];
        std::string sqlname = SQLSafeName(fld.Name, dialect);

        std::string sqltype = "varchar", widunk, comm;
        if (fld.DataType == ColumnType::Integer) sqltype = "integer";
        if (fld.DataType == ColumnType::DateTime)
            sqltype = dialect < 2 ? "datetime" : "timestamp";
        if (fld.DataType == ColumnType::Decimal)
            sqltype = "numeric(" + std::to_string(fld.MaxWidth) + "," +
                      std::to_string(fld.Decimals) + ")";
        if (fld.DataType == ColumnType::String) {
            int wd = fld.MaxWidth;
            if (wd == 0) {
                widunk = " -- width unknown";
                wd = 10;
            }
            sqltype = "varchar(" + std::to_string(wd) + ")";
        }
        if (fld.DataType == ColumnType::DateTime) comm = fld.Mask;

        std::string comma = (r < csvdef.Fields.size() - 1 || dialect == 0) ? "," : "";
        sb += sqlname + " " + sqltype + comma + widunk;
        cols += sqlname;
        if (r < csvdef.Fields.size() - 1) {
            sb += "\r\n\t";
            cols += ",\r\n\t";
        }

        // Enum columns
        if (fld.isCodedValue) {
            std::string enumvals = repl(joinList(fld.CodedList, "\", \""), "'", "''");
            comm = repl(joinList(fld.CodedList, ", "), "'", "''");
            switch (dialect) {
                case 1: {   // MS-SQL
                    std::string chkname = SQLSafeName("CHK_" + fld.Name, dialect);
                    std::string mscolate;
                    if (fld.DataType == ColumnType::String) {
                        enumvals = "'" + repl(enumvals, "\"", "'") + "'";
                        mscolate = " COLLATE Latin1_General_CS_AS";
                    } else {
                        enumvals = repl(enumvals, "\"", "");
                    }
                    enumcols1 += "ALTER TABLE " + TABLE_NAME + " ADD CONSTRAINT " + chkname +
                                 " CHECK(" + sqlname + mscolate + " IN (" + enumvals + "));\r\n";
                    break;
                }
                case 2: {   // PostgreSQL
                    std::string postenum = SQLSafeName("enum_" + fld.Name, dialect);
                    enumcols1 += "CREATE TYPE " + postenum + " AS ENUM ('" +
                                 repl(enumvals, "\"", "'") + "');\r\n";
                    enumcols2 += "ALTER TABLE " + TABLE_NAME + " ALTER COLUMN " + sqlname +
                                 " TYPE " + postenum + " USING (" + sqlname + "::text)::" +
                                 postenum + ";\r\n";
                    if (fld.DataType == ColumnType::Integer) fld.DataType = ColumnType::String;
                    break;
                }
                default: {  // MySQL/MariaDB
                    enumvals = repl(enumvals, "\"", "'");
                    enumcols1 += "ALTER TABLE " + TABLE_NAME + " MODIFY COLUMN " + sqlname +
                                 " ENUM('" + enumvals + "');\r\n";
                    if (fld.DataType == ColumnType::Integer) fld.DataType = ColumnType::String;
                    break;
                }
            }
        }

        std::string str_colheader = repl(fld.Name, "'", "''");
        if (!comm.empty()) comm = " (" + comm + ")";
        switch (dialect) {
            case 1:
                colscom += "EXEC sp_addextendedproperty N'ColLabel', N'" + str_colheader +
                           comm + " comment', N'USER', DBO, N'TABLE', " + TABLE_NAME +
                           ", N'COLUMN', " + sqlname + ";\r\n";
                break;
            case 2:
                colscom += "COMMENT ON COLUMN " + TABLE_NAME + "." + sqlname + " IS '" +
                           str_colheader + comm + " comment';\r\n";
                break;
            default:
                colscom += "MODIFY COLUMN " + sqlname + " " + sqltype + " COMMENT '" +
                           str_colheader + comm + " comment'" +
                           (r < csvdef.Fields.size() - 1 ? "," : ";") + "\r\n";
                break;
        }
    }

    if (dialect == 0) sb += "\r\n\tprimary key(" + recidname + ")";
    sb += "\r\n);\r\n";

    if (!enumcols1.empty())
        sb += "-- Enumeration columns (optional)\r\n/*\r\n" + enumcols1 + enumcols2 + "*/\r\n";

    TextReader strdata(text);
    long lineCount = csvdef.ColNameHeader ? -1 : 0;
    long batchcomm = -1;
    long batchstart = 1;
    long lastend = -1;

    copyCommentsAtStart(csvdef, strdata, sb, "-- ", "\r\n");

    while (!strdata.EndOfStream()) {
        bool iscomm = false;
        std::vector<std::string> list = csvdef.ParseNextLine(strdata, iscomm);

        if (iscomm) {
            if (!list.empty()) sb += "-- " + list[0] + "\n";
            continue;
        }

        if (lineCount >= 0) {
            if (lineCount % SQL_BATCH_SIZE == 0) {
                if (batchcomm > -1 && SQL_BATCH_SIZE > 1) {
                    std::string ins = std::to_string(batchstart) + " - " + std::to_string(lineCount);
                    sb.insert((size_t)batchcomm, ins);
                    batchstart = lineCount + 1;
                }
                if (SQL_BATCH_SIZE > 1 || batchcomm == -1) {
                    sb += "\r\n-- -------------------------------------\r\n";
                    sb += "-- insert records \r\n";
                    batchcomm = (long)sb.size() - 2;
                    sb += "-- -------------------------------------\r\n";
                }
                sb += "INSERT INTO " + TABLE_NAME + " (\r\n" + cols + "\r\n) VALUES\r\n";
            }

            sb += "(";
            for (size_t col = 0; col < csvdef.Fields.size(); col++) {
                std::string colvalue = col < list.size() ? list[col] : "";
                colvalue = cleanValue(colvalue);

                if (colvalue.empty()) {
                    colvalue = "NULL";
                } else if (csvdef.Fields[col].DataType == ColumnType::Decimal) {
                    std::string thous(1, csvdef.Fields[col].DecimalSymbol == '.' ? ',' : '.');
                    colvalue = repl(colvalue, thous, "");
                    for (auto &c : colvalue)
                        if (c == csvdef.Fields[col].DecimalSymbol) c = '.';
                } else if (csvdef.Fields[col].DataType == ColumnType::String ||
                           csvdef.Fields[col].DataType == ColumnType::DateTime) {
                    if (csvdef.Fields[col].DataType == ColumnType::DateTime) {
                        ParsedDateTime dt;
                        if (tryParseDateTimeExact(colvalue, csvdef.Fields[col].Mask,
                                                  csvSettings().TwoDigitYearMax, dt))
                            colvalue = formatDateTime(dt, "yyyy-MM-dd HH:mm:ss");
                    }
                    if (dialect == 0) colvalue = repl(colvalue, "\\", "\\\\");
                    colvalue = repl(colvalue, "'", "''");
                    colvalue = "'" + colvalue + "'";
                }

                sb += (col > 0 ? ", " : "") + colvalue;
            }
            sb += ")" + std::string((lineCount + 1) % SQL_BATCH_SIZE != 0 ? "," : ";") + "\r\n";
            lastend = (long)sb.size() - 3;
        }
        lineCount++;
    }

    if (lastend > -1) {
        sb.erase((size_t)lastend, 1);
        sb.insert((size_t)lastend, ";");
    }
    if (batchcomm > -1)
        sb.insert((size_t)batchcomm,
                  std::to_string(batchstart) + " - " + std::to_string(lineCount));

    sb += "\r\n-- -------------------------------------\r\n";
    sb += "-- Add comments (recommended, especially when archiving)\r\n";
    sb += "-- -------------------------------------\r\n\r\n";

    std::vector<std::string> comment = opt.commentLines;
    comment.insert(comment.begin(), "Table imported using");
    std::string tabcomment = repl(joinList(comment, "\r\n"), "'", "''");
    sb += "-- Table comment\r\n";
    switch (dialect) {
        case 1:
            sb += "EXEC sp_addextendedproperty 'Comment', N'" + tabcomment +
                  "', N'SCHEMA', DBO, N'TABLE', " + TABLE_NAME + ";";
            break;
        case 2:
            sb += "COMMENT ON TABLE " + TABLE_NAME + " IS '" + tabcomment + "';";
            break;
        default:
            sb += "ALTER TABLE " + TABLE_NAME + " COMMENT '" + tabcomment + "';";
            break;
    }

    sb += "\r\n\r\n-- Column comments (optional)\r\n/*\r\n";
    if (dialect == 0) {
        sb += "-- NOTE! MySQL/MariaDB requires repeating the name AND datatype when modifying a column,\r\n";
        sb += "-- so if you have changed any column datatypes make sure they are the same here!\r\n";
        sb += "-- It is safer to add any column comments at the CREATE TABLE statement instead of using MODIFY COLUMN.\r\n";
        sb += "ALTER TABLE " + TABLE_NAME + "\r\n";
    }
    sb += colscom;
    sb += "*/\r\n";

    return sb;
}

// ── XML ─────────────────────────────────────────────────────────────────────

std::string CsvConvert::ToXML(const std::string &text, CsvDefinition &csvdef,
                              const CsvConvertOptions &opt) {
    std::string sb;

    std::string TABLE_NAME = opt.tableName.empty()
                                 ? CsvEdit::StringToVariable(opt.fileNameNoExt)
                                 : opt.tableName;
    TABLE_NAME = XMLSafeName(TABLE_NAME);

    std::vector<std::string> xmlnames;
    for (auto &f : csvdef.Fields) xmlnames.push_back(XMLSafeName(f.Name));

    sb += "<xml>\r\n";
    sb += "\t<!--\r\n";
    for (auto &str : opt.commentLines) sb += "\t\t" + str + "\r\n";
    sb += "\t-->\r\n";

    TextReader strdata(text);
    long lineCount = csvdef.ColNameHeader ? -1 : 0;

    if (csvdef.SkipLines > 0) {
        sb += "\t<!--\n";
        copyCommentsAtStart(csvdef, strdata, sb, "\t", "\r\n");
        sb += "\t-->\n";
    }

    while (!strdata.EndOfStream()) {
        bool iscomm = false;
        std::vector<std::string> list = csvdef.ParseNextLine(strdata, iscomm);

        if (iscomm) {
            if (!list.empty()) sb += "\t<!-- " + list[0] + " -->\n";
            continue;
        }

        if (lineCount >= 0) {
            sb += "\t<" + TABLE_NAME + ">\r\n";

            for (size_t col = 0; col < csvdef.Fields.size(); col++) {
                std::string colvalue = col < list.size() ? list[col] : "";
                const std::string &colname = xmlnames[col];
                colvalue = cleanValue(colvalue);

                if (csvdef.Fields[col].DataType == ColumnType::Decimal) {
                    std::string thous(1, csvdef.Fields[col].DecimalSymbol == '.' ? ',' : '.');
                    colvalue = repl(colvalue, thous, "");
                    for (auto &c : colvalue)
                        if (c == csvdef.Fields[col].DecimalSymbol) c = '.';
                } else if (csvdef.Fields[col].DataType == ColumnType::DateTime &&
                           !colvalue.empty()) {
                    colvalue = isoDateTime(colvalue, csvdef.Fields[col].Mask);
                } else if (csvdef.Fields[col].DataType == ColumnType::String &&
                           !colvalue.empty()) {
                    colvalue = repl(colvalue, "&", "&amp;");
                    colvalue = repl(colvalue, "<", "&lt;");
                    colvalue = repl(colvalue, ">", "&gt;");
                    colvalue = repl(colvalue, "\b", "&#09;");
                    colvalue = repl(colvalue, "\f", "&#0C;");
                    colvalue = repl(colvalue, "\n", "&#10;");
                    colvalue = repl(colvalue, "\r", "&#13;");
                    colvalue = repl(colvalue, "\t", "&#09;");
                    colvalue = repl(colvalue, "\"", "&quote;");
                    colvalue = repl(colvalue, "'", "&apos;");
                }

                if (colvalue.empty())
                    sb += "\t\t<" + colname + "/>\r\n";
                else
                    sb += "\t\t<" + colname + ">" + colvalue + "</" + colname + ">\r\n";
            }
            sb += "\t</" + TABLE_NAME + ">\r\n";
        }
        lineCount++;
    }

    sb += "</xml>\r\n";
    return sb;
}

// ── JSON ────────────────────────────────────────────────────────────────────

std::string CsvConvert::ToJSON(const std::string &text, CsvDefinition &csvdef,
                               const CsvConvertOptions &opt) {
    std::string sb;
    CsvValidate csveval;

    sb += "{\r\n";
    // JSON has no comments — ScriptInfo becomes "key": "value" strings
    for (auto &str : opt.commentLines)
        sb += "\t\"" + repl(str, ": ", "\": \"") + "\",\r\n";
    sb += "\t\"JSONdata\":[";

    TextReader strdata(text);
    long lineCount = csvdef.ColNameHeader ? -1 : 0;

    csvdef.SkipCommentLinesAtStart(strdata);

    while (!strdata.EndOfStream()) {
        bool iscomm = false;
        std::vector<std::string> list = csvdef.ParseNextLine(strdata, iscomm);
        if (iscomm) continue;

        if (lineCount >= 0) {
            if (lineCount > 0) sb += ",";
            sb += "\r\n\t\t{\r\n";

            for (size_t col = 0; col < csvdef.Fields.size(); col++) {
                std::string colvalue = col < list.size() ? list[col] : "";
                std::string colname = repl(csvdef.Fields[col].Name, "\"", "\\\"");
                colvalue = cleanValue(colvalue);

                if (csvdef.Fields[col].DataType == ColumnType::Integer) {
                    if (!csveval.EvaluateInteger(colvalue))
                        colvalue = "\"" + colvalue + "\"";
                } else if (csvdef.Fields[col].DataType == ColumnType::Decimal) {
                    std::string dummy;
                    if (!csveval.EvaluateDecimal(colvalue, csvdef.Fields[col], dummy)) {
                        colvalue = "\"" + colvalue + "\"";
                    } else {
                        std::string thous(1, csvdef.Fields[col].DecimalSymbol == '.' ? ',' : '.');
                        colvalue = repl(colvalue, thous, "");
                        for (auto &c : colvalue)
                            if (c == csvdef.Fields[col].DecimalSymbol) c = '.';
                    }
                } else if (csvdef.Fields[col].DataType == ColumnType::DateTime &&
                           !colvalue.empty()) {
                    colvalue = isoDateTime(colvalue, csvdef.Fields[col].Mask);
                } else if (csvdef.Fields[col].DataType == ColumnType::String) {
                    colvalue = repl(colvalue, "\\", "\\\\");
                    colvalue = repl(colvalue, "\b", "\\b");
                    colvalue = repl(colvalue, "\f", "\\f");
                    colvalue = repl(colvalue, "\n", "\\n");
                    colvalue = repl(colvalue, "\r", "\\r");
                    colvalue = repl(colvalue, "\t", "\\t");
                    colvalue = repl(colvalue, "\"", "\\\"");
                }

                if (csvdef.Fields[col].DataType == ColumnType::String ||
                    csvdef.Fields[col].DataType == ColumnType::DateTime)
                    colvalue = "\"" + colvalue + "\"";

                std::string comma = col < csvdef.Fields.size() - 1 ? "," : "";
                // C# parity: empty ints/decimals stay "" and are skipped;
                // empty strings were quoted to "\"\"" above and ARE emitted.
                if (!colvalue.empty())
                    sb += "\t\t\t\"" + colname + "\": " + colvalue + comma + "\r\n";
            }
            sb += "\t\t}";
        }
        lineCount++;
    }

    sb += "\r\n\t]\r\n}\r\n";
    return sb;
}
