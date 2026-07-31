#include "DateTimeMask.h"

// Read 1-2 digits (single-letter token) or exactly `fixed` digits.
static bool readDigits(const std::string &v, size_t &pos, int fixedCount, int maxCount, int &out) {
    int n = 0, count = 0;
    while (pos < v.size() && v[pos] >= '0' && v[pos] <= '9' && count < maxCount) {
        n = n * 10 + (v[pos] - '0');
        pos++; count++;
    }
    if (fixedCount > 0 && count != fixedCount) return false;
    if (fixedCount == 0 && count == 0) return false;
    out = n;
    return true;
}

static bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static int daysInMonth(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    return (m == 2 && isLeap(y)) ? 29 : d[m - 1];
}

bool tryParseDateTimeExact(const std::string &value, const std::string &mask,
                           int twoDigitYearMax, ParsedDateTime &out) {
    if (mask.empty()) return false;
    out = ParsedDateTime{};
    bool sawYear = false, sawMonth = false, sawDay = false;

    size_t vp = 0, mp = 0;
    while (mp < mask.size()) {
        char t = mask[mp];
        size_t run = 1;
        while (mp + run < mask.size() && mask[mp + run] == t) run++;

        int n = 0;
        switch (t) {
            case 'y':
                // yyyy = exactly 4 digits; yy = exactly 2, pivoted on TwoDigitYearMax.
                if (run >= 4) { if (!readDigits(value, vp, 4, 4, n)) return false; out.year = n; }
                else          { if (!readDigits(value, vp, 2, 2, n)) return false;
                                int pivot = twoDigitYearMax % 100;
                                int century = twoDigitYearMax - pivot;
                                out.year = (n <= pivot) ? century + n : century - 100 + n; }
                sawYear = true; out.hasDate = true;
                break;
            case 'M':
                if (run >= 2) { if (!readDigits(value, vp, 2, 2, n)) return false; }
                else          { if (!readDigits(value, vp, 0, 2, n)) return false; }
                out.month = n; sawMonth = true; out.hasDate = true;
                break;
            case 'd':
                if (run >= 2) { if (!readDigits(value, vp, 2, 2, n)) return false; }
                else          { if (!readDigits(value, vp, 0, 2, n)) return false; }
                out.day = n; sawDay = true; out.hasDate = true;
                break;
            case 'H':
                if (run >= 2) { if (!readDigits(value, vp, 2, 2, n)) return false; }
                else          { if (!readDigits(value, vp, 0, 2, n)) return false; }
                if (n > 23) return false;
                out.hour = n;
                break;
            case 'm':
                if (run >= 2) { if (!readDigits(value, vp, 2, 2, n)) return false; }
                else          { if (!readDigits(value, vp, 0, 2, n)) return false; }
                if (n > 59) return false;
                out.minute = n;
                break;
            case 's':
                if (run >= 2) { if (!readDigits(value, vp, 2, 2, n)) return false; }
                else          { if (!readDigits(value, vp, 0, 2, n)) return false; }
                if (n > 59) return false;
                out.second = n;
                break;
            case 'f':
                if (!readDigits(value, vp, (int)run, (int)run, n)) return false;
                for (size_t i = run; i < 3; i++) n *= 10;
                out.millis = n;
                break;
            default:
                // literal characters must match one-for-one
                for (size_t i = 0; i < run; i++) {
                    if (vp >= value.size() || value[vp] != t) return false;
                    vp++;
                }
                break;
        }
        mp += run;
    }

    // whole value must be consumed (TryParseExact semantics)
    if (vp != value.size()) return false;

    // calendar validity (C# rejects e.g. 31-02-2020, month 13, day 0)
    if (out.hasDate) {
        if (sawMonth && (out.month < 1 || out.month > 12)) return false;
        if (sawDay) {
            int y = sawYear ? out.year : 2000;   // masks without a year: any non-leap-sensitive check
            if (out.day < 1 || out.day > daysInMonth(y, sawMonth ? out.month : 1)) return false;
        }
    }
    return true;
}
