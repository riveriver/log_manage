#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include "cmsis_os2.h"
#include "lwip/opt.h"
#include "lwip/api.h"
#include "lwip/udp.h"
#include "lwip/tcpip.h"
#include "lwip/ip4_addr.h"
//#include "shell.h"
#include "outputf.h"
#include "syslog.h"
//#include "console.h"
#include "systime.h"

// Set the following to 1 to enable network syslog.
#if !defined(USE_NETWORK_SYSLOG)
#define USE_NETWORK_SYSLOG      1
#endif

#if (SYSLOG_STD != SYSLOG_STD_RFC3164) && (SYSLOG_STD != SYSLOG_STD_RFC5424)
#error "SYSLOG_STD must be SYSLOG_STD_RFC3164 or SYSLOG_STD_RFC5424"
#endif

#if (SYSLOG_OUTPUT_MODE != SYSLOG_OUTPUT_UART) && \
  (SYSLOG_OUTPUT_MODE != SYSLOG_OUTPUT_UDP) && \
  (SYSLOG_OUTPUT_MODE != SYSLOG_OUTPUT_UART_UDP)
#error "SYSLOG_OUTPUT_MODE must be SYSLOG_OUTPUT_UART, SYSLOG_OUTPUT_UDP or SYSLOG_OUTPUT_UART_UDP"
#endif

#if ((SYSLOG_OUTPUT_MODE & SYSLOG_OUTPUT_UART) != 0)
#define SYSLOG_ENABLE_UART 1
#else
#define SYSLOG_ENABLE_UART 0
#endif

#if (((SYSLOG_OUTPUT_MODE & SYSLOG_OUTPUT_UDP) != 0) && (USE_NETWORK_SYSLOG == 1))
#define SYSLOG_ENABLE_UDP 1
#else
#define SYSLOG_ENABLE_UDP 0
#endif

// Size of the syslog buffer.
#define SYSLOG_SEND_SIZE              192
#define SYSLOG_BUFFER_SIZE            4096

// Syslog initialized flag.
static bool syslog_initialized = false;

// Syslog console echo flag.
static bool syslog_console_echo = true;

// Syslog level value.
static int syslog_level = SYSLOG_LEVEL_NOTICE;

// Syslog UDP control block.
static struct udp_pcb *syslog_pcb = NULL;

// Syslog destination address (configurable externally).
static ip4_addr_t syslog_dest_ip;
static bool syslog_dest_configured = false;
static uint16_t syslog_dest_port = 514;
static bool syslog_port_configured = false;

// Syslog hostname.
static char syslog_hostname[32] = "stm32";

// Send buffer used to send a syslog message.
static uint32_t syslog_send_len = 0;
static char syslog_send_buffer[SYSLOG_SEND_SIZE];

// Buffered log structures.
static uint32_t syslog_buffer_head = 0;
static uint32_t syslog_buffer_tail = 0;
static uint32_t syslog_buffer_dump = 0;
static char syslog_buffer[SYSLOG_BUFFER_SIZE];

// Strings that store log levels.
static const char *syslog_levels[8] =
{
  "emergency",
  "alert",
  "critical",
  "error",
  "warning",
  "notice",
  "info",
  "debug"
};

// Mutex protection.
osMutexId_t syslog_mutex_id = NULL;

// System configurable syslog IP address.
ip4_addr_t syslog_get_server_ip(void)
{
  // Provide a default if none has been configured yet.
  if (!syslog_dest_configured)
  {
    IP4_ADDR(&syslog_dest_ip, 192, 168, 1, 1);
    syslog_dest_configured = true;
  }

  return syslog_dest_ip;
}

// System configurable syslog port.
uint16_t syslog_get_server_port(void)
{
  // Provide a default if none has been configured yet.
  if (!syslog_port_configured)
  {
    syslog_dest_port = 514;
    syslog_port_configured = true;
  }

  return syslog_dest_port;
}

