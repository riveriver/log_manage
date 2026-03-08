# syslog 使用说明

## 概述
`component/syslog.*` 提供 BSD Syslog 风格的轻量日志通道，适配 STM32 + LwIP：
- UDP 发送到远端 Syslog 服务器（默认端口 514，可配置）。
- 本地环形缓冲 (4 KB) 防丢日志，网络恢复后异步发送。
- 日志级别过滤，低于当前级别的日志自动丢弃。
- 基于 CMSIS-RTOS2 mutex，线程安全；通过 `tcpip_callback` 在 LwIP 线程发送。

## 典型流程
```c
// init after network ready
syslog_set_hostname("STM32");
syslog_set_server_ip("192.168.1.1", 514);
syslog_set_level(LOG_LEVEL_DEBUG);
syslog_init();

// in main.h
#include "syslog.h"
#ifndef LOG_LEVEL_DEBUG
#define LOG_LEVEL_EMERGENCY     SYSLOG_LEVEL_EMERGENCY
#define LOG_LEVEL_ALERT         SYSLOG_LEVEL_ALERT
#define LOG_LEVEL_CRITICAL      SYSLOG_LEVEL_CRITICAL
#define LOG_LEVEL_ERROR         SYSLOG_LEVEL_ERROR
#define LOG_LEVEL_WARNING       SYSLOG_LEVEL_WARNING
#define LOG_LEVEL_NOTICE        SYSLOG_LEVEL_NOTICE
#define LOG_LEVEL_INFO          SYSLOG_LEVEL_INFO
#define LOG_LEVEL_DEBUG         SYSLOG_LEVEL_DEBUG
#endif
#define LOG_EMERG(tag, ...) syslog_printf(SYSLOG_LEVEL_EMERGENCY, __VA_ARGS__)
#define LOG_ALERT(tag, ...) syslog_printf(SYSLOG_LEVEL_ALERT, __VA_ARGS__)
#define LOG_CRIT(tag, ...) syslog_printf(SYSLOG_LEVEL_CRITICAL, __VA_ARGS__)
#define LOG_ERR(tag, ...) syslog_printf(SYSLOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_WARNING(tag, ...) syslog_printf(SYSLOG_LEVEL_WARNING, __VA_ARGS__)
#define LOG_NOTICE(tag, ...) syslog_printf(SYSLOG_LEVEL_NOTICE, __VA_ARGS__)
#define LOG_INFO(tag, ...) syslog_printf(SYSLOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DEBUG(tag, ...) syslog_printf(SYSLOG_LEVEL_DEBUG, __VA_ARGS__)

// call SYSLOG_INFO to printf msg
LOG_INFO("ETH", "Hello from syslog\r\n");
```