#include "CsvGenerateCode.h"
#include <cctype>
#include <cstring>

// ── helpers ─────────────────────────────────────────────────────────────────

static std::string replG(std::string s, const std::string &from, const std::string &to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}
static std::string padRightG(const std::string &s, size_t w, char c) {
    return s.size() >= w ? s : s + std::string(w - s.size(), c);
}
static std::string padLeftG(const std::string &s, size_t w, char c) {
    return s.size() >= w ? s : std::string(w - s.size(), c) + s;
}
static std::string joinG(const std::vector<std::string> &v, const std::string &sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) out += (i ? sep : "") + v[i];
    return out;
}

/// C# DateMaskStandardToCstr: "M/d/yyyy HH:m:s" -> "%m/%d/%Y %H:%M:%S".
static std::string DateMaskStandardToCstr(std::string mask) {
    mask = replG(mask, "HH", "H");
    mask = replG(mask, "H", "%H");
    mask = replG(mask, "mm", "m");
    mask = replG(mask, "m", "n");   // minutes via temporary placeholder
    mask = replG(mask, "ss", "s");
    mask = replG(mask, "s", "%S");
    mask = replG(mask, "yy", "y");
    mask = replG(mask, "yy", "y");
    mask = replG(mask, "yy", "y");
    mask = replG(mask, "y", "%Y");
    mask = replG(mask, "MM", "M");
    mask = replG(mask, "M", "%m");
    mask = replG(mask, "dd", "d");
    mask = replG(mask, "d", "%d");
    mask = replG(mask, "n", "%M");
    return mask;
}

static void ScriptDisclaimer(std::string &sb) {
    sb += "#\r\n# NOTE:\r\n";
    sb += "# This is a generated script and it doesn't handle all potential data errors.\r\n";
    sb += "# The script is meant as a starting point for processing your data files.\r\n";
    sb += "# Adjust and expand the script for your specific data processing needs.\r\n";
    sb += "# Always back-up your data files to prevent data loss.\r\n\r\n";
}

static void ScriptHeader(std::string &sb, const std::string &stext) {
    sb += "# --------------------------------------\r\n";
    sb += "# " + stext + "\r\n";
    sb += "# --------------------------------------\r\n";
}

static std::string displaySeparator(const CsvDefinition &csvdef) {
    std::string separator = csvdef.Separator == '\0' ? "{fixed-width}"
                                                     : std::string(1, csvdef.Separator);
    if (separator == "\t") separator = "\\t";
    return separator;
}

static std::string regexReplaceNonAlnum(const std::string &name, char to,
                                        const char *extraAllowed = "") {
    std::string out;
    for (char c : name) {
        bool ok = isalnum((unsigned char)c) || strchr(extraAllowed, c);
        out += ok ? c : to;
    }
    return out;
}

// ── schema.ini ──────────────────────────────────────────────────────────────

std::string CsvGenerateCode::SchemaIni(CsvDefinition &csvdef, const CsvGenerateOptions &opt) {
    std::string comment;
    if (csvdef.Separator == '\0')
        comment += "\r\n; Fixed Length positions " + csvdef.GetColumnWidths(true) + "\r\n";

    bool notsup = csvdef.SkipLines > 0 || csvdef.CommentChar != '\0';
    for (auto &col : csvdef.Fields) {
        if (col.isCodedValue ||
            (col.DataType == ColumnType::Decimal && col.Decimals != csvdef.NumberDigits) ||
            (col.DataType == ColumnType::DateTime && col.Mask != csvdef.DateTimeFormat))
            notsup = true;
    }
    if (notsup)
        comment += "\r\n; NOTE: some CSV Lint features are not supported by the ODBC Text "
                   "driver, that is why these lines are commented out";

    return "[" + opt.fileName + "]\r\n" + csvdef.GetIniLines() + comment;
}

// ── schema JSON ─────────────────────────────────────────────────────────────