// Get the next character from the syslog buffer.
static char syslog_getc(void)
{
  char ch;

  // If there are no characters, return end-of-string character.
  if (syslog_buffer_tail == syslog_buffer_head) return 0;

  // Get the character to return.
  ch = syslog_buffer[syslog_buffer_tail];

  // Increment the tail.
  syslog_buffer_tail += 1;

  // Keep within the bounds of the buffer.
  if (syslog_buffer_tail >= SYSLOG_BUFFER_SIZE) syslog_buffer_tail = 0;

  return ch;
}

// Return if the syslog buffer is empty or not.
static bool syslog_buffer_empty(void)
{
  return syslog_buffer_tail == syslog_buffer_head ? true : false;
}

// Fill the send buffer with the next message.
static int syslog_next_message(void)
{
  char ch;

  // Reset the send length.
  syslog_send_len = 0;

  // Get the next character from the syslog buffer.
  ch = syslog_getc();

  // Insert the character into the send buffer.
  syslog_send_buffer[syslog_send_len] = ch;

  // Increment the send length.
  syslog_send_len += 1;

  // Keep going until we have extracted the next message.
  while (ch != 0)
  {
    // Get the next character from the syslog buffer.
    ch = syslog_getc();

    // Prevent overflowing the send buffer.
    if (syslog_send_len < SYSLOG_SEND_SIZE)
    {
      // Insert the character into the send buffer.
      syslog_send_buffer[syslog_send_len] = ch;

      // Increment the send length.
      syslog_send_len += 1;
    }
  }

  // Make sure the send buffer is always terminated by an end-of-string character.
  syslog_send_buffer[syslog_send_len - 1] = 0;

  // Return the length of the send buffer minus the end-of-string termination.
  return (int) syslog_send_len - 1;
}

// Put a character into the syslog.
static void syslog_putc(int ch)
{
  // Place the character at the head of the buffer.
  syslog_buffer[syslog_buffer_head] = (char) ch;

  // Increment the head within the bounds of the buffer.
  syslog_buffer_head = (syslog_buffer_head + 1) % SYSLOG_BUFFER_SIZE;

  // Have we collided with the tail indicating the buffer is full.
  if (syslog_buffer_head == syslog_buffer_tail)
  {
    // Increment the tail within the bounds of the buffer.
    syslog_buffer_tail = (syslog_buffer_tail + 1) % SYSLOG_BUFFER_SIZE;

    // Keep going until the entire message is dropped.
    while ((syslog_buffer[syslog_buffer_tail] != 0) && (syslog_buffer_tail != syslog_buffer_head))
    {
      // Increment the tail within the bounds of the buffer.
      syslog_buffer_tail = (syslog_buffer_tail + 1) % SYSLOG_BUFFER_SIZE;
    }
  }

  // Have we collided with the dump tail indicating the buffer is full.
  if (syslog_buffer_head == syslog_buffer_dump)
  {
    // Increment the dump tail within the bounds of the buffer.
    syslog_buffer_dump = (syslog_buffer_dump + 1) % SYSLOG_BUFFER_SIZE;

    // Keep going until the entire message is dropped.
    while ((syslog_buffer[syslog_buffer_dump] != 0) && (syslog_buffer_dump != syslog_buffer_head))
    {
      // Increment the dump tail within the bounds of the buffer.
      syslog_buffer_dump = (syslog_buffer_dump + 1) % SYSLOG_BUFFER_SIZE;
    }
  }
}

// Dump the buffered serial data to the telnet port.
static bool syslog_shell_dmesg(int argc, char **argv)
{
  // Lock the syslog mutex.
  if (syslog_mutex_id && (osMutexAcquire(syslog_mutex_id, osWaitForever) == osOK))
  {
    // Start at the dump tail.
    uint32_t index = syslog_buffer_dump;

    // Keep going until we reach the head.
    while (index != syslog_buffer_head)
    {
      // Get the next character to output.
      int ch = (int) syslog_buffer[index];

      // Output the character and substitue nulls with CR characters.
      // HACK shell_putc(ch == 0 ? '\n' : ch);

      // Increment the index within the bounds of the buffer.
      index = (index + 1) % SYSLOG_BUFFER_SIZE;
    }

    // Release the mutex.
    osMutexRelease(syslog_mutex_id);
  }

  return true;
}

