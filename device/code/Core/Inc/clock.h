#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* UTC wall clock built on the HAL tick. Whenever GNSS or the phone gives a
 * time, the UTC and the tick at that moment are saved; any other tick is
 * converted as utc = saved_utc + (tick - saved_tick). */

/* Seconds from 1970-01-01 to 2000-01-01, the XBee GNSS epoch. */
#define CLOCK_UNIX_2000 946684800L

void Clock_Set(int64_t unix_s, uint32_t tick);
bool Clock_IsSet(void);
bool Clock_TickToUnixMs(uint32_t tick, int64_t *unix_ms);
int64_t Clock_FromCivil(int year, unsigned month, unsigned day,
                        unsigned hour, unsigned minute, unsigned second);
size_t Clock_FormatIso(int64_t unix_ms, char *buf, size_t size);

#endif