std::string CsvGenerateCode::SchemaJSON(CsvDefinition &csvdef, const CsvGenerateOptions &opt) {
    std::string separator = displaySeparator(csvdef);
    std::string sb;

    sb += "{\r\n";
    sb += "\t\"url\": \"" + opt.fileName + "\",\r\n";

    sb += "\t\"dialect\": {";
    if (csvdef.Separator == '\0')
        sb += "\r\n\t\t\"columnpositions\": [0, " + csvdef.GetColumnWidths(true) + "]";
    else
        sb += "\r\n\t\t\"delimiter\": \"" + separator + "\"";
    sb += std::string(",\r\n\t\t\"header\": \"") + (csvdef.ColNameHeader ? "true" : "false") + "\"";
    if (csvdef.SkipLines > 0)
        sb += ",\r\n\t\t\"skipRows\": \"" + std::to_string(csvdef.SkipLines) + "\"";
    if (csvdef.CommentChar != '\0')
        sb += std::string(",\r\n\t\t\"commentPrefix\": \"") + csvdef.CommentChar + "\"";
    sb += "\r\n\t},\r\n";

    sb += "\t\"tableSchema\": {\r\n";
    sb += "\t\t\"columns\": [";

    for (size_t c = 0; c < csvdef.Fields.size(); c++) {
        const CsvColumn &coldef = csvdef.Fields[c];

        std::string dattyp = "string", mask, dec;
        std::string len = std::to_string(coldef.MaxWidth);
        switch (coldef.DataType) {
            case ColumnType::DateTime:
                mask = coldef.Mask;
                dattyp = std::string(mask.find('y') != std::string::npos ? "date" : "") +
                         (mask.find('H') != std::string::npos ? "time" : "");
                break;
            case ColumnType::Integer:
                dattyp = "integer";
                break;
            case ColumnType::Decimal:
                dattyp = "number";
                mask = "#0" + std::string(1, coldef.DecimalSymbol) +
                       std::string((size_t)coldef.Decimals, '0');
                dec = std::string(1, coldef.DecimalSymbol);
                break;
            default:
                break;
        }

        if (c > 0) sb += ",";
        sb += "\r\n\t\t\t{\r\n";
        sb += "\t\t\t\t\"name\": \"" + coldef.Name + "\"";

        if (coldef.isCodedValue) {
            std::string codedlist = joinG(coldef.CodedList, "|");
            sb += ",\r\n\t\t\t\t\"datatype\": {";
            sb += "\r\n\t\t\t\t\t\"base\": \"" + dattyp + "\"";
            sb += ",\r\n\t\t\t\t\t\"format\": \"" + codedlist + "\"";
            sb += "\r\n\t\t\t\t}";
        } else if (!mask.empty() && !dec.empty()) {
            sb += ",\r\n\t\t\t\t\"datatype\": {";
            sb += "\r\n\t\t\t\t\t\"base\": \"" + dattyp + "\"";
            sb += ",\r\n\t\t\t\t\t\"length\": \"" + len + "\"";
            sb += ",\r\n\t\t\t\t\t\"format\": {";
            sb += "\r\n\t\t\t\t\t\t\"decimalChar\": \"" + dec + "\"";
            sb += ",\r\n\t\t\t\t\t\t\"pattern\": \"" + mask + "\"";
            sb += "\r\n\t\t\t\t\t}";
            sb += "\r\n\t\t\t\t}";
        } else if (!mask.empty()) {
            sb += ",\r\n\t\t\t\t\"datatype\": {";
            sb += "\r\n\t\t\t\t\t\"base\": \"" + dattyp + "\"";
            sb += ",\r\n\t\t\t\t\t\"length\": \"" + len + "\"";
            sb += ",\r\n\t\t\t\t\t\"format\": \"" + mask + "\"";
            sb += "\r\n\t\t\t\t}";
        } else {
            sb += ",\r\n\t\t\t\t\"datatype\": \"" + dattyp + "\"";
            sb += ",\r\n\t\t\t\t\"length\": \"" + len + "\"";
        }
        sb += "\r\n\t\t\t}";
    }

    sb += "\r\n\t\t]\r\n";
    sb += "\t}\r\n";
    sb += "}\r\n";
    return sb;
}

// ── data dictionary CSV ─────────────────────────────────────────────────────

std::string CsvGenerateCode::DatadictionaryCSV(CsvDefinition &csvdef,
                                               const CsvGenerateOptions &opt) {
    CsvDefinition datadict(',');
    datadict.AddColumn("Nr", 8, ColumnType::Integer);
    datadict.AddColumn("ColumnName", 1000, ColumnType::String);
    datadict.AddColumn("DataType", 1000, ColumnType::String);
    datadict.AddColumn("Width", 8, ColumnType::Integer);
    datadict.AddColumn("Decimals", 8, ColumnType::Integer);
    datadict.AddColumn("Mask", 1000, ColumnType::String);
    datadict.AddColumn("Enumeration", 9999, ColumnType::String);

    std::string sb = "Nr,ColumnName,DataType,Width,Decimals,Mask,Enumeration\r\n";
    std::vector<std::string> sl;

    for (size_t c = 0; c < csvdef.Fields.size(); c++) {
        const CsvColumn &coldef = csvdef.Fields[c];
        std::string dattyp = "String", mask, dec, enumvals;
        std::string colwid = std::to_string(coldef.MaxWidth);
        switch (coldef.DataType) {
            case ColumnType::DateTime:
                mask = coldef.Mask;
                dattyp = std::string(mask.find('y') != std::string::npos ? "Date" : "") +
                         (mask.find('H') != std::string::npos ? "Time" : "");
                break;
            case ColumnType::Integer:
                dattyp = "Integer";
                break;
            case ColumnType::Decimal:
                dattyp = "Decimal";
                mask = "#0" + std::string(1, coldef.DecimalSymbol) +
                       std::string((size_t)coldef.Decimals, '0');
                dec = std::to_string(coldef.Decimals);
                break;
            default:
                break;
        }
        if (coldef.isCodedValue) enumvals = joinG(coldef.CodedList, "|");

        sl = {std::to_string(c + 1), coldef.Name, dattyp, colwid, dec, mask, enumvals};
        sb += datadict.ConstructLine(sl, false) + "\r\n";
    }
    return sb;
}