// Send a message to syslog.
static bool syslog_shell_syslog(int argc, char **argv)
{
  bool help = false;

  // Parse the command.
  if (argc > 1)
  {
    if (!strcasecmp(argv[1], "clear"))
    {
      // Clear the syslog buffer.
      syslog_clear();
    }
    else if (!strcasecmp(argv[1], "echo"))
    {
      // Are we setting or just getting?
      if (argc > 2)
      {
        // Set the echo flag.
        syslog_set_echo(!strcasecmp(argv[2], "on"));
      }

      // HACK Display the echo flag.
      // shell_printf("echo: %s\n", syslog_get_echo() ? "on" : "off");
    }
    else if (!strcasecmp(argv[1], "level"))
    {
      // Are we setting or just getting?
      if (argc > 2)
      {
        int level = syslog_level;

        // Try to parse the level as a string, then as a number.
        if (!strcasecmp(argv[2], syslog_levels[0])) level = SYSLOG_LEVEL_EMERGENCY;
        else if (!strcasecmp(argv[2], syslog_levels[1])) level = SYSLOG_LEVEL_ALERT;
        else if (!strcasecmp(argv[2], syslog_levels[2])) level = SYSLOG_LEVEL_CRITICAL;
        else if (!strcasecmp(argv[2], syslog_levels[3])) level = SYSLOG_LEVEL_ERROR;
        else if (!strcasecmp(argv[2], syslog_levels[4])) level = SYSLOG_LEVEL_WARNING;
        else if (!strcasecmp(argv[2], syslog_levels[5])) level = SYSLOG_LEVEL_NOTICE;
        else if (!strcasecmp(argv[2], syslog_levels[6])) level = SYSLOG_LEVEL_INFO;
        else if (!strcasecmp(argv[2], syslog_levels[7])) level = SYSLOG_LEVEL_DEBUG;
        else level = atoi(argv[2]);

        // Set the logging level.
        syslog_set_level(level);
      }

      // HACK Display the current level.
      // shell_printf("level: %s (%d)\n", syslog_levels[syslog_get_level()], syslog_get_level());
    }
    else if (!strcasecmp(argv[1], "hostname"))
    {
      // Are we setting or just getting?
      if (argc > 2)
      {
        // Set the hostname.
        syslog_set_hostname(argv[2]);
      }

      // HACK Display the current hostname.
      // shell_printf("hostname: %s\n", syslog_hostname);
    }
    else if (argc > 2)
    {
      int severity = atoi(argv[1]);

      // Handle various arguments.
      if (argc == 3)
        syslog_printf(severity, "%s", argv[2]);
      else if (argc == 4)
        syslog_printf(severity, "%s %s", argv[2], argv[3]);
      else if (argc == 5)
        syslog_printf(severity, "%s %s %s", argv[2], argv[3], argv[4]);
      else if (argc == 6)
        syslog_printf(severity, "%s %s %s %s", argv[2], argv[3], argv[4], argv[5]);
      else if (argc == 7)
        syslog_printf(severity, "%s %s %s %s %s", argv[2], argv[3], argv[4], argv[5], argv[6]);
      else if (argc == 8)
        syslog_printf(severity, "%s %s %s %s %s %s", argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
      else
        // We need help.
        help = true;
    }
    else
    {
      // We need help.
      help = true;
    }
  }
  else
  {
    // We need help.
    help = true;
  }

  // Do we need to display help?
  if (help)
  {
    // HACK
    // shell_puts("  syslog 0-7 message\n");
    // shell_puts("  syslog echo [on|off]\n");
    // shell_puts("  syslog clear\n");
    // shell_puts("  syslog level [emergency|alert|critical|error|warning|notice|info|debug|0-7]\n");
    // shell_puts("  syslog hostname [name]\n");
  }

  return true;
}

// Send syslog packets within the context of the tcpip thread.
static void syslog_send_callback(void *arg)
{
#if SYSLOG_ENABLE_UDP
  // If syslog isn't ready, drop any buffered data.
  if (!syslog_initialized || syslog_pcb == NULL) return;

  // Get buffered messages until we get an empty buffer.
  while (!syslog_buffer_empty())
  {
    // Lock the syslog mutex.
    if (syslog_mutex_id && (osMutexAcquire(syslog_mutex_id, osWaitForever) == osOK))
    {
      // Get the next syslog message.
      int msg_len = syslog_next_message();

      // Only send a message if syslog is enabled and the buffer is a sane length.
      if (syslog_send_len > 8)
      {
#if SYSLOG_ENABLE_UDP
        // Allocate the syslog packet buffer with the actual message length (exclude terminator).
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, msg_len, PBUF_RAM);

        // Did the allocation succeed?
        if (p != NULL)
        {
          // Copy only the actual message data (excluding null terminator).
          memcpy(p->payload, syslog_send_buffer, msg_len);
          p->len = msg_len;
          p->tot_len = msg_len;

          // Send the syslog message.
          udp_send(syslog_pcb, p);

          // Free the syslog packet buffer.
          pbuf_free(p);
        }
#endif
      }

      // Release the syslog mutex.
      osMutexRelease(syslog_mutex_id);
    }
  }
#else
  (void)arg;
#endif
}

