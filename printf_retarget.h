/*
 * retarget.h
 *
 *  Created on: 12-Sep-2021
 *      Author: Praveen Khot
 */

#ifndef INC_RETARGET_H_
#define INC_RETARGET_H_

#include "stm32h7xx_hal.h"
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

void specify_redirect_uart(UART_HandleTypeDef *huart);
int _write(int fd, char* ptr, int len);
int _read(int fd, char* ptr, int len);

#ifdef __cplusplus
}
#endif

#endif /* INC_RETARGET_H_ */