// ── Python pandas ───────────────────────────────────────────────────────────

std::string CsvGenerateCode::PythonPanda(CsvDefinition &csvdef, const CsvGenerateOptions &opt) {
    std::string sb;
    sb += "# Python - read csv with datatypes\r\n";
    for (auto &str : opt.commentLines) sb += "# " + str + "\r\n";
    ScriptDisclaimer(sb);

    sb += "# Library\r\nimport os\r\nimport numpy as np\r\nimport pandas as pd\r\n\r\n";
    sb += "# working directory and filename\r\n";
    sb += "os.chdir(\"" + opt.fileDir + "\")\r\n";
    sb += "filename = \"" + opt.filePath + "\"\r\n\r\n";

    std::string col_names, col_types, col_dates, col_datef, col_ints, col_enums;
    std::string exampleDate, r_dec;

    for (size_t c = 0; c < csvdef.Fields.size(); c++) {
        const CsvColumn &coldef = csvdef.Fields[c];
        const std::string &colname = coldef.Name;
        std::string comma = c < csvdef.Fields.size() - 1 ? "," : "";

        col_names += "    '" + colname + "'" + comma + "\r\n";

        if (coldef.isCodedValue) {
            std::string enumvals = joinG(coldef.CodedList, "\", \"");
            if (coldef.DataType == ColumnType::String) enumvals = "\"" + enumvals + "\"";
            else enumvals = replG(enumvals, "\"", "");
            col_enums += "    \"" + coldef.Name + "\": [" + enumvals + "],\r\n";
        }

        switch (coldef.DataType) {
            case ColumnType::DateTime: {
                std::string msk = DateMaskStandardToCstr(coldef.Mask);
                col_datef += "'" + msk + "', ";
                col_dates += "'" + colname + "', ";
                if (exampleDate.empty()) exampleDate = colname;
                break;
            }
            case ColumnType::Integer:
                col_types += "    \"" + colname + "\": np.int64" + comma + "\r\n";
                col_ints += "#df['" + colname + "'] = df['" + colname +
                            "'].astype(str).str.rstrip('0').str.rstrip('.')\r\n";
                break;
            case ColumnType::Decimal:
                col_types += "    \"" + colname + "\": str" + comma + " # numeric\n";
                if (r_dec.empty()) r_dec = std::string(1, coldef.DecimalSymbol);
                break;
            default:
                col_types += "    \"" + colname + "\": str" + comma + "\r\n";
                break;
        }
    }

    sb += "# column datatypes\r\n";
    sb += "# NOTE: using colClasses parameter doesn't work when for example integers are in quotes etc.\r\n";
    sb += "# and read.csv will mostly interpret datatypes correctly anyway\r\n";

    if (r_dec.empty()) r_dec = ".";

    std::string nameparam = ", header=0";
    if (!csvdef.ColNameHeader) {
        sb += "col_names = [\r\n" + col_names + "]\r\n";
        nameparam = ", names=col_names, header=None";
    }
    if (csvdef.SkipLines > 0) nameparam += ", skiprows=" + std::to_string(csvdef.SkipLines);
    if (csvdef.CommentChar != '\0')
        nameparam += std::string(", comment='") + csvdef.CommentChar + "'";

    sb += "col_types = {\r\n" + col_types + "}\r\n";

    std::string separator = displaySeparator(csvdef);

    if (!col_dates.empty()) {
        col_datef.erase(col_datef.size() - 2);
        col_dates.erase(col_dates.size() - 2);
        sb += "\r\n";
        sb += "# datetime columns; " + col_datef + "\r\n";
        sb += "col_dates = [" + col_dates + "]\r\n\r\n";
        col_dates = ", parse_dates=col_dates";
    }

    if (csvdef.Separator == '\0') {
        sb += "# fixed width, positions " + csvdef.GetColumnWidths(true) + "\r\n";
        sb += "col_widths = [" + csvdef.GetColumnWidths(false) + "]\r\n";
        sb += "df = pd.read_fwf(filename, decimal='" + r_dec + "'" + nameparam + col_dates +
              ", dtype=col_types, widths=col_widths)\r\n\r\n";
    } else {
        sb += "# read csv file\r\n";
        sb += "#df = pd.read_csv(filename, sep='" + separator + "', decimal='" + r_dec + "'" +
              nameparam + col_dates + ", dtype=col_types)\r\n";
        sb += "df = pd.read_csv(filename, sep='" + separator + "', decimal='" + r_dec + "'" +
              nameparam + col_dates + ")\r\n\r\n";
    }

    if (!col_ints.empty()) {
        sb += "# NOTE: Python treats NaN values as float, thus columns with Int64+NaNs are converted to float,\r\n";
        sb += "# you can convert them to string and then rstrip to undo the float '.0' parts\r\n";
        sb += col_ints + "\r\n";
    }
    if (!col_dates.empty())
        sb += "# NOTE: Python treats datetime columns that also have NaN/string values as string\r\n\r\n";
    if (!col_ints.empty() || !col_dates.empty())
        sb += "# double check datatypes\r\nprint(df.dtypes)\r\n\r\n";

    if (!col_enums.empty()) {
        col_enums.erase(col_enums.size() - 3);
        sb += "# enumeration allowed values\r\nallowed_values = {\r\n" + col_enums + "\r\n}\r\n\r\n";
        sb += "# check enumeration\r\ndf_invalid = {\r\n    column_name: df[~df[column_name].isin(allowed_values)]\r\n                 .value_counts(subset = column_name)\r\n                 .to_frame().reset_index(names = \"Invalid_value\")\r\n    for column_name, allowed_values in allowed_values.items()\r\n}\r\ndf_chk = pd.concat(df_invalid, names = (\"Column_name\", None)).droplevel(1)\r\n";
        sb += "if not df_chk.empty:\r\n    print(\"Invalid values found:\")\r\n    print(df_chk)\r\n\r\n";
    }

    if (exampleDate.empty()) exampleDate = "myDateField";
    std::string yr = std::to_string(opt.exampleYear);
    sb += "# Remove or uncomment the script parts below to filter, transform, merge as needed\r\n\r\n";

    ScriptHeader(sb, "Data filter and sort suggestions");
    sb += "# filter on value or date range\r\n";
    sb += "#df = df[(df[\"date_column\"] == \"test\")]\r\n";
    sb += "#df = df[(df[\"" + exampleDate + "\"] >= \"" + yr + "-01-01\") & (df[\"" +
          exampleDate + "\"] < \"" + yr + "-07-01\")]\r\n\r\n";
    sb += "# sort on column\r\n";
    sb += "#df = df.sort_values(\"" + exampleDate +
          "\") # or descending; .sort_values(by=[\"" + exampleDate + "\"], ascending=False)\r\n\r\n";
    sb += "# Reorder or remove columns (edit code below)\r\n";
    sb += "df = df[[\r\n" + col_names + "]]\r\n\r\n";

    ScriptHeader(sb, "Data transformation suggestions");
    sb += "# Date to string example, format as MM/dd/yyyy\r\n";
    sb += "#df['" + exampleDate + "'] = df['" + exampleDate + "'].dt.strftime('%m/%d/%Y')\r\n\r\n";
    sb += "# Replace labels with codes example, when column contains 'Yes' or 'No' replace with '1' or '0'\r\n";
    sb += "#lookuplist = {'Yes': 1, 'No': 0}\r\n";
    sb += "#df['yesno_int'] = df['yesno_str'].map(lookuplist)\r\n\r\n";
    sb += "# Calculate new values example\r\n";
    sb += "#df['bmi_calc'] = round(df['weight'] / (df['height'] / 100) ** 2, 1)\r\n";
    sb += "#df['center_patient'] = df['centercode'].str.slice(0, 2) + '-' + df['patientcode'].map(str) # '01-123' etc\r\n\r\n";

    ScriptHeader(sb, "Data merge example");
    sb += "# Merge dataframes example, to join on multiple columns use a list, for example: on=['patient_id', 'center_id']\r\n";
    sb += "#merged_df = pd.merge(df1, df2, how='left', on='patient_id') # same key column name\r\n";
    sb += "#merged_df = pd.merge(df1, df2, how='left', left_on='df1 key', right_on='df2 id') # different key column names\r\n\r\n";

    if (csvdef.Separator == '\0') separator = ",";
    sb += "# csv write new output\r\n";
    sb += "filenew = \"output.txt\"\r\n";
    sb += "df.to_csv(filenew, sep='" + separator +
          "', decimal=',', na_rep='', header=True, index=False, encoding='utf-8')\r\n";

    return sb;
}

