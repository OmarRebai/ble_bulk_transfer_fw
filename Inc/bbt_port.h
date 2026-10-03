/**
 * @file            bbt_port.h
 * @brief           Platform / OS abstraction layer required by the BBT module.
 * Updated with Doxygen headers.
 * @date            25.05.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#ifndef BBT_PORT_H
#define BBT_PORT_H

#include "bbt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Opaque OS abstraction types
 * ==========================================================================*/

typedef void *bbt_os_thread_t;
typedef void *bbt_os_queue_t;
typedef void *bbt_os_mutex_t;

#define BBT_OS_WAIT_FOREVER  0xFFFFFFFFu

/* ============================================================================
 * OS abstraction
 *
 * Implemented by:
 *   Src/port/bbt_port_cmsis.c
 *   Src/port/bbt_port_baremetal.c
 *   Src/port/bbt_port_sequencer.c
 * ==========================================================================*/

/**
 * @brief Create a platform thread for the BBT worker.
 *
 * @param entry Thread entry function accepting a single void* argument.
 * @param arg Argument passed to the entry function.
 * @param name Friendly thread name (platform dependent).
 * @param stack_size Stack size in bytes for the created thread.
 * @param priority Thread priority (platform dependent mapping).
 * @return Opaque thread handle or NULL on failure.
 */
bbt_os_thread_t bbt_os_thread_create(void (*entry)(void *),
                                     void *arg,
                                     const char *name,
                                     uint32_t stack_size,
                                     int priority);

/**
 * @brief Delete/terminate a previously created thread.
 *
 * @param thread Thread handle returned by bbt_os_thread_create().
 */
void bbt_os_thread_delete(bbt_os_thread_t thread);

/**
 * @brief Create a fixed-size queue for inter-context message passing.
 *
 * @param item_size Size of each queue element in bytes.
 * @param depth Maximum number of queued items.
 * @return Opaque queue handle or NULL on failure.
 */
bbt_os_queue_t bbt_os_queue_create(uint32_t item_size, uint32_t depth);

/**
 * @brief Delete a queue created with bbt_os_queue_create().
 */
void bbt_os_queue_delete(bbt_os_queue_t queue);

/**
 * @brief Push an item into the queue.
 *
 * @param queue Queue handle.
 * @param item Pointer to item memory to enqueue (copied into queue storage).
 * @param timeout_ms Maximum wait time in milliseconds (BBT_OS_WAIT_FOREVER supported).
 * @return true on success, false on timeout/failure.
 */
bool bbt_os_queue_send(bbt_os_queue_t queue,
                       const void *item,
                       uint32_t timeout_ms);

/**
 * @brief Receive an item from the queue.
 *
 * @param queue Queue handle.
 * @param item Out pointer to receive dequeued item.
 * @param timeout_ms Maximum wait time in milliseconds.
 * @return true if an item was received, false on timeout.
 */
bool bbt_os_queue_receive(bbt_os_queue_t queue,
                          void *item,
                          uint32_t timeout_ms);

/**
 * @brief Create an OS mutex/lock primitive.
 *
 * @return Opaque mutex handle or NULL on failure.
 */
bbt_os_mutex_t bbt_os_mutex_create(void);

/**
 * @brief Delete a mutex created by bbt_os_mutex_create().
 */
void bbt_os_mutex_delete(bbt_os_mutex_t mutex);

/**
 * @brief Acquire the provided mutex (blocking semantics platform dependent).
 */
void bbt_os_mutex_lock(bbt_os_mutex_t mutex);

/**
 * @brief Release the provided mutex.
 */
void bbt_os_mutex_unlock(bbt_os_mutex_t mutex);

/**
 * @brief Return current system time in milliseconds.
 *
 * The implementation must provide a monotonically increasing tick value.
 */
uint32_t bbt_os_get_time_ms(void);

/**
 * @brief Busy-wait or sleep for a number of milliseconds (platform dependent).
 */
void bbt_os_delay_ms(uint32_t delay_ms);

/* ============================================================================
 * Application/platform functions
 *
 * The final project MUST implement these functions.
 * ==========================================================================*/

/**
 * @brief Application BLE send primitive used by the BBT retry wrapper.
 *
 * The application owns the real BLE stack API call. Return 0 when the stack
 * accepted the packet for transmission, non-zero on busy/error.
 *
 * @param data Pointer to bytes to send.
 * @param len Number of bytes to send.
 * @param is_reliable true for control frames that should use the reliable app path.
 * @return 0 on success, non-zero on failure.
 */
int app_bbt_send(const uint8_t *data,
                 uint16_t len,
                 bool is_reliable);

/**
 * @brief Send a BLE notification to the connected peer.
 *
 * Used to transmit CHUNK protocol frames.
 *
 * @param data Pointer to bytes to send.
 * @param len Number of bytes to send.
 * @return 0 on success, non-zero on failure.
 */
int bbt_port_ble_notify(const uint8_t *data,
                        uint16_t len);

/**
 * @brief Send a BLE indication to the connected peer.
 *
 * Used to transmit START, START_ACK, END, NACK, COMPLETE and ERROR protocol
 * frames.
 *
 * @param data Pointer to bytes to send.
 * @param len Number of bytes to send.
 * @return 0 on success, non-zero on failure.
 */
int bbt_port_ble_indicate(const uint8_t *data,
                          uint16_t len);

/**
 * @brief Logging hook for the application/platform.
 *
 * Level uses BBT_LOG_* defines. This is a printf-style variadic API.
 */
void bbt_port_log(int level, const char *fmt, ...);

#define BBT_LOG_ERROR   0
#define BBT_LOG_WARN    1
#define BBT_LOG_INFO    2
#define BBT_LOG_DEBUG   3

#ifdef __cplusplus
}
#endif

#endif /* BBT_PORT_H */
