#include "clock.h"

#include <stdio.h>

static bool clock_set;
static int64_t base_unix_ms;
static uint32_t base_tick;

void Clock_Set(int64_t unix_s, uint32_t tick)
{
	base_unix_ms = unix_s * 1000;
	base_tick = tick;
	clock_set = true;
}

bool Clock_IsSet(void)
{
	return clock_set;
}

/* Works for ticks before the clock was set too (readings held while waiting
 * for a time), as long as they are within ~24 days of the saved tick. */
bool Clock_TickToUnixMs(uint32_t tick, int64_t *unix_ms)
{
	if (!clock_set) {
		return false;
	}
	*unix_ms = base_unix_ms + (int32_t)(tick - base_tick);
	return true;
}

/* Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant). */
static int64_t DaysFromCivil(int y, unsigned m, unsigned d)
{
	y -= m <= 2u;
	int era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153u * (m > 2u ? m - 3u : m + 9u) + 2u) / 5u + d - 1u;
	unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
	return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

int64_t Clock_FromCivil(int year, unsigned month, unsigned day,
                        unsigned hour, unsigned minute, unsigned second)
{
	return DaysFromCivil(year, month, day) * 86400 +
	       (int64_t)(hour * 3600u + minute * 60u + second);
}

/* "YYYY-MM-DDTHH:MM:SS.mmmZ". Returns the length written (0 on error). */
size_t Clock_FormatIso(int64_t unix_ms, char *buf, size_t size)
{
	if (unix_ms < 0) {
		return 0;
	}
	int64_t secs = unix_ms / 1000;
	unsigned ms = (unsigned)(unix_ms % 1000);
	int32_t z = (int32_t)(secs / 86400);
	unsigned sod = (unsigned)(secs % 86400);

	/* Civil date from days since 1970-01-01 (H. Hinnant). */
	z += 719468;
	int32_t era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);
	unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
	int y = (int)yoe + era * 400;
	unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
	unsigned mp = (5u * doy + 2u) / 153u;
	unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
	unsigned m = mp < 10u ? mp + 3u : mp - 9u;
	y += m <= 2u;

	int n = snprintf(buf, size, "%04d-%02u-%02uT%02u:%02u:%02u.%03uZ",
	                 y, m, d, sod / 3600u, (sod / 60u) % 60u, sod % 60u, ms);
	return (n > 0 && (size_t)n < size) ? (size_t)n : 0;
}