// ── R script ────────────────────────────────────────────────────────────────

std::string CsvGenerateCode::RScript(CsvDefinition &csvdef, const CsvGenerateOptions &opt) {
    std::string sb;
    sb += "# R-script - read csv with datatypes\r\n";
    for (auto &str : opt.commentLines) sb += "# " + str + "\r\n";
    ScriptDisclaimer(sb);

    sb += "# Library\r\nlibrary(dplyr)\r\n\r\n";
    sb += "setwd(\"" + opt.fileDir + "\")\r\n\r\n";
    sb += "filename = \"" + opt.filePath + "\"\r\n\r\n";
    sb += "# column datatypes\r\n";
    sb += "# NOTE: using colClasses parameter doesn't work when for example integers are in quotes etc.\r\n";
    sb += "# and read.csv will mostly interpret datatypes correctly anyway\r\n";

    std::string col_names = "c(", col_types = "c(", col_dates, col_numbs, col_enums;
    std::string exampleDate, r_dec;

    for (size_t c = 0; c < csvdef.Fields.size(); c++) {
        const CsvColumn &coldef = csvdef.Fields[c];
        std::string colname = regexReplaceNonAlnum(coldef.Name, '.', "_");
        std::string comma = c < csvdef.Fields.size() - 1 ? "," : ")";
        std::string indent = c > 0 ? "              " : "";

        col_names += indent + "\"" + colname + "\"" + comma + "\r\n";
        if (c > 0) col_types += "              ";

        if (coldef.isCodedValue) {
            std::string enumvals = joinG(coldef.CodedList, "\", \"");
            if (coldef.DataType == ColumnType::String) enumvals = "\"" + enumvals + "\"";
            else enumvals = replG(enumvals, "\"", "");
            col_enums += "  \"" + coldef.Name + "\" = c(" + enumvals + "),\r\n";
        }

        switch (coldef.DataType) {
            case ColumnType::DateTime: {
                col_types += "\"" + colname + "\" = \"character\"" + comma + " # " +
                             coldef.Mask + "\r\n";
                std::string msk = DateMaskStandardToCstr(coldef.Mask);
                std::string rtype = msk.find('H') == std::string::npos ? "Date" : "POSIXct";
                col_dates += "df$" + colname + " <- as." + rtype + "(df$" + colname +
                             ", format=\"" + msk + "\")\r\n";
                if (exampleDate.empty()) exampleDate = colname;
                break;
            }
            case ColumnType::Integer:
                col_types += "\"" + colname + "\" = \"integer\"" + comma + "\r\n";
                break;
            case ColumnType::Decimal:
                col_types += "\"" + colname + "\" = \"character\"" + comma + " # numeric\n";
                col_numbs += "df$" + colname + " <- as.numeric(df$" + colname + ")\r\n";
                if (r_dec.empty()) r_dec = std::string(1, coldef.DecimalSymbol);
                break;
            default:
                col_types += "\"" + colname + "\" = \"character\"" + comma + "\r\n";
                break;
        }
    }

    if (r_dec.empty()) r_dec = ".";

    std::string nameparam;
    if (!csvdef.ColNameHeader) {
        sb += "colNames <- " + col_names + "\r\n";
        nameparam = "col.name=colNames, ";
    }
    if (csvdef.SkipLines > 0) nameparam += "skip=" + std::to_string(csvdef.SkipLines) + ", ";
    if (csvdef.CommentChar != '\0')
        nameparam += std::string("comment.char=\"") + csvdef.CommentChar + "\", ";

    sb += "colTypes <- " + col_types + "\r\n";

    std::string separator = displaySeparator(csvdef);
    std::string header = csvdef.ColNameHeader ? "TRUE" : "FALSE";

    if (csvdef.Separator == '\0') {
        sb += "# fixed width, positions " + csvdef.GetColumnWidths(true) + "\r\n";
        sb += "colWidths <- c(" + csvdef.GetColumnWidths(false) + ")\r\n";
        sb += "df <- read.fwf(filename, " + nameparam +
              "colClasses=colTypes, width=colWidths, stringsAsFactors=FALSE, "
              "comment.char='', header=" + header + ")\r\n\r\n";
    } else {
        sb += "# read csv file\r\n";
        sb += "#df <- read.csv(filename, sep='" + separator + "', dec=\"" + r_dec + "\", " +
              nameparam + "colClasses=colTypes, header=" + header + ")\r\n";
        sb += "df <- read.csv(filename, sep='" + separator + "', dec=\"" + r_dec + "\", " +
              nameparam + "header=" + header + ")\r\n\r\n";
    }

    if (!col_dates.empty()) {
        sb += "# datetime values\r\n";
        sb += "# NOTE: any datetime formatting errors will result in empty/NA values without any warning\r\n";
        sb += col_dates + "\r\n";
    }
    if (!col_numbs.empty()) {
        sb += "# numeric values\r\n";
        sb += "# NOTE: the error message \"NAs introduced by coercion\" means there are decimal formatting errors\r\n";
        sb += col_numbs + "\r\n";
    }

    if (!col_enums.empty()) {
        col_enums.erase(col_enums.size() - 3);
        sb += "# enumeration allowed values\r\nallowed_values <- list(\r\n" + col_enums + "\r\n)\r\n\r\n";
        sb += "# check enumeration\r\ndf_invalid <- lapply(names(allowed_values), function(column_name) {\r\n  df[[column_name]] <- as.character(df[[column_name]])  # Convert values to strings\r\n  invalid_values <- df[!df[[column_name]] %in% as.character(allowed_values[[column_name]]), column_name]\r\n  invalid_counts <- table(invalid_values)\r\n  data.frame(Column_name = column_name, Invalid_value = names(invalid_counts), Count = as.numeric(invalid_counts), stringsAsFactors = FALSE)\r\n})\r\n";
        sb += "df_chk <- bind_rows(df_invalid)\r\nif (nrow(df_chk) > 0) {\r\n  cat(\"Invalid values found:\\n\")\r\n  print(df_chk)\r\n}\r\n\r\n";
    }

    if (exampleDate.empty()) exampleDate = "myDateField";
    std::string yr = std::to_string(opt.exampleYear);
    sb += "# Remove or uncomment the script parts below to filter, transform, merge as needed\r\n\r\n";

    ScriptHeader(sb, "Data filter and sort suggestions");
    sb += "# filter on value or date range\r\n";
    sb += "#filtered_df <- df[df$study == \"123\"), ]\r\n";
    sb += "#filtered_df <- df[df$" + exampleDate + " >= as.Date(\"" + yr +
          "-01-01\") & df$" + exampleDate + " < as.Date(\"" + yr + "-07-01\"), ]\r\n\r\n";
    sb += "# sort on column\r\n";
    sb += "#df <- df[order(df$" + exampleDate + "), ] # or descending: order(df$" +
          exampleDate + ", decreasing=TRUE)\r\n\r\n";
    sb += "# Reorder or remove columns (edit code below)\r\n";
    sb += "colOrder <- " + col_names;
    sb += "df <- df[, colOrder]\r\n\r\n";

    ScriptHeader(sb, "Data transformation suggestions");
    sb += "# Date to string example, format as MM/dd/yyyy\r\n";
    sb += "#df$" + exampleDate + " <- format(df$" + exampleDate + ", \"%m/%d/%Y\")\r\n\r\n";
    sb += "# Replace labels with codes example, when column contains 'Yes' or 'No' replace with '1' or '0'\r\n";
    sb += "#lookuplist <- data.frame(\"code\" = c(\"0\", \"1\"),\r\n";
    sb += "#                    \"label\" = c(\"No\", \"Yes\") )\r\n";
    sb += "#df$yesno_int <- lookuplist$code[match(df$yesno_str, lookuplist$label)]\r\n\r\n";
    sb += "# Calculate new values example\r\n";
    sb += "#df$bmi_calc <- df$weight / (df$height / 100) ^ 2\r\n";
    sb += "#df$center_patient <- paste(substr(df$centercode, 1, 2), '-', df$patientcode) # '01-123' etc.\r\n\r\n";

    ScriptHeader(sb, "Merge examples");
    sb += "# Merge dataframes example, all.x=TRUE meaning take all df1 records(=x) and left outer join with df2(=y)\r\n";
    sb += "#merged_df <- merge(df1, df2, all.x=TRUE, by=c('patient_id')) # same key column name\r\n";
    sb += "#merged_df <- merge(df1, df2, all.x=TRUE, by.x=c('df1 key'), by.y=c('df2 id')) # different key column name\r\n\r\n";

    sb += "# csv write new output\r\n";
    sb += "filenew = \"output.txt\"\r\n";
    sb += "write.table(df, file=filenew, sep=\";\", dec=\",\", na=\"\", row.names=FALSE)\r\n";

    return sb;
}

