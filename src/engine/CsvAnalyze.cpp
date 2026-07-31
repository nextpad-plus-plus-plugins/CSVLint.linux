#include "CsvAnalyze.h"
#include "CsvValidate.h"
#include "DateTimeMask.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

// ── local helpers ───────────────────────────────────────────────────────────

static std::string trimS(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string trimCharS(const std::string &s, char c) {
    size_t b = 0, e = s.size();
    while (b < e && s[b] == c) b++;
    while (e > b && s[e - 1] == c) e--;
    return s.substr(b, e - b);
}

static bool tryParseIntS(const std::string &s, int &out) {
    std::string t = trimS(s);
    if (t.empty()) return false;
    size_t i = (t[0] == '+' || t[0] == '-') ? 1 : 0;
    if (i == t.size()) return false;
    long long v = 0;
    for (; i < t.size(); i++) {
        if (t[i] < '0' || t[i] > '9') return false;
        v = v * 10 + (t[i] - '0');
        if (v > 2147483647LL) return false;   // int.TryParse 32-bit parity
    }
    out = (t[0] == '-') ? (int)-v : (int)v;
    return true;
}

/// Counter preserving first-seen order — mirrors C# Dictionary enumeration
/// order, which the original's LINQ tie-breaking implicitly depends on.
struct OrderedCounter {
    std::vector<std::pair<char, int>> items;
    void increase(char c) {
        for (auto &kv : items) if (kv.first == c) { kv.second++; return; }
        items.emplace_back(c, 1);
    }
    int get(char c) const {
        for (auto &kv : items) if (kv.first == c) return kv.second;
        return 0;
    }
    bool contains(char c) const { return get(c) > 0; }
};

static void mapIncrease(std::map<int, int> &m, int key) { m[key]++; }

// ── CsvAnalyzeColumn ────────────────────────────────────────────────────────

void CsvAnalyzeColumn::KeepUniqueValues(const std::string &value) {
    if ((int)stat_uniquecount.size() <= csvSettings().UniqueValuesMax) {
        if (trimS(value) != "") {
            for (auto &kv : stat_uniquecount)
                if (kv.first == value) { kv.second++; return; }
            stat_uniquecount.emplace_back(value, 1);
        }
    }
}

void CsvAnalyzeColumn::KeepMinMaxInteger(const std::string &value) {
    int valint = 0;
    if (tryParseIntS(value, valint)) {
        if (valint < stat_minint || stat_minint_org.empty()) {
            stat_minint = valint;
            stat_minint_org = value;
        }
        if (valint > stat_maxint || stat_maxint_org.empty()) {
            stat_maxint = valint;
            stat_maxint_org = value;
        }
    }
}

void CsvAnalyzeColumn::KeepMinMaxDecimal(const std::string &value, char dec) {
    // Parse with the detected decimal character normalized to '.' (the C#
    // original used culture-dependent float.TryParse; this is deterministic).
    std::string t = trimS(value);
    if (dec == ',') {
        for (auto &c : t) if (c == ',') c = '.';
    }
    char *end = nullptr;
    double valdbl = strtod(t.c_str(), &end);
    if (end && *end == '\0' && end != t.c_str()) {
        if (valdbl < stat_mindbl || stat_mindbl_org.empty()) {
            stat_mindbl = valdbl;
            stat_mindbl_org = value;
        }
        if (valdbl > stat_maxdbl || stat_maxdbl_org.empty()) {
            stat_maxdbl = valdbl;
            stat_maxdbl_org = value;
        }
    }
}

void CsvAnalyzeColumn::DateTimeFormatKnown(const std::string &knownformat) {
    if (!knownformat.empty()) {
        bool d_before_y = knownformat.find('d') < knownformat.find('y');
        bool M_before_d = knownformat.find('M') < knownformat.find('d');
        stat_dat_dmy = d_before_y ? (M_before_d ? 3 : 2) : 1;
        stat_dat_format = knownformat;
    }
}

void CsvAnalyzeColumn::KeepMinMaxDateTime(const std::string &value,
                                          int ddmax1, int ddmax2, int ddmax3, int datatype) {
    if (stat_dat_dmy == 0) {
        bool newformat = stat_dat_format.empty();
        std::string sepstr = DateSep == '\0' ? "" : std::string(1, DateSep);

        if (datatype == 1 || datatype == 3) {
            if (ddmax1 > 31 && ddmax3 > 12 && ddmax3 <= 31) {
                stat_dat_dmy = 1;
                std::string yearmask = ddmax1 >= 1000 ? "yyyy" : "yy";
                stat_dat_format = yearmask + sepstr + "M" + sepstr + "d";
                newformat = true;
            }
            if (ddmax1 > 12 && ddmax1 <= 31 && ddmax3 > 31) {
                stat_dat_dmy = 2;
                std::string yearmask = ddmax3 >= 1000 ? "yyyy" : "yy";
                stat_dat_format = "d" + sepstr + "M" + sepstr + yearmask;
                newformat = true;
            }
            if (ddmax2 > 12 && ddmax2 <= 31 && ddmax3 > 31) {
                stat_dat_dmy = 3;
                std::string yearmask = ddmax3 >= 1000 ? "yyyy" : "yy";
                stat_dat_format = "M" + sepstr + "d" + sepstr + yearmask;
                newformat = true;
            }
            if (stat_dat_format.empty()) {
                if (ddmax1 > 31) {
                    std::string yearmask = ddmax1 >= 1000 ? "yyyy" : "yy";
                    stat_dat_format = yearmask + sepstr + "M" + sepstr + "d";
                } else {
                    std::string yearmask = ddmax3 >= 1000 ? "yyyy" : "yy";
                    if (DateSep == '/')
                        stat_dat_format = "M/d/" + yearmask;   // US-style
                    else
                        stat_dat_format = "d" + sepstr + "M" + sepstr + yearmask;
                }
            }
        }

        if (datatype == 2 || datatype == 3) {
            if (newformat) {
                if (!stat_dat_format.empty()) stat_dat_format += " ";
                int count = 0;
                for (char c : value) if (c == ':') count++;
                if (count == 1) stat_dat_format += "H:mm";
                if (count == 2) stat_dat_format += "H:mm:ss";
            }
        }
    }

    ParsedDateTime valdat;
    if (tryParseDateTimeExact(value, stat_dat_format, csvSettings().TwoDigitYearMax, valdat)) {
        long long key = valdat.sortKey();
        if (key < stat_mindat_key || stat_mindat_org.empty()) {
            stat_mindat_key = key;
            stat_mindat_org = value;
        }
        if (key > stat_maxdat_key || stat_maxdat_org.empty()) {
            stat_maxdat_key = key;
            stat_maxdat_org = value;
        }
    }
}

void CsvAnalyzeColumn::InputData(std::string data, bool fullstats) {
    const CsvSettings &st = csvSettings();
    CountAll++;

    // adjust for quoted values (can be a space before the first quote)
    std::string datatrim = trimS(data);
    if (!datatrim.empty() && datatrim[0] == st.DefaultQuoteChar) {
        data = trimS(data);
        data = trimCharS(data, st.DefaultQuoteChar);
    }
    if (st.TrimValues) data = trimS(data);

    // assume first line only contains column header names
    if (CountAll == 1) {
        Name = data;
        return;
    }

    int length = (int)data.size();
    if (length == 0) {
        CountEmpty++;
        return;
    }

    if (length < MinWidth) MinWidth = length;
    if (length > MaxWidth) MaxWidth = length;

    KeepUniqueValues(data);

    // check each character in string
    int digits = 0, sign = 0, signpos = 0, point = 0, comma = 0, datesep = 0, other = 0;
    char sep1 = '\0', sep2 = '\0', seplast = '\0';
    int ddmax1 = -1, ddmax2 = -1, ddmax3 = -1;
    std::string digitpart;
    int digitpartcount = 0;
    char dec = '\0';
    int vallength = length;

    for (int charidx = 0; charidx < vallength; charidx++) {
        char ch = data[charidx];
        bool isdigit = ch >= '0' && ch <= '9';

        if (isdigit) {
            digits++;
            digitpart += ch;
        }

        if (!isdigit || charidx == vallength - 1) {
            if (!digitpart.empty()) digitpartcount++;
            bool isNumeric = false;
            int n1 = 0;
            if (tryParseIntS(digitpart, n1) && !digitpart.empty()) {
                if (digitpartcount == 1 && ddmax1 < n1) ddmax1 = n1;
                if (digitpartcount == 2 && ddmax2 < n1) ddmax2 = n1;
                if (digitpartcount == 3 && ddmax3 < n1) ddmax3 = n1;
                isNumeric = true;
            }
            // check if cannot be a datetime
            if (!isNumeric || digitpart.size() > 4 ||
                (digitpart.size() == 3 && (seplast != '.' || digitpartcount <= 2))) {
                ddmax1 = -1;
            }

            if (ch == ',') {
                comma++;
                dec = ch;
            } else if (std::string("\\/-:. ").find(ch) != std::string::npos) {
                other++;
                datesep++;
                seplast = ch;
                if (sep1 == '\0' && datesep == 1) sep1 = ch;
                else if (sep2 == '\0' && datesep == 2) sep2 = ch;
            } else if (!isdigit) {
                other++;
            }

            if (ch == '.') { point++; dec = ch; other--; }
            if (ch == '+') { sign++; signpos = charidx; other--; }
            if (ch == '-') { sign++; signpos = charidx; other--; }

            digitpart.clear();
        }
    }

    // exception for time values like "12:23" -> also fill ddmax2 "23"
    if (datesep == 1 && vallength >= 4 && vallength <= 5) {
        size_t pos3 = data.find(sep1);
        if (pos3 != std::string::npos) {
            std::string datedig3 = data.substr(pos3 + 1);
            int n3 = 0;
            if (tryParseIntS(datedig3, n3) && !datedig3.empty()) {
                if (ddmax2 < n3) ddmax2 = n3;
            }
        }
    }

    // determine most likely datatype based on characters in string
    if (length >= 6 && length <= 10 && datesep == 2 && sep1 != ':' && sep1 == sep2 &&
        digits >= 4 && digits <= 8 && ddmax1 > 0 && (ddmax1 <= 31 || ddmax1 >= 1900)) {
        // date
        CountDateTime++;
        if (DateSep == '\0') DateSep = sep1;
        if (DateMax1 < ddmax1) DateMax1 = ddmax1;
        if (DateMax2 < ddmax2) DateMax2 = ddmax2;
        if (DateMax3 < ddmax3) DateMax3 = ddmax3;
        if (fullstats) KeepMinMaxDateTime(data, ddmax1, ddmax2, ddmax3, 1);
    } else if (length >= 13 && length <= 23 && datesep > 2 && datesep <= 6 &&
               digits >= 7 && digits <= 17 && ddmax1 > 0 && (ddmax1 <= 31 || ddmax1 >= 1900)) {
        // datetime
        CountDateTime++;
        if (DateSep == '\0') DateSep = sep1;
        if (DateMax1 < ddmax1) DateMax1 = ddmax1;
        if (DateMax2 < ddmax2) DateMax2 = ddmax2;
        if (DateMax3 < ddmax3) DateMax3 = ddmax3;
        if (fullstats) KeepMinMaxDateTime(data, ddmax1, ddmax2, ddmax3, 3);
    } else if (length >= 4 && length <= 12 && sep1 == ':' && datesep >= 1 && datesep <= 3 &&
               digits >= 3 && digits <= 9 && ddmax1 >= 0 && ddmax1 <= 23 && ddmax2 <= 59) {
        // time
        CountDateTime++;
        if (DateSep == '\0') DateSep = sep1;
        if (DateMax1 < ddmax1) DateMax1 = ddmax1;
        if (DateMax2 < ddmax2) DateMax2 = ddmax2;
        if (DateMax3 < ddmax3) DateMax3 = ddmax3;
        if (fullstats) KeepMinMaxDateTime(data, ddmax1, ddmax2, ddmax3, 2);
    } else if (digits > 0 && point == 0 && comma == 0 && sign <= 1 && signpos == 0 &&
               other == 0 && length <= csvSettings().IntegerDigitsMax) {
        // numeric integer, but not "000123"
        if (data.size() > 1 && data[0] == '0') {
            CountString++;
        } else {
            CountInteger++;
            if (fullstats) KeepMinMaxInteger(data);
            if ((int)data.size() > DecimalIntMax) DecimalIntMax = (int)data.size();
        }
    } else if (digits > 0 && (point == 1 || comma == 1) && sign <= 1 && signpos == 0 &&
               other == 0 && datesep <= 2) {
        // numeric decimal
        CountDecimal++;
        if (dec == '.') CountDecimalPoint++;
        if (dec == ',') CountDecimalComma++;

        size_t decpos = data.rfind(dec);
        int countdec = (int)(data.size() - decpos - 1);
        int countdig = (int)(data.size() - countdec - 1);

        if (countdec <= csvSettings().DecimalDigitsMax) {
            if (countdec > DecimalDecMax) DecimalDecMax = countdec;
            if (countdig > DecimalDigMax) DecimalDigMax = countdig;
            if (fullstats) KeepMinMaxDecimal(data, dec);
        } else {
            CountString++;
        }
    } else {
        CountString++;
    }
}

bool CsvAnalyzeColumn::CountDataTypeSignificant(int c1, int c2, int c3, int c4) const {
    if (c1 + c2 + c3 + c4 == 0) return false;
    double errorratio = (1.0 * (c2 + c3 + c4)) / (c1 + c2 + c3 + c4);
    return errorratio < csvSettings().ErrorTolerancePerc;
}

CsvColumn CsvAnalyzeColumn::InferDatatype() {
    std::string mask;
    CsvColumn result(Index);
    result.Name = Name;
    result.DataType = ColumnType::String;
    result.MaxWidth = 0;
    result.Mask = "";
    result.DecimalSymbol = '.';
    result.Decimals = 0;

    // if mixed integers and decimals, check percentage of decimals
    if (CountInteger > 0 && CountDecimal > 0) {
        if (!CountDataTypeSignificant(CountInteger, CountDecimal, 0, 0)) {
            CountDecimal += CountInteger;
            CountInteger = 0;
            if (DecimalDigMax < DecimalIntMax) DecimalDigMax = DecimalIntMax;
        }
    }

    if (CountDataTypeSignificant(CountInteger, CountString, CountDecimal, CountDateTime)) {
        result.DataType = ColumnType::Integer;
    } else if (CountDataTypeSignificant(CountDecimal, CountString, CountInteger, CountDateTime)) {
        result.DataType = ColumnType::Decimal;
        char dec = CountDecimalPoint > CountDecimalComma ? '.' : ',';
        mask = std::string(DecimalDigMax, '9') + dec + std::string(DecimalDecMax, '9');
        result.Decimals = DecimalDecMax;
        if ((int)mask.size() > MaxWidth) MaxWidth = (int)mask.size();
        result.DecimalSymbol = dec;
    } else if (CountDataTypeSignificant(CountDateTime, CountString, CountInteger, CountDecimal)) {
        result.DataType = ColumnType::DateTime;
        std::string part1 = "dd", part2 = "MM", part3 = "yyyy";
        if (DateMax1 > 12 && DateMax1 <= 31 && DateMax2 >= 1 && DateMax2 <= 12) {
            part1 = "dd"; part2 = "MM";
        }
        if (DateMax1 >= 1 && DateMax1 <= 12 && DateMax2 > 12 && DateMax2 <= 31) {
            part1 = "MM"; part2 = "dd";
        }
        if (DateMax1 > 1000) {
            part1 = "yyyy"; part2 = "MM"; part3 = "dd";
        }
        if (DateSep == ':') {
            part1 = "HH"; part2 = "mm"; part3 = "ss";
        }

        mask = part1 + DateSep + part2 + DateSep + part3;

        if (DateSep == ':' && MaxWidth <= 5) {
            size_t p = mask.find(":ss");
            if (p != std::string::npos) mask.erase(p, 3);
        }
        if (MinWidth >= 6 && MinWidth <= 8 && DateMax1 < 100 && DateMax3 < 100) {
            size_t p = mask.find("yyyy");
            if (p != std::string::npos) mask.replace(p, 4, "yy");
        }
        if (MaxWidth >= 13) mask += " HH:mm";
        if (MaxWidth > 16)  mask += ":ss";
        if (MaxWidth > 19)  mask += ".fff";

        // fixed-length "dd-MM-yyyy" or without prefix zeroes "d-M-yyyy"
        if (MinWidth < MaxWidth || MaxWidth < (int)mask.size()) {
            auto rep = [&](const char *from, const char *to) {
                size_t p = 0;
                while ((p = mask.find(from, p)) != std::string::npos) {
                    mask.replace(p, 2, to);
                    p += 1;
                }
            };
            rep("dd", "d"); rep("MM", "M"); rep("HH", "H");
        }
    }

    result.Mask = mask;
    result.MaxWidth = MaxWidth;
    return result;
}

// ── separator inference ─────────────────────────────────────────────────────

/// LINQ-parity ordering: enumeration order of `variances` is first-seen order.
static char GetSeparatorFromVariance(const std::vector<std::pair<char, float>> &variances,
                                     const OrderedCounter &occurrences,
                                     int lineCount, int &uncertancy) {
    const std::string &preferredSeparators = csvSettings().Separators;
    uncertancy = 0;

    // Optimistic: preferred separators with 0 variance, most occurrences first
    {
        std::vector<std::pair<char, float>> cand;
        for (auto &kv : variances)
            if (kv.second == 0.0f && preferredSeparators.find(kv.first) != std::string::npos)
                cand.push_back(kv);
        std::stable_sort(cand.begin(), cand.end(),
                         [&](const std::pair<char, float> &a, const std::pair<char, float> &b) {
                             return occurrences.get(a.first) > occurrences.get(b.first);
                         });
        if (!cand.empty()) return cand[0].first;
    }
    uncertancy++;

    // best char that exists on all lines, by ascending variance (stable)
    std::vector<std::pair<char, float>> sortedVariances = variances;
    std::stable_sort(sortedVariances.begin(), sortedVariances.end(),
                     [](const std::pair<char, float> &a, const std::pair<char, float> &b) {
                         return a.second < b.second;
                     });
    int found = 0;
    char first = '\0', second = '\0';
    for (auto &kv : sortedVariances) {
        if (occurrences.get(kv.first) >= lineCount) {
            if (found == 0) first = kv.first;
            if (found == 1) second = kv.first;
            found++;
            if (found >= 2) break;
        }
    }
    if (found >= 1 && preferredSeparators.find(first) != std::string::npos) return first;
    uncertancy++;
    if (found >= 2 && preferredSeparators.find(second) != std::string::npos) return second;
    uncertancy++;

    return '\0';
}

// ── InferFromData ───────────────────────────────────────────────────────────

CsvDefinition CsvAnalyze::InferFromData(const std::string &text,
                                        bool autodetect, char mansep, const std::string &manwid,
                                        bool manhead, int manskip, char commchar,
                                        bool userRequested, bool isCsv) {
    const CsvSettings &st = csvSettings();
    TextReader strfreq(text);
    std::string line;
    int lineCount = 0, linesQuoted = 0, lineContent = 0;

    int skipdetect = 0;
    char commentdetect = '\0';

    std::vector<OrderedCounter> frequencies;
    OrderedCounter occurrences;
    std::vector<OrderedCounter> frequenciesQuoted;
    OrderedCounter occurrencesQuoted;
    std::map<int, int> multiSpaces, multiZeroes, wordStarts, lineLengths;
    bool inQuotes = false;
    OrderedCounter letterFrequencyQuoted;

    while (strfreq.ReadLine(line)) {
        lineCount++;
        char firstchar = line.empty() ? '\0' : line[0];

        if (firstchar == commchar && commchar != '\0') {
            line = "";
            if (commentdetect == '\0') commentdetect = firstchar;
        }

        if (autodetect) {
            if (skipdetect == lineCount - 1 && trimS(line).empty()) skipdetect++;
        } else {
            if (lineCount <= manskip) line = "";
        }

        if (!trimS(line).empty()) {
            OrderedCounter letterFrequency;

            mapIncrease(lineLengths, (int)line.size());
            mapIncrease(wordStarts, (int)line.size());

            int spaces = 0, zeroes = 0, pos = 0, num = -1;
            for (char chr : line) {
                letterFrequency.increase(chr);
                occurrences.increase(chr);

                if (chr == st.DefaultQuoteChar) inQuotes = !inQuotes;
                else if (!inQuotes) {
                    letterFrequencyQuoted.increase(chr);
                    occurrencesQuoted.increase(chr);

                    int newcol = 0;
                    if (chr == ' ') {
                        zeroes = 0;
                        if (num == 1) {
                            num = -1;
                            spaces++;
                        }
                        if (++spaces > 1) mapIncrease(multiSpaces, pos + 1);
                    } else {
                        if (spaces > 1) newcol = 1;
                        spaces = 0;

                        // C# semantics: isdigit = "0123456789".IndexOf(chr)
                        int isdigit = (chr >= '0' && chr <= '9') ? chr - '0' : -1;

                        if (isdigit == 0) {
                            if (++zeroes > 1) mapIncrease(multiZeroes, pos - 1);
                        } else {
                            zeroes = 0;
                        }

                        bool ignore = std::string(",.-+:/\\").find(chr) != std::string::npos;
                        if (!ignore) {
                            if (isdigit < 0) {
                                if (num == 1) newcol = 1;
                                num = 0;
                            } else {
                                if (num == 0) newcol = 1;
                                num = 1;
                            }
                        }
                        if (newcol == 1) mapIncrease(wordStarts, pos);
                    }
                }
                pos++;
            }

            frequencies.push_back(letterFrequency);
            if (!inQuotes) {
                frequenciesQuoted.push_back(letterFrequencyQuoted);
                letterFrequencyQuoted = OrderedCounter();
                linesQuoted++;
            }

            if (++lineContent > 20 - 1 && !inQuotes) break;
        }
    }

    // variance on the frequency of each char (first-seen key order)
    std::vector<std::pair<char, float>> variances;
    for (auto &kv : occurrences.items) {
        float mean = (float)kv.second / lineContent;
        float variance = 0;
        for (auto &frequency : frequencies) {
            float f = (float)frequency.get(kv.first);
            variance += (f - mean) * (f - mean);
        }
        variance /= lineContent;
        variances.emplace_back(kv.first, variance);
    }

    std::vector<std::pair<char, float>> variancesQuoted;
    for (auto &kv : occurrencesQuoted.items) {
        float mean = (float)kv.second / linesQuoted;
        float variance = 0;
        for (auto &frequency : frequenciesQuoted) {
            float f = (float)frequency.get(kv.first);
            variance += (f - mean) * (f - mean);
        }
        variance /= lineContent;
        variancesQuoted.emplace_back(kv.first, variance);
    }

    int uncertancy = 0, uncertancyQuoted = 0;
    char Separator = GetSeparatorFromVariance(variances, occurrences, lineContent, uncertancy);

    CsvDefinition result(Separator);
    result.ScanState = (userRequested || isCsv) ? CsvScanState::FullScan : CsvScanState::QuickScan;

    char separatorQuoted = GetSeparatorFromVariance(variancesQuoted, occurrencesQuoted,
                                                    linesQuoted, uncertancyQuoted);
    if (uncertancyQuoted < uncertancy)
        result.Separator = separatorQuoted;
    else if (uncertancy < uncertancyQuoted ||
             (uncertancy == uncertancyQuoted && lineContent > linesQuoted))
        result.TextQualifier = '\0';   // better ignoring quotes

    if (!autodetect) result.Separator = mansep;

    result.ColNameHeader = result.Separator != '\0';

    if (autodetect) {
        result.SkipLines = skipdetect;
        result.CommentChar = commentdetect;
    } else {
        result.ColNameHeader = manhead;
        result.SkipLines = manskip;
        result.CommentChar = commchar;
    }

    // Exception, probably not tabular data file
    if (result.Separator == '\0' && (lineLengths.size() > 1 || lineContent <= 1)) {
        int xml1 = occurrences.get('>');
        int xml2 = occurrences.get('<');
        int bin = 0;
        for (auto &kv : occurrences.items)
            if ((unsigned char)kv.first < 32 && kv.first != 9) bin += kv.second;

        std::string guess = "Textfile";
        if (bin > 0) guess = "Binary";
        if (xml1 > 0 && xml1 == xml2) guess = "XML";

        result.AddColumn(guess, 9999, ColumnType::String, "");
        result.FieldWidths = {9999};
        return result;
    }

    // Failed to detect separator, could it be a fixed-width file?
    if (result.Separator == '\0') {
        // big spaces descending (right to left), zero-runs ascending
        std::vector<int> commonSpace, commonZero;
        for (auto it = multiSpaces.rbegin(); it != multiSpaces.rend(); ++it)
            if (it->second >= lineContent - 1) commonSpace.push_back(it->first);
        for (auto &kv : multiZeroes)
            if (kv.second >= lineContent - 1 && kv.first != 0) commonZero.push_back(kv.first);

        int lastvalue = 0;
        std::vector<int> foundfieldWidths;

        // set widths manually
        {
            size_t start = 0;
            while (start <= manwid.size() && !manwid.empty()) {
                size_t commaPos = manwid.find(',', start);
                std::string tok = commaPos == std::string::npos
                                      ? manwid.substr(start)
                                      : manwid.substr(start, commaPos - start);
                int wid = 0;
                if (tryParseIntS(tok, wid) && !trimS(tok).empty())
                    foundfieldWidths.push_back(wid);
                if (commaPos == std::string::npos) break;
                start = commaPos + 1;
            }
        }

        if (autodetect || foundfieldWidths.empty()) {
            for (int space : commonSpace) {
                if (space != lastvalue - 1) foundfieldWidths.push_back(space);
                lastvalue = space;
            }
            for (int zero : commonZero) {
                if (zero != lastvalue + 1) {
                    if (std::find(foundfieldWidths.begin(), foundfieldWidths.end(), zero) ==
                        foundfieldWidths.end())
                        foundfieldWidths.push_back(zero);
                }
                lastvalue = zero;
            }
            for (auto &kv : wordStarts) {
                if (kv.second >= lineContent - 1) {
                    if (std::find(foundfieldWidths.begin(), foundfieldWidths.end(), kv.first) ==
                        foundfieldWidths.end())
                        foundfieldWidths.push_back(kv.first);
                }
            }
        }
        std::sort(foundfieldWidths.begin(), foundfieldWidths.end());

        // end positions -> individual column widths
        int pos1 = 0;
        for (size_t i = 0; i < foundfieldWidths.size(); i++) {
            int pos2 = foundfieldWidths[i];
            foundfieldWidths[i] = pos2 - pos1;
            pos1 = pos2;
        }
        result.FieldWidths = foundfieldWidths;
    }

    if (result.ScanState == CsvScanState::QuickScan)
        return result;

    // ── determine data types for columns ────────────────────────────────────
    bool fixedwidth = result.Separator == '\0';
    TextReader strdata(text);
    std::vector<CsvAnalyzeColumn> colstats;
    lineContent = 0;

    result.SkipCommentLinesAtStart(strdata);

    while (!strdata.EndOfStream()) {
        lineContent++;
        bool iscomm = false;
        std::vector<std::string> values = result.ParseNextLine(strdata, iscomm);

        if (!iscomm) {
            for (size_t i = 0; i < values.size(); i++) {
                // first row defines the column count; ignore extra columns from bad data
                if (lineContent == 1 && (int)i > (int)colstats.size() - 1)
                    colstats.emplace_back((int)i);
                if ((int)i <= (int)colstats.size() - 1)
                    colstats[i].InputData(values[i], false);
            }
        }
    }

    int idx = 0;
    for (auto &stats : colstats) {
        CsvColumn col = stats.InferDatatype();
        if (fixedwidth && idx < (int)result.FieldWidths.size())
            col.MaxWidth = result.FieldWidths[idx];
        result.AddColumn(idx, col.Name, col.MaxWidth, col.DataType, col.Mask);

        if (col.DataType == ColumnType::String || col.DataType == ColumnType::Integer)
            result.Fields[idx].AddCodedValues(stats.stat_uniquecount);
        idx++;
    }

    // determine if the first row was actually header names
    int count = 0;
    bool allstrings = true;
    bool emptyname = false;
    CsvValidate csvvalid;
    for (auto &namcol : result.Fields) {
        if (namcol.DataType != ColumnType::String) {
            allstrings = false;
            std::string str = csvvalid.EvaluateDataValue(namcol.Name, namcol, namcol.Index);
            if (!str.empty()) count++;
        }
        // strip digits, if nothing left the name is "empty"
        std::string testname;
        for (char c : namcol.Name)
            if (c < '0' || c > '9') testname.push_back(c);
        if (trimS(testname).empty()) emptyname = true;

        // carriage returns in header names -> spaces
        std::string nm = namcol.Name;
        size_t p;
        while ((p = nm.find("\r\n")) != std::string::npos) nm.replace(p, 2, " ");
        for (auto &c : nm) if (c == '\r' || c == '\n') c = ' ';
        namcol.Name = nm;
    }

    result.ColNameHeader = (allstrings || count > 0) && !emptyname;
    if (!autodetect) result.ColNameHeader = manhead;

    if (!result.ColNameHeader) {
        for (auto &col : result.Fields)
            col.Name = "FIELD" + std::to_string(col.Index + 1);
    }

    return result;
}

// ── StatisticalReport ───────────────────────────────────────────────────────

static std::string reportPercentage(int iPart, int iTotal) {
    double dPerc = iPart * 100.0 / iTotal;
    char buf[32];
    snprintf(buf, sizeof buf, "%.1f", dPerc);
    return buf;
}

std::string CsvAnalyze::StatisticalReport(const std::string &text, CsvDefinition &csvdef,
                                          const std::vector<std::string> &commentLines) {
    std::vector<CsvAnalyzeColumn> colstats;
    int lineCount = 0;
    TextReader strdata(text);

    int commentCount = csvdef.SkipCommentLinesAtStart(strdata);

    while (!strdata.EndOfStream()) {
        bool iscomm = false;
        std::vector<std::string> values = csvdef.ParseNextLine(strdata, iscomm);

        if (iscomm) {
            commentCount++;
        } else {
            lineCount++;
            for (size_t i = 0; i < values.size(); i++) {
                if ((int)i > (int)colstats.size() - 1) {
                    colstats.emplace_back((int)i);
                    if (i < csvdef.Fields.size() &&
                        csvdef.Fields[i].DataType == ColumnType::DateTime)
                        colstats[i].DateTimeFormatKnown(csvdef.Fields[i].Mask);
                }
                colstats[i].InputData(values[i], true);
            }
        }
    }

    if (csvdef.ColNameHeader) lineCount--;

    std::string sb;
    sb += "Analyze data report\r\n";
    for (auto &str : commentLines) sb += str + "\r\n";

    std::string strhead = csvdef.ColNameHeader ? " (+1 header line)" : "";
    sb += "\r\nData records: " + std::to_string(lineCount) + strhead + "\r\n";
    if (commentCount > 0) sb += "Comment lines total: " + std::to_string(commentCount) + "\r\n";
    sb += "Max.unique values: " + std::to_string(csvSettings().UniqueValuesMax) + "\r\n";
    sb += "\r\n";

    // check any empty fieldnames
    std::string nameempty;
    for (size_t i = 0; i < csvdef.Fields.size(); i++)
        if (csvdef.Fields[i].Name.empty()) nameempty += std::to_string(i + 1) + ", ";
    if (!nameempty.empty()) {
        nameempty.erase(nameempty.size() - 2);
        sb += "**Warning: empty column name(s) (column " + nameempty + ") **\r\n";
    }

    // duplicate column names (first-seen order)
    std::vector<std::pair<std::string, int>> nameCounts;
    for (auto &c : colstats) {
        bool found = false;
        for (auto &kv : nameCounts)
            if (kv.first == c.Name) { kv.second++; found = true; break; }
        if (!found) nameCounts.emplace_back(c.Name, 1);
    }
    int dupTotal = 0;
    for (auto &kv : nameCounts) if (kv.second > 1) dupTotal++;
    if (dupTotal > 0) {
        sb += "**Warning: duplicate column names (" + std::to_string(dupTotal) + ") **\r\n";
        for (auto &kv : nameCounts) {
            if (kv.second > 1) {
                std::string dupcount = std::to_string(kv.second);
                dupcount.resize(13, ' ');
                sb += "n=" + dupcount + ": " + kv.first + "\r\n";
            }
        }
        sb += "\r\n";
    }

    int idx = 0;
    for (auto &stats : colstats) {
        idx++;
        sb += "----------------------------------------\r\n";
        sb += std::to_string(idx) + ": " + stats.Name + "\r\n";

        sb += "DataTypes      : ";
        if (stats.CountDecimal  > 0) sb += "decimal ("  + std::to_string(stats.CountDecimal)  + " = " + reportPercentage(stats.CountDecimal,  lineCount) + "%), ";
        if (stats.CountInteger  > 0) sb += "integer ("  + std::to_string(stats.CountInteger)  + " = " + reportPercentage(stats.CountInteger,  lineCount) + "%), ";
        if (stats.CountString   > 0) sb += "string ("   + std::to_string(stats.CountString)   + " = " + reportPercentage(stats.CountString,   lineCount) + "%), ";
        if (stats.CountDateTime > 0) sb += "datetime (" + std::to_string(stats.CountDateTime) + " = " + reportPercentage(stats.CountDateTime, lineCount) + "%), ";
        if (stats.CountEmpty    > 0) sb += "empty ("    + std::to_string(stats.CountEmpty)    + " = " + reportPercentage(stats.CountEmpty,    lineCount) + "%), ";
        if (sb.size() >= 2 && sb.compare(sb.size() - 2, 2, ", ") == 0) sb.erase(sb.size() - 2);
        sb += "\r\n";

        if (stats.MaxWidth > 0) {
            std::string strwid = stats.MinWidth == stats.MaxWidth
                                     ? std::to_string(stats.MaxWidth)
                                     : std::to_string(stats.MinWidth) + " ~ " + std::to_string(stats.MaxWidth);
            sb += "Width range    : " + strwid + " characters\r\n";
        }

        if (stats.CountInteger  > 0) sb += "Integer range  : " + stats.stat_minint_org + " ~ " + stats.stat_maxint_org + "\r\n";
        if (stats.CountDecimal  > 0) sb += "Decimal range  : " + stats.stat_mindbl_org + " ~ " + stats.stat_maxdbl_org + "\r\n";
        if (stats.CountDateTime > 0) sb += "DateTime range : " + stats.stat_mindat_org + " ~ " + stats.stat_maxdat_org + "\r\n";

        if (!stats.stat_uniquecount.empty() &&
            (int)stats.stat_uniquecount.size() <= csvSettings().UniqueValuesMax) {
            auto sorted = stats.stat_uniquecount;
            std::sort(sorted.begin(), sorted.end(),
                      [](const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) {
                          return a.first < b.first;
                      });
            sb += "-- Unique values (" + std::to_string(sorted.size()) + ") --\r\n";
            for (auto &uqval : sorted) {
                std::string strcount = std::to_string(uqval.second);
                strcount.resize(13, ' ');
                sb += "n=" + strcount + ": " + uqval.first + "\r\n";
            }
        }
        sb += "\r\n";
    }

    return sb;
}

// ── CountUniqueValues ───────────────────────────────────────────────────────

std::string CsvAnalyze::CountUniqueValues(const std::string &text, CsvDefinition &csvdef,
                                          const std::vector<int> &colidx,
                                          bool sortBy, bool sortAsc, CsvDefinition &csvnew) {
    std::vector<std::pair<std::string, int>> uniquecount;   // first-seen order
    TextReader strdata(text);
    bool iscomm = false;

    csvdef.SkipCommentLinesAtStart(strdata);
    if (csvdef.ColNameHeader) csvdef.ParseNextLine(strdata, iscomm);

    char newsep = csvdef.Separator == '\0' ? ',' : csvdef.Separator;

    while (!strdata.EndOfStream()) {
        std::vector<std::string> values = csvdef.ParseNextLine(strdata, iscomm);
        if (!iscomm) {
            std::string uniq;
            for (size_t i = 0; i < colidx.size(); i++) {
                int col = colidx[i];
                std::string val = col < (int)values.size() ? values[col] : "";
                if (val.find(newsep) != std::string::npos) {
                    std::string escaped;
                    for (char c : val) {
                        if (c == '"') escaped += "\"\"";
                        else escaped.push_back(c);
                    }
                    val = "\"" + escaped + "\"";
                }
                if (i > 0) uniq += newsep;
                uniq += val;
            }
            bool found = false;
            for (auto &kv : uniquecount)
                if (kv.first == uniq) { kv.second++; found = true; break; }
            if (!found) uniquecount.emplace_back(uniq, 1);
        }
    }

    std::string sb;
    csvnew = CsvDefinition(newsep);

    for (size_t i = 0; i < colidx.size(); i++) {
        std::string colname = colidx[i] < (int)csvdef.Fields.size()
                                  ? csvdef.Fields[colidx[i]].Name : "";
        if (colname.find(newsep) != std::string::npos) colname = "\"" + colname + "\"";
        sb += colname + newsep;
        csvnew.AddColumn((int)i, colname, csvdef.Fields[colidx[i]].MaxWidth,
                         csvdef.Fields[colidx[i]].DataType, csvdef.Fields[colidx[i]].Mask);
    }
    sb += "count_distinct\r\n";

    if (sortBy) {
        std::stable_sort(uniquecount.begin(), uniquecount.end(),
                         [sortAsc](const std::pair<std::string, int> &a,
                                   const std::pair<std::string, int> &b) {
                             return sortAsc ? a.second < b.second : a.second > b.second;
                         });
    }

    int maxwidth = 0;
    for (auto &unqcnt : uniquecount) {
        sb += unqcnt.first + newsep + std::to_string(unqcnt.second) + "\r\n";
        if (maxwidth < unqcnt.second) maxwidth = unqcnt.second;
    }
    csvnew.AddColumn("count_distinct", (int)std::to_string(maxwidth).size(), ColumnType::Integer);

    return sb;
}
