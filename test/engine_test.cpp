// engine_test.cpp — headless tests of the ported CSV engine against the
// upstream Windows repo's testdata fixtures, using its hand-authored
// schema.ini as ground truth where the inference algorithm agrees by design.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "../src/engine/CsvDefinition.h"
#include "../src/engine/CsvAnalyze.h"
#include "../src/engine/CsvValidate.h"
#include "../src/engine/DateTimeMask.h"
#include "../src/engine/CsvEdit.h"
#include "../src/engine/CsvConvert.h"
#include "../src/engine/CsvGenerateCode.h"

static int gFail = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS  %s\n", msg); \
    else { printf("FAIL  %s\n", msg); gFail++; } } while (0)

// Linux port: fixtures vendored under test/testdata (same upstream files the
// macOS test reads from a sibling nppPluginsWin64 checkout); CSVLINT_TESTDATA
// overrides.
static std::string testDataDir() {
    const char *env = getenv("CSVLINT_TESTDATA");
    return env ? std::string(env) + "/" : std::string(CSVLINT_TESTDATA_DIR) + "/";
}
static const std::string kTestData = testDataDir();

static std::string slurp(const std::string &name) {
    std::ifstream f(kTestData + name, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static CsvDefinition infer(const std::string &text) {
    return CsvAnalyze::InferFromData(text, /*autodetect*/true, '\0', "", false, 0, '\0',
                                     /*userRequested*/true, /*isCsv*/true);
}

static const CsvColumn *colByName(const CsvDefinition &d, const std::string &n) {
    for (auto &c : d.Fields) if (c.Name == n) return &c;
    return nullptr;
}

int main() {
    // ── DateTimeMask unit tests ─────────────────────────────────────────────
    {
        ParsedDateTime dt;
        CHECK(tryParseDateTimeExact("2020-01-28 14:56", "yyyy-MM-dd HH:mm", 2026, dt) &&
              dt.year == 2020 && dt.month == 1 && dt.day == 28 && dt.hour == 14,
              "mask: yyyy-MM-dd HH:mm parses");
        CHECK(!tryParseDateTimeExact("2020-02-31", "yyyy-MM-dd", 2026, dt),
              "mask: rejects Feb 31");
        CHECK(tryParseDateTimeExact("2020-02-29", "yyyy-MM-dd", 2026, dt),
              "mask: accepts leap day 2020");
        CHECK(!tryParseDateTimeExact("2019-02-29", "yyyy-MM-dd", 2026, dt),
              "mask: rejects leap day 2019");
        CHECK(tryParseDateTimeExact("23-4-2014", "d-M-yyyy", 2026, dt) &&
              dt.day == 23 && dt.month == 4,
              "mask: d-M-yyyy variable digits");
        CHECK(!tryParseDateTimeExact("23-04-2014x", "d-M-yyyy", 2026, dt),
              "mask: trailing garbage rejected");
        CHECK(!tryParseDateTimeExact("3-12-2014", "dd-MM-yyyy", 2026, dt),
              "mask: dd needs two digits");
        CHECK(tryParseDateTimeExact("05-01-99", "dd-MM-yy", 2026, dt) && dt.year == 1999,
              "mask: yy pivots to 1999 (max 2026)");
        CHECK(tryParseDateTimeExact("05-01-12", "dd-MM-yy", 2026, dt) && dt.year == 2012,
              "mask: yy pivots to 2012 (max 2026)");
        CHECK(tryParseDateTimeExact("10:21:22", "HH:mm:ss", 2026, dt) && !dt.hasDate,
              "mask: time-only, no date part");
        CHECK(!tryParseDateTimeExact("25:00", "HH:mm", 2026, dt), "mask: hour 25 rejected");
        CHECK(tryParseDateTimeExact("2019-12-31 23:59:59.123", "yyyy-MM-dd HH:mm:ss.fff", 2026, dt)
              && dt.millis == 123, "mask: milliseconds");
    }

    // ── ParseNextLine unit tests ────────────────────────────────────────────
    {
        CsvDefinition d(',');
        std::string data = "a,\"b,c\",\"say \"\"hi\"\"\",NaN, \"x\" ,\r\nnext";
        TextReader rd(data);
        bool com = false;
        auto vals = d.ParseNextLine(rd, com);
        CHECK(vals.size() == 6, "parse: 6 values");
        CHECK(vals[0] == "a" && vals[1] == "b,c", "parse: quoted separator kept");
        CHECK(vals[2] == "say \"hi\"", "parse: escaped quotes unescaped");
        CHECK(vals[3] == "", "parse: NullKeyword NaN -> empty");
        CHECK(vals[4] == "x", "parse: space before quote tolerated");
        auto vals2 = d.ParseNextLine(rd, com);
        CHECK(vals2.size() == 1 && vals2[0] == "next", "parse: next line");

        // quoted value containing a newline
        CsvDefinition d2(',');
        std::string data2 = "one,\"two\nlines\",three";
        TextReader rd2(data2);
        auto v2 = d2.ParseNextLine(rd2, com);
        CHECK(v2.size() == 3 && v2[1] == "two\nlines", "parse: newline inside quotes");

        // comment line
        CsvDefinition d3(',');
        d3.CommentChar = '#';
        std::string data3 = "# a comment\nreal,line";
        TextReader rd3(data3);
        auto v3 = d3.ParseNextLine(rd3, com);
        CHECK(com && v3.size() == 1 && v3[0] == "# a comment", "parse: comment line flagged");

        // fixed width
        CsvDefinition d4('\0');
        d4.FieldWidths = {5, 3};
        std::string data4 = "abcdE12 tail";
        TextReader rd4(data4);
        auto v4 = d4.ParseNextLine(rd4, com);
        CHECK(v4.size() == 3 && v4[0] == "abcdE" && v4[1] == "12" && v4[2] == "tail",
              "parse: fixed width + overflow column");
    }

    // ── Ini lines round trip ────────────────────────────────────────────────
    {
        std::string err;
        std::string ini =
            "Format=Delimited(;)\r\nColNameHeader=True\r\nDateTimeFormat=d-m-yyyy\r\n"
            "DecimalSymbol=,\r\nNumberDigits=2\r\n"
            "Col1=Code Integer Width 4\r\nCol2=Dosis Float Width 6\r\n"
            "Col3=StartDatum DateTime Width 10\r\n"
            "Col4=Eenheid Text Width 5\r\n;Col4=Eenheid Enumeration e|g|mg\r\n";
        CsvDefinition d = CsvDefinition::FromIniLines(ini, &err);
        CHECK(err.empty(), "ini: no error");
        CHECK(d.Separator == ';' && d.ColNameHeader, "ini: separator + header");
        CHECK(d.DateTimeFormat == "d-M-yyyy", "ini: external mask d-n-yyyy -> internal d-M-yyyy");
        CHECK(d.Fields.size() == 4, "ini: 4 columns");
        CHECK(d.Fields[0].DataType == ColumnType::Integer && d.Fields[0].MaxWidth == 4,
              "ini: col1 Integer w4");
        CHECK(d.Fields[1].DataType == ColumnType::Decimal && d.Fields[1].Mask == "999,99",
              "ini: col2 Float mask from NumberDigits+DecimalSymbol");
        CHECK(d.Fields[2].DataType == ColumnType::DateTime && d.Fields[2].Mask == "d-M-yyyy",
              "ini: col3 DateTime mask");
        CHECK(d.Fields[3].isCodedValue && d.Fields[3].CodedList.size() == 3 &&
              d.Fields[3].CodedList[2] == "mg", "ini: col4 enumeration");

        std::string out = d.GetIniLines();
        CHECK(out.find("Format=Delimited(;)") != std::string::npos, "ini out: format");
        CHECK(out.find("DateTimeFormat=d-m-yyyy") != std::string::npos,
              "ini out: internal d-M-yyyy -> external d-m-yyyy");
        CHECK(out.find("Col1=Code Integer Width 4") != std::string::npos, "ini out: col1");
        CHECK(out.find(";Col4=Eenheid Enumeration e|g|mg") != std::string::npos,
              "ini out: enumeration comment");

        // duplicate keys
        CsvDefinition dd = CsvDefinition::FromIniLines("A=1\nA=2\n", &err);
        CHECK(err.find("Duplicate key") != std::string::npos, "ini: duplicate key error");
    }

    // ── Inference: medicine.csv (semicolon, decimal comma, d-M-yyyy) ───────
    {
        CsvDefinition d = infer(slurp("medicine.csv"));
        printf("      medicine: sep=%c cols=%zu header=%d\n", d.Separator, d.Fields.size(),
               (int)d.ColNameHeader);
        CHECK(d.Separator == ';', "medicine: separator ;");
        CHECK(d.ColNameHeader, "medicine: has header");
        CHECK(d.Fields.size() == 8, "medicine: 8 columns");
        const CsvColumn *c1 = colByName(d, "Code");
        CHECK(c1 && c1->DataType == ColumnType::Integer && c1->MaxWidth == 4,
              "medicine: Code Integer w4");
        const CsvColumn *c5 = colByName(d, "Dosis");
        CHECK(c5 && c5->DataType == ColumnType::Decimal && c5->DecimalSymbol == ',',
              "medicine: Dosis Decimal with comma");
        const CsvColumn *c7 = colByName(d, "StartDatum");
        CHECK(c7 && c7->DataType == ColumnType::DateTime && c7->Mask == "d-M-yyyy",
              "medicine: StartDatum DateTime d-M-yyyy");
        const CsvColumn *c6 = colByName(d, "DossisEenheid");
        CHECK(c6 && c6->isCodedValue, "medicine: DossisEenheid coded");
        if (c6) {
            std::string joined;
            for (auto &v : c6->CodedList) joined += v + "|";
            printf("      medicine enum: %s\n", joined.c_str());
        }
    }

    // ── Inference: orders.txt (tab separated) ───────────────────────────────
    {
        CsvDefinition d = infer(slurp("orders.txt"));
        CHECK(d.Separator == '\t', "orders: separator TAB");
        CHECK(d.ColNameHeader && d.Fields.size() == 7, "orders: header + 7 columns");
        const CsvColumn *price = colByName(d, "Price");
        CHECK(price && price->DataType == ColumnType::Decimal && price->DecimalSymbol == '.',
              "orders: Price Decimal with point");
        const CsvColumn *dt = colByName(d, "OrderDate");
        CHECK(dt && dt->DataType == ColumnType::DateTime, "orders: OrderDate DateTime");
        if (dt) printf("      orders date mask: %s\n", dt->Mask.c_str());
    }

    // ── Inference: labresults.txt (quoted comma sep) ────────────────────────
    {
        CsvDefinition d = infer(slurp("labresults.txt"));
        CHECK(d.Separator == ',', "labresults: separator ,");
        CHECK(d.Fields.size() == 7, "labresults: 7 columns");
        const CsvColumn *dt = colByName(d, "LabDateTime");
        CHECK(dt && dt->DataType == ColumnType::DateTime && dt->Mask == "yyyy-MM-dd HH:mm",
              "labresults: LabDateTime yyyy-MM-dd HH:mm");
        // LabResult holds 25% genuine strings ("Neg", "See comment.") — far
        // beyond the 1% tolerance, so the ALGORITHM (C# parity) says String;
        // the upstream schema.ini's "Float" is the author's manual override.
        const CsvColumn *res = colByName(d, "LabResult");
        CHECK(res && res->DataType == ColumnType::String && res->MaxWidth == 17,
              "labresults: LabResult String w17 (25% strings beat tolerance)");
    }

    // ── Inference: bpfile.txt (pipe separated + time column) ────────────────
    {
        CsvDefinition d = infer(slurp("bpfile.txt"));
        CHECK(d.Separator == '|', "bpfile: separator |");
        CHECK(d.Fields.size() == 10, "bpfile: 10 columns");
        const CsvColumn *date = colByName(d, "Date");
        CHECK(date && date->DataType == ColumnType::DateTime && date->Mask == "yyyy/MM/dd",
              "bpfile: Date yyyy/MM/dd");
        const CsvColumn *time = colByName(d, "Time");
        CHECK(time && time->DataType == ColumnType::DateTime && time->Mask == "HH:mm:ss",
              "bpfile: Time HH:mm:ss");
        // GetIniLines writes the deviating time mask as Text + ;comment
        std::string ini = d.GetIniLines();
        CHECK(ini.find("Col6=Date DateTime Width") != std::string::npos,
              "bpfile ini: Date as DateTime");
        CHECK(ini.find(";Col7=Time DateTime HH:mm:ss") != std::string::npos &&
              ini.find("Col7=Time Text Width") != std::string::npos,
              "bpfile ini: Time as Text + DateTime comment");
        const CsvColumn *visit = colByName(d, "Visit");
        CHECK(visit && visit->isCodedValue && visit->CodedList.size() == 2,
              "bpfile: Visit enumeration BASE|FUP1");
    }

    // ── Inference: kinesiology.csv (fixed width) ────────────────────────────
    {
        CsvDefinition d = infer(slurp("kinesiology.csv"));
        printf("      kinesiology: sep=%d cols=%zu widths=%s\n", (int)d.Separator,
               d.Fields.size(), d.GetColumnWidths(false).c_str());
        CHECK(d.Separator == '\0', "kinesiology: fixed width detected");
        CHECK(d.ColNameHeader, "kinesiology: has header");
        CHECK(d.Fields.size() >= 20, "kinesiology: ~22 columns found");
        const CsvColumn *age = colByName(d, "Age");
        CHECK(age && age->DataType == ColumnType::Integer, "kinesiology: Age Integer");
    }

    // ── Inference: ggddata.txt (fixed width, no header) ─────────────────────
    {
        CsvDefinition d = infer(slurp("ggddata.txt"));
        printf("      ggddata: sep=%d cols=%zu header=%d widths=%s\n", (int)d.Separator,
               d.Fields.size(), (int)d.ColNameHeader, d.GetColumnWidths(false).c_str());
        CHECK(d.Separator == '\0', "ggddata: fixed width detected");
        CHECK(!d.ColNameHeader, "ggddata: no header (FIELD1.. names)");
        CHECK(!d.Fields.empty() && d.Fields[0].Name == "FIELD1", "ggddata: FIELD1 naming");
    }

    // ── Not tabular data ────────────────────────────────────────────────────
    {
        CsvDefinition d = infer("Just some prose text.\nWith lines of differing length.\n"
                                "No separators to speak of here at all really.\n");
        CHECK(d.Fields.size() == 1 && d.Fields[0].MaxWidth == 9999,
              "prose: single Textfile column");
        printf("      prose guess: %s\n", d.Fields[0].Name.c_str());
    }

    // ── Validation ──────────────────────────────────────────────────────────
    {
        // definition from the upstream schema.ini [medicine.csv] section
        std::string err;
        CsvDefinition d = CsvDefinition::FromIniLines(
            "Format=Delimited(;)\nColNameHeader=True\nDateTimeFormat=d-m-yyyy\n"
            "DecimalSymbol=,\nNumberDigits=2\n"
            "Col1=Code Integer Width 4\nCol2=PatientNaam Text Width 25\n"
            "Col3=MedicatieCode Text Width 8\nCol4=MedicatieNaam Text Width 50\n"
            "Col5=Dosis Float Width 6\nCol6=DossisEenheid Text Width 5\n"
            "Col7=StartDatum DateTime Width 10\nCol8=StopDatum DateTime Width 10\n", &err);
        CHECK(err.empty() && d.Fields.size() == 8, "validate: medicine def loads");

        std::string text = slurp("medicine.csv");
        TextReader rd(text);
        CsvValidate v;
        v.ValidateData(rd, d, "00:00:00.000");
        std::string report = v.Report();
        printf("      medicine validate tail: %s",
               report.substr(report.rfind("Inspected")).c_str());
        CHECK(report.find("Inspected") != std::string::npos, "validate: summary line present");

        // deliberate errors
        CsvDefinition bad = CsvDefinition::FromIniLines(
            "Format=CSVDelimited\nColNameHeader=True\nDateTimeFormat=yyyy-mm-dd\n"
            "DecimalSymbol=.\nNumberDigits=1\n"
            "Col1=Id Integer Width 3\nCol2=Amount Float Width 6\nCol3=When DateTime Width 10\n",
            &err);
        std::string badcsv =
            "Id,Amount,When\n"
            "1,2.5,2020-01-31\n"          // ok
            "12x,3.5,2020-02-15\n"        // bad int
            "3,.5,2020-03-01\n"           // missing leading zero
            "4,4.55,2020-04-01\n"         // too many decimals
            "5,5.5,2020-13-01\n"          // bad month
            "6,6.5,1850-01-01\n"          // year out of range
            "7777,7.5,2020-06-01\n"       // too long for width 3
            "8,8.5,2020-07-01,extra\n";   // too many columns
        TextReader rdb(badcsv);
        CsvValidate vb;
        vb.ValidateData(rdb, bad, "00:00:00.000");
        std::string rep = vb.Report();
        printf("%s", rep.c_str());
        CHECK(rep.find("value \"12x\" not a valid integer") != std::string::npos,
              "validate: bad integer reported");
        CHECK(rep.find("missing leading zero") != std::string::npos,
              "validate: leading zero reported");
        CHECK(rep.find("has too many decimals") != std::string::npos,
              "validate: too many decimals reported");
        CHECK(rep.find("value \"2020-13-01\" not a valid datetime") != std::string::npos,
              "validate: bad month reported");
        CHECK(rep.find("is out of range") != std::string::npos,
              "validate: year range reported");
        CHECK(rep.find("value \"7777\" is too long") != std::string::npos,
              "validate: too long reported");
        CHECK(rep.find("Too many columns") != std::string::npos,
              "validate: too many columns reported");
    }

    // ── ConstructLine / header round trip ───────────────────────────────────
    {
        CsvDefinition d(',');
        d.AddColumn("a", 5, ColumnType::String);
        d.AddColumn("n", 5, ColumnType::Integer);
        std::vector<std::string> vals = {"x,y", "12"};
        CHECK(d.ConstructLine(vals, false) == "\"x,y\",12", "construct: quotes when needed");
        CHECK(d.ConstructHeader() == "a,n", "construct: header");

        CsvDefinition f('\0');
        f.AddColumn("t", 5, ColumnType::String);
        f.AddColumn("i", 4, ColumnType::Integer);
        f.FieldWidths = {5, 4};
        CHECK(f.ConstructLine({"ab", "7"}, false) == "ab      7",
              "construct: fixed width pads text right, numbers left");
    }

    // ── CsvEdit: reformat ───────────────────────────────────────────────────
    {
        std::string err;
        CsvDefinition d = CsvDefinition::FromIniLines(
            "Format=Delimited(;)\nColNameHeader=True\nDateTimeFormat=d-m-yyyy\n"
            "DecimalSymbol=,\nNumberDigits=1\n"
            "Col1=Id Integer Width 2\nCol2=Amt Float Width 5\nCol3=When DateTime Width 10\n", &err);
        std::string text = "Id;Amt;When\n1;2,5;23-4-2014\n2;10,0;1-12-2015\n";
        std::string out = CsvEdit::ReformatDataFile(text, d, ",", true,
                                                    "yyyy-MM-dd", ".", " ", false, "\n");
        CHECK(out == "Id,Amt,When\n1,2.5,2014-04-23\n2,10.0,2015-12-01\n",
              "edit: reformat separator+datetime+decimal");

        // to fixed width: header line dropped, numeric right-aligned
        CsvDefinition d2 = CsvDefinition::FromIniLines(
            "Format=Delimited(;)\nColNameHeader=True\n"
            "Col1=Id Integer Width 3\nCol2=Name Text Width 5\n", &err);
        std::string t2 = "Id;Name\n1;ab\n23;cde\n";
        std::string o2 = CsvEdit::ReformatDataFile(t2, d2, std::string(1, '\0'), true,
                                                   "", "", " ", false, "\n");
        CHECK(o2 == "  1ab   \n 23cde  \n", "edit: reformat to fixed width (no header)");
    }

    // ── CsvEdit: sort ───────────────────────────────────────────────────────
    {
        std::string err;
        CsvDefinition d = CsvDefinition::FromIniLines(
            "Format=CSVDelimited\nColNameHeader=True\n"
            "Col1=N Integer Width 3\nCol2=T Text Width 5\n", &err);
        std::string text = "N,T\n5,ee\n-2,bb\n10,cc\n-11,aa\n1,dd\n";
        std::string asc = CsvEdit::SortData(text, d, 0, true, true, "\n", &err);
        CHECK(err.empty() && asc == "N,T\n-11,aa\n-2,bb\n1,dd\n5,ee\n10,cc\n",
              "edit: sort integer ascending incl. negatives");
        std::string desc = CsvEdit::SortData(text, d, 0, false, true, "\n", &err);
        CHECK(desc == "N,T\n10,cc\n5,ee\n1,dd\n-2,bb\n-11,aa\n",
              "edit: sort integer descending");
        std::string bylen = CsvEdit::SortData(text, d, 1, true, false, "\n", &err);
        CHECK(bylen.find("N,T\n") == 0, "edit: sort by length keeps header");
        std::string bad = CsvEdit::SortData(text, d, 9, true, true, "\n", &err);
        CHECK(!err.empty() && bad == text, "edit: sort index out of bounds errors");
    }

    // ── CsvEdit: column split / add ─────────────────────────────────────────
    {
        std::string err;
        CsvDefinition d = CsvDefinition::FromIniLines(
            "Format=CSVDelimited\nColNameHeader=True\n"
            "Col1=Code Text Width 7\nCol2=V Integer Width 2\n", &err);
        std::string text = "Code,V\nAB-12,1\nCD-345,2\n";
        CsvDefinition dnew;

        // split on character '-' (1st occurrence)
        std::string o4 = CsvEdit::ColumnSplit(text, d, 0, 4, "-", "1", false, "\n", dnew);
        CHECK(o4 == "Code,Code (2),Code (3),V\nAB-12,AB,12,1\nCD-345,CD,345,2\n",
              "edit: split on character keeps original + adds two");
        CHECK(dnew.Fields.size() == 4 && dnew.Fields[1].Name == "Code (2)",
              "edit: split adds uniquely named columns");

        // split on position 2, remove original
        std::string o5 = CsvEdit::ColumnSplit(text, d, 0, 5, "", "2", true, "\n", dnew);
        CHECK(o5 == "Code (2),Code (3),V\nAB,-12,1\nCD,-345,2\n",
              "edit: split on position with remove");

        // search and replace (edit-only, one new column)
        std::string o2 = CsvEdit::ColumnSplit(text, d, 0, 2, "-", "_", false, "\n", dnew);
        CHECK(o2 == "Code,Code (2),V\nAB-12,AB_12,1\nCD-345,CD_345,2\n",
              "edit: search-replace adds edited column");

        // pad left to width 4 (edit-only)
        CsvDefinition d2 = CsvDefinition::FromIniLines(
            "Format=CSVDelimited\nColNameHeader=True\nCol1=K Text Width 4\n", &err);
        std::string t2 = "K\n7\n42\n";
        std::string o1 = CsvEdit::ColumnSplit(t2, d2, 0, 1, "0", "4", false, "\n", dnew);
        CHECK(o1 == "K,K (2)\n7,0007\n42,0042\n", "edit: pad-left with zeros");
    }

    // ── CsvEdit: select columns ─────────────────────────────────────────────
    {
        std::string err;
        CsvDefinition d = CsvDefinition::FromIniLines(
            "Format=CSVDelimited\nColNameHeader=True\n"
            "Col1=A Text Width 2\nCol2=B Text Width 2\nCol3=C Text Width 2\n", &err);
        std::string text = "A,B,C\na1,b1,c1\na2,b2,c2\n";
        CsvDefinition dnew;
        std::string out = CsvEdit::SelectColumns(text, d, {2, 0}, "\n", dnew);
        CHECK(out == "C,A\nc1,a1\nc2,a2\n", "edit: select+reorder columns");
        CHECK(dnew.Fields.size() == 2 && dnew.Fields[0].Name == "C",
              "edit: select builds new definition");
    }

    // ── formatDateTime round trip ───────────────────────────────────────────
    {
        ParsedDateTime dt;
        tryParseDateTimeExact("23-4-2014", "d-M-yyyy", 2026, dt);
        CHECK(formatDateTime(dt, "yyyy-MM-dd") == "2014-04-23", "edit: date format 1");
        CHECK(formatDateTime(dt, "d/M/yy") == "23/4/14", "edit: date format 2");
        tryParseDateTimeExact("2019-12-31 23:59:59.123", "yyyy-MM-dd HH:mm:ss.fff", 2026, dt);
        CHECK(formatDateTime(dt, "yyyyMMddHHmmss") == "20191231235959", "edit: sortable iso");
    }

    // ── CsvConvert: SQL / XML / JSON ────────────────────────────────────────
    {
        std::string err;
        CsvDefinition d = CsvDefinition::FromIniLines(
            "Format=CSVDelimited\nColNameHeader=True\nDateTimeFormat=yyyy-mm-dd\n"
            "DecimalSymbol=.\nNumberDigits=1\n"
            "Col1=Id Integer Width 3\nCol2=Name Text Width 8\n"
            "Col3=Amt Float Width 6\nCol4=When DateTime Width 10\n", &err);
        std::string text = "Id,Name,When\n1,Bob's,2.5,2020-01-31\n2,Ann,3.5,2020-02-15\n";
        text = "Id,Name,Amt,When\n1,Bob's,2.5,2020-01-31\n2,Ann,3.5,2020-02-15\n";
        CsvConvertOptions opt;
        opt.fileNameNoExt = "unit test";
        opt.commentLines = {"File: unit.csv"};

        CsvDefinition dsql = d;
        std::string sql = CsvConvert::ToSQL(text, dsql, opt);
        CHECK(sql.find("CREATE TABLE unit_test (") != std::string::npos,
              "sql: table name from file, spaces to underscore");
        CHECK(sql.find("Id integer,") != std::string::npos, "sql: integer column");
        CHECK(sql.find("Amt numeric(6,1),") != std::string::npos, "sql: numeric column");
        CHECK(sql.find("When datetime,") != std::string::npos, "sql: datetime column");
        CHECK(sql.find("(1, 'Bob''s', 2.5, '2020-01-31 00:00:00'),") != std::string::npos,
              "sql: insert row with escaped quote + iso datetime");
        CHECK(sql.find("(2, 'Ann', 3.5, '2020-02-15 00:00:00');") != std::string::npos,
              "sql: last row ends with semicolon");
        CHECK(sql.find("-- insert records 1 - 2\r\n") != std::string::npos,
              "sql: batch comment with record count");

        CsvDefinition dxml = d;
        std::string xml = CsvConvert::ToXML(text, dxml, opt);
        CHECK(xml.find("<unit_test>") != std::string::npos, "xml: record tag");
        CHECK(xml.find("<Name>Bob&apos;s</Name>") != std::string::npos, "xml: escaped value");
        CHECK(xml.find("<When>2020-01-31T00:00:00</When>") != std::string::npos,
              "xml: sortable datetime");

        CsvDefinition djson = d;
        std::string json = CsvConvert::ToJSON(text, djson, opt);
        CHECK(json.find("\"Id\": 1,") != std::string::npos, "json: bare integer");
        CHECK(json.find("\"Name\": \"Bob's\",") != std::string::npos, "json: string quoted");
        CHECK(json.find("\"Amt\": 2.5,") != std::string::npos, "json: bare decimal");
        CHECK(json.find("\"When\": \"2020-01-31T00:00:00\"") != std::string::npos,
              "json: datetime quoted iso");
    }

    // ── CsvGenerateCode: 6 metadata/script generators ───────────────────────
    {
        std::string err;
        CsvDefinition d = CsvDefinition::FromIniLines(
            "Format=CSVDelimited\nColNameHeader=True\nDateTimeFormat=yyyy-mm-dd\n"
            "DecimalSymbol=.\nNumberDigits=1\n"
            "Col1=Id Integer Width 3\nCol2=Name Text Width 8\n"
            "Col3=Amt Float Width 6\nCol4=When DateTime Width 10\n"
            "Col5=Status Text Width 4\n"
            ";Col5=Status Text Width 4 Enumeration OK|NOK\n", &err);
        CsvGenerateOptions gopt;
        gopt.filePath = "/tmp/unit.csv";
        gopt.fileName = "unit.csv";
        gopt.fileDir = "/tmp";
        gopt.commentLines = {"File: unit.csv"};
        gopt.exampleYear = 2026;

        std::string ini = CsvGenerateCode::SchemaIni(d, gopt);
        CHECK(ini.rfind("[unit.csv]\r\n", 0) == 0, "gen ini: filename section header");
        CHECK(ini.find("DateTimeFormat=yyyy-mm-dd") != std::string::npos,
              "gen ini: external datetime mask");
        // C# builds the ";ColN" comment from the bare column name (datatype and
        // width are appended to `def` only after the enum comment is written).
        CHECK(ini.find(";Col5=Status Enumeration OK|NOK") != std::string::npos,
              "gen ini: enum comment line survives");
        CHECK(ini.find("; NOTE: some CSV Lint features are not supported") != std::string::npos,
              "gen ini: ODBC note because of enum column");

        std::string json = CsvGenerateCode::SchemaJSON(d, gopt);
        CHECK(json.find("\"url\": \"unit.csv\"") != std::string::npos, "gen json: url");
        CHECK(json.find("\"delimiter\": \",\"") != std::string::npos, "gen json: delimiter");
        CHECK(json.find("\"datatype\": \"integer\"") != std::string::npos,
              "gen json: integer datatype");
        CHECK(json.find("\"decimalChar\": \".\"") != std::string::npos &&
              json.find("\"pattern\": \"#0.0\"") != std::string::npos,
              "gen json: decimal pattern object");
        CHECK(json.find("\"base\": \"date\"") != std::string::npos &&
              json.find("\"format\": \"yyyy-MM-dd\"") != std::string::npos,
              "gen json: date base + internal mask format");
        CHECK(json.find("\"format\": \"OK|NOK\"") != std::string::npos,
              "gen json: enum format list");

        std::string dict = CsvGenerateCode::DatadictionaryCSV(d, gopt);
        CHECK(dict.rfind("Nr,ColumnName,DataType,Width,Decimals,Mask,Enumeration\r\n", 0) == 0,
              "gen dict: header row");
        CHECK(dict.find("1,Id,Integer,3,,,") != std::string::npos, "gen dict: integer row");
        CHECK(dict.find("3,Amt,Decimal,6,1,#0.0,") != std::string::npos, "gen dict: decimal row");
        CHECK(dict.find("4,When,Date,10,,yyyy-MM-dd,") != std::string::npos,
              "gen dict: date row");
        CHECK(dict.find("5,Status,String,4,,,OK|NOK") != std::string::npos,
              "gen dict: enum row");

        std::string py = CsvGenerateCode::PythonPanda(d, gopt);
        CHECK(py.find("os.chdir(\"/tmp\")\r\n") != std::string::npos &&
              py.find("filename = \"/tmp/unit.csv\"") != std::string::npos,
              "gen py: chdir + full-path filename");
        CHECK(py.find("\"Id\": np.int64,") != std::string::npos, "gen py: int64 dtype");
        CHECK(py.find("# datetime columns; '%Y-%m-%d'") != std::string::npos &&
              py.find("col_dates = ['When']") != std::string::npos,
              "gen py: strftime mask + parse_dates list");
        CHECK(py.find("df = pd.read_csv(filename, sep=',', decimal='.', header=0, "
                      "parse_dates=col_dates)") != std::string::npos,
              "gen py: read_csv call");
        CHECK(py.find("\"Status\": [\"OK\", \"NOK\"]") != std::string::npos,
              "gen py: enum allowed values");
        CHECK(py.find("#df = df[(df[\"When\"] >= \"2026-01-01\")") != std::string::npos,
              "gen py: example year in filter suggestion");

        std::string r = CsvGenerateCode::RScript(d, gopt);
        CHECK(r.find("setwd(\"/tmp\")") != std::string::npos, "gen r: setwd");
        CHECK(r.find("df$When <- as.Date(df$When, format=\"%Y-%m-%d\")") != std::string::npos,
              "gen r: as.Date conversion");
        CHECK(r.find("df$Amt <- as.numeric(df$Amt)") != std::string::npos,
              "gen r: as.numeric conversion");
        CHECK(r.find("df <- read.csv(filename, sep=',', dec=\".\", header=TRUE)")
                  != std::string::npos,
              "gen r: read.csv call");
        CHECK(r.find("\"Status\" = c(\"OK\", \"NOK\")") != std::string::npos,
              "gen r: enum allowed values");

        std::string ps = CsvGenerateCode::PowerShell(d, gopt);
        CHECK(ps.find("$pathname = \"/tmp/\"") != std::string::npos &&
              ps.find("$filename = $pathname + \"unit.csv\"") != std::string::npos,
              "gen ps: path + filename");
        CHECK(ps.find("$csvdata = Import-Csv -Path $filename -Delimiter \",\"")
                  != std::string::npos,
              "gen ps: Import-Csv call");
        CHECK(ps.find("[datetime]::parseexact($row.When, 'yyyy-MM-dd', $null)")
                  != std::string::npos,
              "gen ps: datetime parseexact keeps C# mask");
        CHECK(ps.find("[int]($row.Id -replace 'NaN', '')") != std::string::npos,
              "gen ps: int conversion strips NullKeyword");
        CHECK(ps.find("[decimal]($row.Amt -replace ',', '')") != std::string::npos,
              "gen ps: decimal conversion point-decimal");
        CHECK(ps.find("$Status_array = @(\"OK\", \"NOK\")") != std::string::npos &&
              ps.find("!($Status_array -contains $row.Status)") != std::string::npos,
              "gen ps: enum array + containment check");
    }

    printf(gFail ? "== %d FAILURE(S) ==\n" : "== ALL PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