// ── PowerShell ──────────────────────────────────────────────────────────────

std::string CsvGenerateCode::PowerShell(CsvDefinition &csvdef, const CsvGenerateOptions &opt) {
    const CsvSettings &st = csvSettings();
    std::string sb;
    sb += "# PowerShell - read csv with datatypes\r\n";
    for (auto &str : opt.commentLines) sb += "# " + str + "\r\n";
    ScriptDisclaimer(sb);

    sb += "# working directory and filename\r\n";
    sb += "$pathname = \"" + opt.fileDir + "/\"\r\n";
    sb += "$filename = $pathname + \"" + opt.fileName + "\"\r\n\r\n";

    std::string col_names, col_fixed, col_fixed_write1, col_fixed_write2, col_order,
        col_types, col_enums, check_enums;
    std::string exampleDate, r_dec;
    int startpos = 0;

    size_t MAX_COLNAME = 1;
    for (auto &f : csvdef.Fields)
        if (f.Name.size() > MAX_COLNAME) MAX_COLNAME = f.Name.size();

    for (size_t c = 0; c < csvdef.Fields.size(); c++) {
        const CsvColumn &coldef = csvdef.Fields[c];
        std::string colname = coldef.Name;
        std::string colname_fix = regexReplaceNonAlnum(colname, '_');
        if (colname != colname_fix) colname = "\"" + colname + "\"";

        std::string colnamepad = padRightG(colname, MAX_COLNAME, ' ');
        std::string comma = c < csvdef.Fields.size() - 1 ? ", " : "";

        col_names += "\"" + coldef.Name + "\"" + comma;
        std::string datemask = coldef.DataType == ColumnType::DateTime
                                   ? ".ToString(\"" + coldef.Mask + "\")" : "";
        col_order += "\t\t" + colnamepad + " = $_." + colname + datemask + "\r\n";

        if (coldef.isCodedValue) {
            std::string enumvals = joinG(coldef.CodedList, "\", \"");
            if (coldef.DataType == ColumnType::String) enumvals = "\"" + enumvals + "\"";
            else enumvals = replG(enumvals, "\"", "");
            col_enums += "$" + colname_fix + "_array = @(" + enumvals + ")\r\n";
            check_enums += "\tif ($row." + colname + " -and !($" + colname_fix +
                           "_array -contains $row." + colname + ")) {$errmsg += \"Invalid " +
                           replG(colname, "\"", "\"\"") + " \"\"$($row." + colname +
                           ")\"\" \"}\r\n";
        }

        switch (coldef.DataType) {
            case ColumnType::DateTime:
                col_types += "\t\t$row." + colnamepad + " = [datetime]::parseexact($row." +
                             colname + ", '" + coldef.Mask + "', $null)\r\n";
                if (exampleDate.empty()) exampleDate = colname;
                break;
            case ColumnType::Integer:
                col_types += "\t\t$row." + colnamepad + " = [int]($row." + colname +
                             " -replace '" + st.NullKeyword + "', '')\r\n";
                break;
            case ColumnType::Decimal: {
                std::string repl2 = coldef.DecimalSymbol == '.'
                                        ? "-replace ',', ''"
                                        : "-replace '\\.', '' -replace ',', '.'";
                col_types += "\t\t$row." + colnamepad + " = [decimal]($row." + colname +
                             " " + repl2 + ")\r\n";
                if (r_dec.empty()) r_dec = std::string(1, coldef.DecimalSymbol);
                break;
            }
            default:
                col_types += "\t\t$row." + colnamepad + " = $row." + colname +
                             ".Trim(' \"')\r\n";
                break;
        }

        if (csvdef.Separator == '\0') {
            std::string strpos = padLeftG(std::to_string(startpos), 3, ' ');
            std::string strwid = padLeftG(std::to_string(coldef.MaxWidth), 2, ' ');
            col_fixed += "\t\t" + colnamepad + " = $line.Substring(" + strpos + ", " +
                         strwid + ").Trim(' \"')\r\n";
            startpos += coldef.MaxWidth;
        }
        col_fixed_write1 += "{" + std::to_string(c) + "," +
                            std::string(coldef.DataType == ColumnType::Integer ||
                                                coldef.DataType == ColumnType::Decimal
                                            ? "" : "-") +
                            std::to_string(coldef.MaxWidth) + "} ";
        col_fixed_write2 += "$row." + colname + comma;
    }

    if (r_dec.empty()) r_dec = ".";

    std::string nameparam;
    std::string separator(1, csvdef.Separator);
    if (csvdef.Separator != '\0') {
        if (separator == "\t") separator = "`t";
        nameparam += " -Delimiter \"" + separator + "\"";
    }
    if (!csvdef.ColNameHeader) nameparam += " -Header @(" + col_names + ")";

    if (csvdef.Separator == '\0') {
        sb += "# read fixed width data file, positions " + csvdef.GetColumnWidths(true) + "\r\n";
        sb += "$stream_in = [System.IO.StreamReader]::new($filename)\r\n\r\n";
        if (csvdef.SkipLines > 0) {
            sb += "# skip first " + std::to_string(csvdef.SkipLines) + " lines\r\n";
            sb += "for ($i=0; $i -lt " + std::to_string(csvdef.SkipLines) +
                  "; $i=$i+1) { $skipline = $stream_in.ReadLine() }\r\n";
        }
        if (csvdef.ColNameHeader) {
            sb += "# skip header\r\n";
            sb += "$skipline = $stream_in.ReadLine()\r\n";
        }
        sb += "# read fixed width data\r\n";
        sb += "$csvdata = while ($line = $stream_in.ReadLine()) {\r\n";
        sb += "\t[PSCustomObject]@{\r\n";
        sb += col_fixed;
        sb += "\t}\r\n}\r\n$stream_in.Dispose()\r\n\r\n";
    } else {
        sb += "# read csv data file\r\n";
        if (csvdef.SkipLines > 0)
            sb += "$csvdata = Get-Content -Path $filename | Select-Object -Skip " +
                  std::to_string(csvdef.SkipLines) + " | ConvertFrom-Csv" + nameparam + "\r\n\r\n";
        else
            sb += "$csvdata = Import-Csv -Path $filename" + nameparam + "\r\n\r\n";
    }

    if (!col_types.empty()) {
        sb += "# Explicit datatypes\r\n";
        sb += "# WARNING: PowerShell has very basic error handling for null or invalid values,\r\n";
        sb += "# so if your data file contains integer, decimal or datetime columns with empty or incorrect values,\r\n";
        sb += "# this script can throw errors, silently change values to '0' or omit rows in the output csv, so beware.\r\n";
        sb += "$line = 0\r\n";
        sb += "foreach ($row in $csvdata)\r\n{\r\n";
        sb += "\t$line += 1\r\n";
        sb += "\ttry {\r\n";
        sb += col_types;
        sb += "\t} catch {\r\n";
        sb += "\t\tWrite-Error \"Data conversion error(s) on line $line\" -TargetObject $row\r\n";
        sb += "\t}\r\n";
        sb += "}\r\n\r\n";
    }

    if (!col_enums.empty()) {
        sb += "# Enumeration allowed values\r\n";
        sb += col_enums + "\r\n";
        sb += "# enumeration check invalid values\r\n";
        sb += "$line = 0\r\n";
        sb += "foreach ($row in $csvdata)\r\n{\r\n";
        sb += "\t# check invalid values\r\n";
        sb += "\t$errmsg = \"\"\r\n";
        sb += check_enums + "\r\n";
        sb += "\t# report invalid values\r\n";
        sb += "\t$line = $line + 1\r\n";
        sb += "\tif ($errmsg) {Write-Error \"$errmsg on line $line\" -TargetObject $row}\r\n}\r\n\r\n";
    }

    if (exampleDate.empty()) exampleDate = "myDateField";
    std::string yr = std::to_string(opt.exampleYear);
    sb += "# Remove or uncomment the script parts below to filter, transform, merge as needed\r\n\r\n";

    ScriptHeader(sb, "Data filter and sort suggestions");
    sb += "# filter on value or date range\r\n";
    sb += "#$csvdata = $csvdata | Where-Object { $_." + exampleDate +
          " -gt [DateTime]::Parse(\"" + yr + "-01-01\") -and $_." + exampleDate +
          " -lt [DateTime]::Parse(\"" + yr + "-07-01\") }\r\n\r\n";
    sb += "# sort on column\r\n";
    sb += "#$csvdata = $csvdata | Sort-Object -Property \"" + exampleDate +
          "\" # -Descending\r\n\r\n";
    sb += "# Reorder or remove columns (edit code below)\r\n";
    sb += "$csvnew = $csvdata | ForEach-Object {\r\n";
    sb += "\t[PSCustomObject]@{\r\n";
    sb += "\t\t# Reorder columns\r\n";
    sb += col_order;

    sb += "#\t\t# Data transformation suggestions\r\n";
    sb += "#\t\t" + padRightG(exampleDate, MAX_COLNAME, ' ') + " = $_." + exampleDate +
          ".ToString(\"yyyy-MM-dd\")\r\n";
    sb += "#\t\t" + padRightG("YesNo_int", MAX_COLNAME, ' ') + " = switch ($_.YesNo_str) {\r\n";
    sb += "#\t\t\t\t\"No\" {\"0\"}\r\n";
    sb += "#\t\t\t\t\"Yes\" {\"1\"}\r\n";
    sb += "#\t\t\t\tdefault {$_}\r\n";
    sb += "#\t\t\t}\r\n";
    sb += "#\t\t" + padRightG("bmi", MAX_COLNAME, ' ') +
          " = [math]::Round($_.Weight / ($_.Height * $_.Height), 2)\r\n";
    sb += "#\t\t" + padRightG("cent_pat", MAX_COLNAME, ' ') +
          " = $_.centercode.SubString(0, 2) + \"-\" + patientcode # '01-123' etc\r\n";
    sb += "\t}\r\n";
    sb += "}\r\n\r\n";

    ScriptHeader(sb, "Merge data example");
    sb += "## Merge datasets in PowerShell requires custom external modules which goes beyond the scope of this generated script\r\n";
    sb += "##Install-Module -Name Join-Object\r\n";
    sb += "##$merged_df = Join-Object -Left $patients -Right $visits -LeftJoinProperty 'PATIENT_ID' -RightJoinProperty 'PATIENT_ID' -ExcludeRightProperties 'Junk' -Prefix 'R_' | Format-Table\r\n\r\n";

    sb += "# csv write new output\r\n";
    sb += "$filenew = $pathname + \"output.txt\"\r\n";
    sb += "$csvnew | Export-Csv -Path $filenew -Encoding utf8 -Delimiter \"`t\" -NoTypeInformation\r\n\r\n";

    sb += "# alternatively, write as fixed width\r\n";
    sb += "#$stream_out = New-Object System.IO.StreamWriter $filenew\r\n";
    sb += "#foreach ($row in $csvnew)\r\n";
    sb += "#{\r\n";
    sb += "#\t# {colnr,width} space etc, negative width means left aligned\r\n";
    std::string w1 = col_fixed_write1;
    while (!w1.empty() && w1.back() == ' ') w1.pop_back();
    sb += "#\t$stream_out.WriteLine((\"" + w1 + "\" -f " + col_fixed_write2 + "))\r\n";
    sb += "#}\r\n";
    sb += "#$stream_out.Dispose()\r\n";

    return sb;
}