// Reconnect syslog UDP connection with the current destination IP.
static void syslog_reconnect_callback(void *arg)
{
  (void) arg;

  if (syslog_pcb == NULL) return;

  udp_disconnect(syslog_pcb);
  udp_connect(syslog_pcb, &syslog_dest_ip, syslog_get_server_port());
}

// Initialize within the context of the tcpip thread.
static void syslog_init_callback(void *arg)
{
  ip4_addr_t dest_ip_addr;

  // Get the address we are sending packets to.
  dest_ip_addr = syslog_get_server_ip();

  // Initialize a new UDP control block.
  if ((syslog_pcb = udp_new()) != NULL)
  {
    // Bind the pcb to a local address.
    if (udp_bind(syslog_pcb, IP_ADDR_ANY, syslog_get_server_port()) == ERR_OK)
    {
      // Set up the connection.
      if (udp_connect(syslog_pcb, &dest_ip_addr, syslog_get_server_port()) == ERR_OK)
      {
        // HACK Add the shell command.
        // shell_add_command("dmesg", syslog_shell_dmesg);
        // shell_add_command("syslog", syslog_shell_syslog);

        // We succeeded and syslog is now initialized.
        syslog_initialized = true;
      }
    }
  }
}

// Initialize log buffering.
void syslog_init(void)
{
  // Initialize the mutex attributes. Note this is not a recursive mutex, but it does inherit priority.
  osMutexAttr_t syslog_mutex_attrs =
  {
    .name = "syslog",
    .attr_bits = osMutexPrioInherit
  };

  // Create the syslog mutex.
  syslog_mutex_id = osMutexNew(&syslog_mutex_attrs);
  if (!syslog_mutex_id)
  {
    // HACK Report error to console.
    outputf("SYSLOG: unable to create mutex\n");
  }

  // We need to initialize within the context of the tcpip thread.
#if SYSLOG_ENABLE_UDP
  tcpip_callback(syslog_init_callback, NULL);
#else
  // UART-only mode: no UDP init required.
  syslog_initialized = true;
#endif
}

// Clear the syslog buffer.
void syslog_clear(void)
{
  // Sanity check we are initialized.
  if (!syslog_initialized) return;

  // Lock the syslog mutex.
  osMutexAcquire(syslog_mutex_id, osWaitForever);

  // Reset the buffer indices.
  syslog_buffer_head = 0;
  syslog_buffer_tail = 0;
  syslog_buffer_dump = 0;

  // Release the mutex.
  osMutexRelease(syslog_mutex_id);
}

