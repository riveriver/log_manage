#ifndef __SYSLOG_H__
#define __SYSLOG_H__

// BSD Syslog Protocol
// See: https://www.ietf.org/rfc/rfc3164.txt

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
//#include "lwip/udp.h"
//#include "lwip/ip_addr.h"
//#include "lwip/ip4_addr.h"


#ifdef __cplusplus
extern "C" {
#endif

// Syslog wire-format selection (compile-time).
#define SYSLOG_STD_RFC3164 3164
#define SYSLOG_STD_RFC5424 5424
#ifndef SYSLOG_STD
#define SYSLOG_STD SYSLOG_STD_RFC3164
#endif

// Syslog output target selection (compile-time).
#define SYSLOG_OUTPUT_UART      0x01
#define SYSLOG_OUTPUT_UDP       0x02
#define SYSLOG_OUTPUT_UART_UDP  (SYSLOG_OUTPUT_UART | SYSLOG_OUTPUT_UDP)

#ifndef SYSLOG_OUTPUT_MODE
#define SYSLOG_OUTPUT_MODE SYSLOG_OUTPUT_UDP
#endif

// Syslog facility values.
enum
{
  SYSLOG_KERNEL = 0,        // kernel messages
  SYSLOG_USERLEVEL,         // user-level messages
  SYSLOG_MAIL,              // mail system
  SYSLOG_SYSTEM,            // system daemons
  SYSLOG_SECURITY,          // security/authorization messages
  SYSLOG_INTERNAL,          // messages generated internally by syslogd
  SYSLOG_PRINTER,           // line printer subsystem
  SYSLOG_NEWS,              // network news subsystem
  SYSLOG_UUCP,              // UUCP subsystem
  SYSLOG_CLOCK,             // clock daemon
  SYSLOG_AUTH,              // security/authorization messages
  SYSLOG_FTP,               // FTP daemon
  SYSLOG_NTP,               // NTP subsystem
  SYSLOG_LOGAUDIT,          // log audit
  SYSLOG_LOGALERT,          // log alert
  SYSLOG_CLOCK2,            // clock daemon
  SYSLOG_LOCAL0,            // local use 0  (local0)
  SYSLOG_LOCAL1,            // local use 1  (local1)
  SYSLOG_LOCAL2,            // local use 2  (local2)
  SYSLOG_LOCAL3,            // local use 3  (local3)
  SYSLOG_LOCAL4,            // local use 4  (local4)
  SYSLOG_LOCAL5,            // local use 5  (local5)
  SYSLOG_LOCAL6,            // local use 6  (local6)
  SYSLOG_LOCAL7             // local use 7  (local7)
};

// Syslog severity values.
enum
{
  SYSLOG_LEVEL_EMERGENCY = 0,     // Emergency: system is unusable
  SYSLOG_LEVEL_ALERT,             // Alert: action must be taken immediately
  SYSLOG_LEVEL_CRITICAL,          // Critical: critical conditions
  SYSLOG_LEVEL_ERROR,             // Error: error conditions
  SYSLOG_LEVEL_WARNING,           // Warning: warning conditions
  SYSLOG_LEVEL_NOTICE,            // Notice: normal but significant condition
  SYSLOG_LEVEL_INFO,              // Informational: informational messages
  SYSLOG_LEVEL_DEBUG              // Debug: debug-level messages
};

void syslog_init(void);
void syslog_clear(void);
void syslog_set_echo(bool enable);
bool syslog_get_echo(void);
void syslog_set_level(int severity);
int syslog_get_level(void);
void syslog_printf(int severity, const char *fmt, ...);
void syslog_printf_tag(int severity, const char *tag, const char *fmt, ...);
int8_t syslog_set_server_ip(const char *addr_str, uint16_t port);
void syslog_set_hostname(const char *hostname);

#ifdef __cplusplus
}
#endif

#endif /* __SYSLOG_H__ */
