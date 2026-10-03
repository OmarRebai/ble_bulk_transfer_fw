/**
 * @file            bbt_config.h
 * @brief           BBT configuration of the ESP-IDF component, set from Kconfig (menuconfig: BLE Bulk Transfer).
 * @date            03.10.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#ifndef BBT_CONFIG_H
#define BBT_CONFIG_H

#include "sdkconfig.h"

#define BBT_OS_ESP_IDF

#define BBT_RX_QUEUE_DEPTH          CONFIG_BBT_RX_QUEUE_DEPTH
#define BBT_RX_THREAD_STACK_SIZE    CONFIG_BBT_RX_THREAD_STACK_SIZE
#define BBT_RX_THREAD_PRIORITY      CONFIG_BBT_RX_THREAD_PRIORITY

#if CONFIG_BBT_RX_THREAD_CORE < 0
#define BBT_ESP_IDF_TASK_CORE       tskNO_AFFINITY
#else
#define BBT_ESP_IDF_TASK_CORE       CONFIG_BBT_RX_THREAD_CORE
#endif

#define BBT_MAX_CHUNK_SIZE          CONFIG_BBT_MAX_CHUNK_SIZE
#define BBT_MAX_TOTAL_CHUNKS        CONFIG_BBT_MAX_TOTAL_CHUNKS
#define BBT_MAX_NOTIFY_SIZE         CONFIG_BBT_MAX_NOTIFY_SIZE

#define BBT_WORKER_POLL_MS          CONFIG_BBT_WORKER_POLL_MS
#define BBT_IDLE_TIMEOUT_MS         CONFIG_BBT_IDLE_TIMEOUT_MS
#define BBT_NACK_RETRY_MAX          CONFIG_BBT_NACK_RETRY_MAX
#define BBT_SENDER_CHUNK_DELAY_MS   CONFIG_BBT_SENDER_CHUNK_DELAY_MS

#define BBT_BLE_SEND_RETRY_MAX      CONFIG_BBT_BLE_SEND_RETRY_MAX
#define BBT_BLE_SEND_RETRY_DELAY_MS CONFIG_BBT_BLE_SEND_RETRY_DELAY_MS

/* The export path builds a chunk packet (8-byte header + payload) in the transmit buffer. */
#if BBT_MAX_NOTIFY_SIZE < (BBT_MAX_CHUNK_SIZE + 8)
#error "CONFIG_BBT_MAX_NOTIFY_SIZE must be at least CONFIG_BBT_MAX_CHUNK_SIZE + 8"
#endif

#endif /* BBT_CONFIG_H */