// Set the syslog echo to console flag.
void syslog_set_echo(bool enable)
{
#if SYSLOG_ENABLE_UART
  // Set the syslog echo to console flag.
  syslog_console_echo = enable;
#else
  (void)enable;
#endif
}

// Get the syslog echo to console flag.
bool syslog_get_echo(void)
{
#if SYSLOG_ENABLE_UART
  // Get the syslog echo to console flag.
  return syslog_console_echo;
#else
  return false;
#endif
}

// Set the syslog logging level.
void syslog_set_level(int severity)
{
  // Sanity check the severity.
  if (severity > SYSLOG_LEVEL_DEBUG) severity = SYSLOG_LEVEL_DEBUG;
  if (severity < SYSLOG_LEVEL_EMERGENCY) severity = SYSLOG_LEVEL_EMERGENCY;

  // Set the logging level.
  syslog_level = severity;
}

// Get the syslog logging level.
int syslog_get_level(void)
{
  return syslog_level;
}

// RFC5424 version field.
#define SYSLOG_RFC5424_VERSION 1

// Build a RFC5424 APP-NAME token (NILVALUE '-' if empty).
static void syslog_app_name_token(const char *tag, char *out, size_t out_len)
{
  size_t i = 0;
  size_t j = 0;

  if (!out || out_len == 0) return;

  if (!tag || !tag[0])
  {
    out[0] = '-';
    out[1] = 0;
    return;
  }

  // RFC5424 APP-NAME max length is 48 chars.
  while (tag[i] && j < (out_len - 1) && j < 48)
  {
    char c = tag[i++];
    if (c <= 32 || c > 126) c = '_';
    out[j++] = c;
  }

  if (j == 0)
  {
    out[0] = '-';
    out[1] = 0;
    return;
  }

  out[j] = 0;
}

// Build RFC3164 TAG token (best effort, printable chars only).
static void syslog_tag_token_3164(const char *tag, char *out, size_t out_len)
{
  size_t i = 0;
  size_t j = 0;

  if (!out || out_len == 0) return;

  if (!tag || !tag[0])
  {
    out[0] = 0;
    return;
  }

  while (tag[i] && j < (out_len - 1))
  {
    char c = tag[i++];
    if (c <= 32 || c > 126) c = '_';
    out[j++] = c;
  }

  out[j] = 0;
}

