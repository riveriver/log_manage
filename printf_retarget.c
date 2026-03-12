// https://github.com/cnoviello/mastering-stm32/blob/master/nucleo-f030R8/system/src/retarget/retarget.c

#include "printf_retarget.h"

#include <_ansi.h>
#include <_syslist.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/times.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>

#include "main.h"
#include "cmsis_os.h"
#include "stm32h7xx_hal.h"
#include "task.h"

#if !defined(OS_USE_SEMIHOSTING)

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

UART_HandleTypeDef *printf_uart;

void specify_redirect_uart(UART_HandleTypeDef *huart) {
  printf_uart = huart;
  /* Disable I/O buffering for STDOUT stream, so that
   * chars are sent out as soon as they are printed. */
  setvbuf(stdout, NULL, _IONBF, 0);
}

int _write(int fd, char* ptr, int len) {

  if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
    errno = EBADF;
    return -1;
  }

  if (printf_uart == NULL) {
    errno = EBADF;
    return -1;
  }

  if (ptr == NULL) {
    errno = EINVAL;
    return -1;
  }

  if (len < 0 || len > UINT16_MAX) {
    errno = EINVAL;
    return -1;
  }

  if (len == 0) {
    return 0;
  }

  HAL_StatusTypeDef hstatus;
  hstatus = HAL_UART_Transmit(printf_uart, (uint8_t *)ptr, (uint16_t)len, 10);
  if (hstatus == HAL_OK) {
    return len;
  }

  errno = EIO;
  return -1;
}

int _read(int fd, char* ptr, int len) {

  if (fd != STDIN_FILENO) {
    errno = EBADF;
    return -1;
  }

  if (printf_uart == NULL) {
    errno = EBADF;
    return -1;
  }

  if (ptr == NULL) {
    errno = EINVAL;
    return -1;
  }

  if (len < 0) {
    errno = EINVAL;
    return -1;
  }

  if (len == 0) {
    return 0;
  }

  HAL_StatusTypeDef hstatus;
  hstatus = HAL_UART_Receive(printf_uart, (uint8_t *)ptr, 1, 100);
  if (hstatus == HAL_OK) {
    return 1;
  }

  errno = EIO;
  return -1;
}


#endif //#if !defined(OS_USE_SEMIHOSTING)
