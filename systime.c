#include <time.h>
#include "cmsis_os2.h"
#include "systime.h"
//#include "shell.h"
#include "outputf.h"
//#include "ethptp.h"

#define USE_SYSTIME_TICKS
// #define USE_SYSTIME_GPS_EPOCH
// #define USE_SYSTIME_NTP
// #define USE_SYSTIME_RTC

#if !defined(__ARMCC_VERSION) && defined (__GNUC__)
#define _localtime_r localtime_r
#endif

// Indicates when system time becomes valid.
static bool systime_valid = false;

// Name of days.
static char *days[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

// Name of months.
static char *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

uint32_t system_get_time_ms(void)
{
#ifdef USE_SYSTIME_RTC
    // Attempt to read time from RTC. This is more accurate and stable than tick-based methods
        RTC_TimeTypeDef sTime = {0};
        RTC_DateTypeDef sDate = {0};

        /* Try to read RTC time/date. If successful, convert to unix ms. */
        if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) == HAL_OK &&
            HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN) == HAL_OK)
        {
            /* Convert RTC date/time to unix seconds (UTC) */
            uint16_t year = 2000 + (uint16_t)sDate.Year; /* RTC stores year offset from 2000 */
            uint8_t month = sDate.Month;
            uint8_t day = sDate.Date;
            uint8_t hour = sTime.Hours;
            uint8_t minute = sTime.Minutes;
            uint8_t second = sTime.Seconds;

            /* Compute days since 1970-01-01 */
            uint32_t days = 0;
            for (uint16_t y = 1970; y < year; ++y)
            {
                bool is_leap = ( (y%4==0 && y%100!=0) || (y%400==0) );
                days += is_leap ? 366U : 365U;
            }

            uint8_t days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
            bool is_leap_year = ( (year%4==0 && year%100!=0) || (year%400==0) );
            if (is_leap_year) days_in_month[1] = 29;

            for (uint8_t m = 1; m < month; ++m)
            {
                days += days_in_month[m-1];
            }

            days += (uint32_t)(day - 1);

            uint32_t seconds = days * 86400UL + (uint32_t)hour * 3600UL + (uint32_t)minute * 60UL + (uint32_t)second;
            uint32_t ms = seconds * 1000UL;

            return ms;
        }
#endif

#ifdef USE_SYSTIME_TICKS
#if defined(FREERTOS) || defined(USE_FREERTOS) || defined(configUSE_PREEMPTION)
    // 判断是否在中断中
    if (__get_IPSR() != 0) {
        // 在中断
        return (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS);
    } else {
        // 任务上下文
        return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }
#else
    // HAL_GetTick() is not guaranteed to be thread safe, so we must check if we are in an interrupt context.
    if (__get_IPSR() != 0) {
        // In interrupt context, use a safe method to read the tick count.
        // This is a simple example and may not be perfectly accurate under heavy interrupt load.
        uint32_t tick_count;
        do {
            tick_count = HAL_GetTick();
        } while (tick_count != HAL_GetTick()); // Ensure we get a stable reading
        return tick_count;
    } else {
        // In task context, we can safely call HAL_GetTick()
        return HAL_GetTick();
    }
#endif
}

uint32_t system_get_time_s(void)
{
    return system_get_time_ms() / 1000U;
}

// Return the time/date relative to the high precision Ethernet timer.
static bool systime_shell_date(int argc, char **argv)
{
  char buffer[32];

  // Get the date from system time.
  systime_str(buffer, sizeof(buffer));

  // Print the string.
  shell_printf("%s\n", buffer);

  return true;
}

// Intialize the system time.  This must be called after
// Ethernet hardware is initialized.
void systime_init(void)
{
  // HACK 
  // ptptime_t ptptime;
  // // Initialize Ethernet clock to Wednesday, January 1, 2020 12:00:00 AM (GMT).
  // // See: https://www.epochconverter.com/
  // ptptime.tv_sec = 1577836800;
  // ptptime.tv_nsec = 0;
  // ethptp_set_time(&ptptime);

  // HACK Add the shell command to print the date.
  // shell_add_command("date", systime_shell_date);

  // Consider system time valid.
  // XXX This actually should only be set once
  // XXX PTPD has established the system time.
  systime_valid = true;
}

// Returns true of system time is considered valid.
bool systime_is_valid(void)
{
  return systime_valid;
}

// Get the system time as nanoseconds since 1970.  We must be able to call this
// function from interrupt service routines for certain functionality.
int64_t systime_get(void)
{
  int64_t systime;
  
  // Get current system time in seconds and convert to nanoseconds.
  uint32_t seconds = system_get_time_s();
  systime = (int64_t)seconds * 1000000000LL;

  // Return the system time.
  return systime;
}

// Set the system time as nanoseconds since 1970.
void systime_set(int64_t systime)
{
//   HACK 
//   ptptime_t ptptime;

//   // Get the nanoseconds portion of the time.
//   ptptime.tv_nsec = systime % 1000000000;

//   // Adjust to the seconds.
//   systime = systime / 1000000000;

// #ifdef USE_SYSTIME_GPS_EPOCH
//   // Adjust to time since 1970 (Unix epoch). There was an offset of 315964800 seconds
//   // between Unix and GPS time when GPS time began, that offset changes each time
//   // there is a leap second. We currently don't factor in leap seconds.
//   // See: https://www.andrews.edu/~tzs/timeconv/timealgorithm.html
//   systime += 315964800;
// #endif

//   // Get the seconds portion of the time.
//   ptptime.tv_sec = (int32_t) systime;

//   // Set the system time.
//   ethptp_set_time(&ptptime);
}

// Adjust the system clock rate by the indicated value in parts-per-billion.
void systime_adjust(int32_t adjustment)
{
  // Pass the adjustment to ethernet clock.
  ethptp_adj_freq(adjustment);
}

// Create a formatted system time similar to strftime().
size_t systime_str(char *buffer, size_t buflen)
{
  extern uint32_t board_get_time_s(void);
  time_t seconds1970;
  struct tm now;

  // Get the seconds since 1970 (Unix epoch) from system time.
  seconds1970 = (time_t)board_get_time_s();

  // Break the seconds to a time structure.
  _localtime_r(&seconds1970, &now);

  // Format into a string.  We don't use strftime() to reduce stack usage.
  // return strftime(buffer, buflen, "%a %b %d %H:%M:%S UTC %Y\n", &now);
  return snoutputf(buffer, buflen, "%s %s %02d %02d:%02d:%02d UTC %4d",
                   days[now.tm_wday], months[now.tm_mon], now.tm_mday,
                   now.tm_hour, now.tm_min, now.tm_sec, 1900 + now.tm_year);
}

// Create a formatted log time similar to strftime().
size_t systime_log(char *buffer, size_t buflen)
{
  extern uint32_t board_get_time_s(void);
  time_t seconds1970;
  struct tm now;

  // Get the seconds since 1970 (Unix epoch) from system time.
  seconds1970 = (time_t)board_get_time_s();

  // Break the seconds to a time structure.
  _localtime_r(&seconds1970, &now);

  // Format into a string.  We don't use strftime() to reduce stack usage.
  // return strftime(buffer, buflen, "%b %e %H:%M:%S ", &now);
  return snoutputf(buffer, buflen, "%s %2d %02d:%02d:%02d ",
                   months[now.tm_mon], now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
}