// Core formatter for RFC5424 output.
static void syslog_vprintf_ex(int severity, const char *tag, const char *format, va_list args)
{
#if SYSLOG_ENABLE_UDP
  int i;
  int offset;
  int priority;
#if (SYSLOG_STD == SYSLOG_STD_RFC5424)
  char app_name[49];
#else
  char tag3164[49];
#endif
#endif
  va_list args_copy;

  if (!format) return;

  // Should we echo the syslog data to the console?
#if SYSLOG_ENABLE_UART
  if (syslog_console_echo && syslog_mutex_id && (osMutexAcquire(syslog_mutex_id, osWaitForever) == osOK))
  {
    if (tag && tag[0])
    {
      outputf("[%s] ", tag);
    }

    va_copy(args_copy, args);
    voutputf(format, args_copy);
    va_end(args_copy);
    outputf("\n");

    // Release the mutex.
    osMutexRelease(syslog_mutex_id);
  }
#endif

#if SYSLOG_ENABLE_UDP

  // Sanity check we are initialized.
  if (!syslog_initialized) return;

  // Sanity check the severity.
  if (severity > SYSLOG_LEVEL_DEBUG) severity = SYSLOG_LEVEL_DEBUG;
  if (severity < SYSLOG_LEVEL_EMERGENCY) severity = SYSLOG_LEVEL_EMERGENCY;

  // Skip severities greater than our current logging level.
  if (severity > syslog_level) return;

  // Determine the priority.
  priority = (SYSLOG_LOCAL0 << 3) | severity;

#if (SYSLOG_STD == SYSLOG_STD_RFC5424)
  syslog_app_name_token(tag, app_name, sizeof(app_name));
#else
  syslog_tag_token_3164(tag, tag3164, sizeof(tag3164));
#endif

  // Lock the syslog mutex.
  if (syslog_mutex_id && (osMutexAcquire(syslog_mutex_id, osWaitForever) == osOK))
  {
#if (SYSLOG_STD == SYSLOG_STD_RFC5424)
    // RFC5424 header: <PRI>VERSION TIMESTAMP HOSTNAME APP-NAME PROCID MSGID SD
    offset = snoutputf(syslog_send_buffer, sizeof(syslog_send_buffer), "<%d>%d ", priority, SYSLOG_RFC5424_VERSION);
    offset += systime_log_rfc5424(syslog_send_buffer + offset, sizeof(syslog_send_buffer) - offset);
    offset += snoutputf(syslog_send_buffer + offset, sizeof(syslog_send_buffer) - offset,
                        " %s %s - - - ",
                        syslog_hostname[0] ? syslog_hostname : "-",
                        app_name);
#else
    // RFC3164 header (traditional BSD syslog): <PRI>TIMESTAMP HOSTNAME TAG:
    offset = snoutputf(syslog_send_buffer, sizeof(syslog_send_buffer), "<%d>", priority);
    offset += systime_log(syslog_send_buffer + offset, sizeof(syslog_send_buffer) - offset);
    offset += snoutputf(syslog_send_buffer + offset, sizeof(syslog_send_buffer) - offset,
                        "%s",
                        syslog_hostname[0] ? syslog_hostname : "-");

    if (tag3164[0])
    {
      offset += snoutputf(syslog_send_buffer + offset, sizeof(syslog_send_buffer) - offset,
                          " %s:", tag3164);
    }

    offset += snoutputf(syslog_send_buffer + offset, sizeof(syslog_send_buffer) - offset, " ");
#endif

    // MSG payload
    va_copy(args_copy, args);
    offset += vsnoutputf(syslog_send_buffer + offset, sizeof(syslog_send_buffer) - offset, format, args_copy);
    va_end(args_copy);

    // Insert each character from the send buffer to the syslog buffer.
    for (i = 0; i < sizeof(syslog_send_buffer) && syslog_send_buffer[i]; ++i)
      syslog_putc(syslog_send_buffer[i]);

    // Insert the end-of-string character.
    syslog_putc(0);

    // Release the mutex.
    osMutexRelease(syslog_mutex_id);
  }

  // We need to send within the context of the tcpip thread.
  tcpip_callback(syslog_send_callback, NULL);
#else
  (void)severity;
  (void)tag;
#endif
}

// Output a formatted syslog message.
void syslog_printf(int severity, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  syslog_vprintf_ex(severity, NULL, format, args);
  va_end(args);
}

// Output a formatted syslog message with module tag mapped to RFC5424 APP-NAME.
void syslog_printf_tag(int severity, const char *tag, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  syslog_vprintf_ex(severity, tag, format, args);
  va_end(args);
}

// Update destination and port then reconnect if already initialized.
static void syslog_apply_destination(ip4_addr_t addr, uint16_t port)
{
  syslog_dest_ip = addr;
  syslog_dest_configured = true;

  if (port == 0) port = 514;
  syslog_dest_port = port;
  syslog_port_configured = true;

  // If already initialized, reconnect with the new destination.
  if (syslog_initialized && syslog_pcb)
  {
    tcpip_callback(syslog_reconnect_callback, NULL);
  }
}

// Set syslog server using string form, e.g. "192.168.1.36" and port.
int8_t syslog_set_server_ip(const char *addr_str, uint16_t port)
{
  ip4_addr_t addr;

  if (!addr_str || !ip4addr_aton(addr_str, &addr))
  {
    // Failed to parse address; keep previous configuration.
    return 1;
  }

  syslog_apply_destination(addr, port);
  return 0;
}

// Set the syslog hostname.
void syslog_set_hostname(const char *hostname)
{
  if (hostname)
  {
    strncpy(syslog_hostname, hostname, sizeof(syslog_hostname) - 1);
    syslog_hostname[sizeof(syslog_hostname) - 1] = 0;
  }
}
