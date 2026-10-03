/**
 * @file            bbt_port_baremetal.c
 * @brief           Bare-metal port implementation and helpers for minimal systems.
 * Updated with Doxygen headers.
 * @date            25.05.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#include "bbt_port.h"

#if defined(BBT_OS_BAREMETAL)

#include "bbt_receiver.h"

#include <string.h>

/*
 * The application must provide this macro in bbt_config.h.
 *
 * Example:
 *   #define BBT_BAREMETAL_GET_TIME_MS()  systick_ms_get()
 */
#ifndef BBT_BAREMETAL_GET_TIME_MS
#error "BBT_BAREMETAL_GET_TIME_MS() must be defined in bbt_config.h for bare-metal mode"
#endif

/*
 * Optional critical section macros.
 *
 * If BLE write callback and bbt_poll() can run concurrently,
 * these should disable/enable interrupts or lock a lightweight critical section.
 */
#ifndef BBT_BAREMETAL_ENTER_CRITICAL
#define BBT_BAREMETAL_ENTER_CRITICAL() \
  do                                   \
  {                                    \
  }                                    \
  while (0)
#endif

#ifndef BBT_BAREMETAL_EXIT_CRITICAL
#define BBT_BAREMETAL_EXIT_CRITICAL() \
  do                                  \
  {                                   \
  }                                   \
  while (0)
#endif

#ifndef BBT_BAREMETAL_QUEUE_ITEM_SIZE
#define BBT_BAREMETAL_QUEUE_ITEM_SIZE (sizeof(bbt_rx_queue_item_t))
#endif

#ifndef BBT_BAREMETAL_QUEUE_DEPTH
#define BBT_BAREMETAL_QUEUE_DEPTH BBT_RX_QUEUE_DEPTH
#endif

typedef struct
{
  uint8_t *buffer;
  uint32_t item_size;
  uint32_t depth;
  uint32_t head;
  uint32_t tail;
  uint32_t count;
} bbt_baremetal_queue_t;

static bbt_baremetal_queue_t _queue;
static uint8_t               _queue_buffer[BBT_BAREMETAL_QUEUE_ITEM_SIZE * BBT_BAREMETAL_QUEUE_DEPTH];
static bool                  _queue_in_use = false;

bbt_os_thread_t bbt_os_thread_create(void (*entry)(void *), void *arg, const char *name, uint32_t stack_size,
                                     int priority)
{
  (void)entry;
  (void)arg;
  (void)name;
  (void)stack_size;
  (void)priority;

  /*
   * No thread in bare-metal mode.
   * User must call bbt_core_worker_service().
   */
  return NULL;
}

void bbt_os_thread_delete(bbt_os_thread_t thread)
{
  (void)thread;
}

bbt_os_queue_t bbt_os_queue_create(uint32_t item_size, uint32_t depth)
{
  if ((item_size == 0u) || (depth == 0u))
  {
    return NULL;
  }

  if ((item_size > BBT_BAREMETAL_QUEUE_ITEM_SIZE) || (depth > BBT_BAREMETAL_QUEUE_DEPTH))
  {
    return NULL;
  }

  if (_queue_in_use)
  {
    return NULL;
  }

  _queue.buffer    = _queue_buffer;
  _queue.item_size = item_size;
  _queue.depth     = depth;
  _queue.head      = 0u;
  _queue.tail      = 0u;
  _queue.count     = 0u;
  _queue_in_use    = true;

  return (bbt_os_queue_t)&_queue;
}

void bbt_os_queue_delete(bbt_os_queue_t queue)
{
  bbt_baremetal_queue_t *q = (bbt_baremetal_queue_t *)queue;

  if ((q == NULL) || (q != &_queue))
  {
    return;
  }

  _queue.count  = 0u;
  _queue.head   = 0u;
  _queue.tail   = 0u;
  _queue_in_use = false;
}

bool bbt_os_queue_send(bbt_os_queue_t queue, const void *item, uint32_t timeout_ms)
{
  (void)timeout_ms;

  bbt_baremetal_queue_t *q = (bbt_baremetal_queue_t *)queue;

  if ((q == NULL) || (item == NULL) || !_queue_in_use)
  {
    return false;
  }

  bool ok = false;

  BBT_BAREMETAL_ENTER_CRITICAL();

  if (q->count < q->depth)
  {
    uint8_t *dst = &q->buffer[q->head * q->item_size];
    memcpy(dst, item, q->item_size);

    q->head = (q->head + 1u) % q->depth;
    q->count++;

    ok = true;
  }

  BBT_BAREMETAL_EXIT_CRITICAL();

  return ok;
}

bool bbt_os_queue_receive(bbt_os_queue_t queue, void *item, uint32_t timeout_ms)
{
  bbt_baremetal_queue_t *q = (bbt_baremetal_queue_t *)queue;

  if ((q == NULL) || (item == NULL) || !_queue_in_use)
  {
    return false;
  }

  uint32_t start = BBT_BAREMETAL_GET_TIME_MS();

  do
  {
    bool ok = false;

    BBT_BAREMETAL_ENTER_CRITICAL();

    if (q->count > 0u)
    {
      uint8_t *src = &q->buffer[q->tail * q->item_size];
      memcpy(item, src, q->item_size);

      q->tail = (q->tail + 1u) % q->depth;
      q->count--;

      ok = true;
    }

    BBT_BAREMETAL_EXIT_CRITICAL();

    if (ok)
    {
      return true;
    }

    if (timeout_ms == 0u)
    {
      return false;
    }
  }
  while ((BBT_BAREMETAL_GET_TIME_MS() - start) < timeout_ms);

  return false;
}

bbt_os_mutex_t bbt_os_mutex_create(void)
{
  /*
   * Dummy non-NULL handle.
   */
  return (bbt_os_mutex_t)0x1;
}

void bbt_os_mutex_delete(bbt_os_mutex_t mutex)
{
  (void)mutex;
}

void bbt_os_mutex_lock(bbt_os_mutex_t mutex)
{
  (void)mutex;
  BBT_BAREMETAL_ENTER_CRITICAL();
}

void bbt_os_mutex_unlock(bbt_os_mutex_t mutex)
{
  (void)mutex;
  BBT_BAREMETAL_EXIT_CRITICAL();
}

uint32_t bbt_os_get_time_ms(void)
{
  return BBT_BAREMETAL_GET_TIME_MS();
}

void bbt_os_delay_ms(uint32_t delay_ms)
{
  uint32_t start = BBT_BAREMETAL_GET_TIME_MS();

  while ((BBT_BAREMETAL_GET_TIME_MS() - start) < delay_ms)
  {
    /*
     * Busy wait.
     * Application can replace this file if needed.
     */
  }
}

#endif /* BBT_OS_BAREMETAL */