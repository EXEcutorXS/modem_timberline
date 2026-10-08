#ifndef TIMEZONE_H
#define TIMEZONE_H

#include <stdint.h>

/* Calendar/time-zone arithmetic for the "time from internet" feature
   (see Modem::doTimeSync() and Timberline::timeSyncHandler()). Header-only,
   no hardware dependencies. All times are plain Gregorian, seconds since
   1970-01-01 00:00:00.

   DST rule codes (the "dst" half of the "<utcOffsetMin>,<dst>" timeZone
   payload): 0 = none, 1 = EU (last Sunday of March 01:00 UTC to last Sunday
   of October 01:00 UTC), 2 = US (second Sunday of March 02:00 local standard
   time to first Sunday of November 02:00 local daylight time). Southern
   hemisphere DST rules are not covered. */
enum { TZ_DST_NONE = 0, TZ_DST_EU = 1, TZ_DST_US = 2 };

/* Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm). */
static inline int32_t tz_daysFromCivil(int y, int m, int d)
{
    y -= (m <= 2);
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static inline void tz_civilFromDays(int32_t z, int& y, int& m, int& d)
{
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (int)(yoe + era * 400);
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp  = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y += (m <= 2);
}

static inline uint32_t tz_toUnix(int y, int mo, int d, int h, int mi, int s)
{
    return (uint32_t)((int64_t)tz_daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s);
}

static inline void tz_fromUnix(uint32_t t, int& y, int& mo, int& d, int& h, int& mi, int& s)
{
    int32_t days = (int32_t)(t / 86400);
    uint32_t rem = t % 86400;
    tz_civilFromDays(days, y, mo, d);
    h = (int)(rem / 3600); mi = (int)((rem % 3600) / 60); s = (int)(rem % 60);
}

/* 0 = Sunday ... 6 = Saturday (1970-01-01 was a Thursday). */
static inline int tz_weekday(int32_t days) { return (int)((days + 4) % 7 + 7) % 7; }

/* Day of month of the n-th Sunday of a month (n = 1..4), or the last Sunday
   for n = 0. */
static inline int tz_sundayOfMonth(int y, int m, int n)
{
    if (n > 0) {
        int32_t first = tz_daysFromCivil(y, m, 1);
        return 1 + (7 - tz_weekday(first)) % 7 + (n - 1) * 7;
    }
    int nextY = (m == 12) ? y + 1 : y, nextM = (m == 12) ? 1 : m + 1;
    int32_t last = tz_daysFromCivil(nextY, nextM, 1) - 1;
    int lastDom;
    { int yy, mm, dd; tz_civilFromDays(last, yy, mm, dd); lastDom = dd; }
    return lastDom - tz_weekday(last);
}

static inline bool tz_dstActive(uint32_t utc, int16_t stdOffsetMin, uint8_t rule)
{
    if (rule == TZ_DST_NONE) return false;
    int y, mo, d, h, mi, s;
    tz_fromUnix(utc, y, mo, d, h, mi, s);
    uint32_t start, end;
    if (rule == TZ_DST_EU) {
        start = tz_toUnix(y, 3,  tz_sundayOfMonth(y, 3, 0),  1, 0, 0);
        end   = tz_toUnix(y, 10, tz_sundayOfMonth(y, 10, 0), 1, 0, 0);
    } else {
        start = tz_toUnix(y, 3,  tz_sundayOfMonth(y, 3, 2),  2, 0, 0) - (uint32_t)((int32_t)stdOffsetMin * 60);
        end   = tz_toUnix(y, 11, tz_sundayOfMonth(y, 11, 1), 2, 0, 0) - (uint32_t)((int32_t)(stdOffsetMin + 60) * 60);
    }
    return utc >= start && utc < end;
}

/* Local time (as a "fake UTC" unix value, ready for tz_fromUnix()) for a UTC
   instant, standard offset in minutes plus the DST rule. */
static inline uint32_t tz_localFromUtc(uint32_t utc, int16_t stdOffsetMin, uint8_t rule)
{
    int32_t off = stdOffsetMin + (tz_dstActive(utc, stdOffsetMin, rule) ? 60 : 0);
    return (uint32_t)((int32_t)utc + off * 60);
}

#endif /* TIMEZONE_H */
