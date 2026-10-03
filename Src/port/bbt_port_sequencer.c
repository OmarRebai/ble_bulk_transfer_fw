/**
 * @file            bbt_port_sequencer.c
 * @brief           STM32 Utility Sequencer port implementation for BBT.
 * @date            04.06.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#include "bbt_port.h"

#if defined(BBT_OS_SEQUENCER)

#include "bbt_receiver.h"
#include "stm32_seq.h"

#include <string.h>

/*
 * Required application configuration:
 *
 *   #define BBT_SEQUENCER_TASK_ID_BIT  (1u << CFG_TASK_BBT_WORKER)
 *   #define BBT_SEQUENCER_GET_TIME_MS() HAL_GetTick()
 *
 * Optional:
 *   #define BBT_SEQUENCER_TASK_PRIORITY CFG_SCH_PRIO_0
 *   #define BBT_SEQUENCER_DELAY_MS(ms)  HAL_Delay(ms)
 *   #define BBT_SEQUENCER_ENTER_CRITICAL() ...
 *   #define BBT_SEQUENCER_EXIT_CRITICAL()  ...
 */
#ifndef BBT_SEQUENCER_TASK_ID_BIT
#error "BBT_SEQUENCER_TASK_ID_BIT must be defined in bbt_config.h for sequencer mode"
#endif

#ifndef BBT_SEQUENCER_GET_TIME_MS
#error "BBT_SEQUENCER_GET_TIME_MS() must be defined in bbt_config.h for sequencer mode"
#endif

#ifndef BBT_SEQUENCER_TASK_PRIORITY
#define BBT_SEQUENCER_TASK_PRIORITY 0u
#endif

#ifndef BBT_SEQUENCER_AUTOSCHEDULE
#define BBT_SEQUENCER_AUTOSCHEDULE 1
#endif

#ifndef BBT_SEQUENCER_ENTER_CRITICAL
#define BBT_SEQUENCER_ENTER_CRITICAL() \
  do                                   \
  {                                    \
  }                                    \
  while (0)
#endif

#ifndef BBT_SEQUENCER_EXIT_CRITICAL
#define BBT_SEQUENCER_EXIT_CRITICAL() \
  do                                  \
  {                                   \
  }                                   \
  while (0)
#endif

#ifndef BBT_SEQUENCER_DELAY_MS
#define BBT_SEQUENCER_DELAY_MS(delay_ms) bbt_sequencer_busy_delay(delay_ms)
#endif

#ifndef BBT_SEQUENCER_QUEUE_ITEM_SIZE
#define BBT_SEQUENCER_QUEUE_ITEM_SIZE (sizeof(bbt_rx_queue_item_t))
#endif

#ifndef BBT_SEQUENCER_QUEUE_DEPTH
#define BBT_SEQUENCER_QUEUE_DEPTH BBT_RX_QUEUE_DEPTH
#endif

typedef struct
{
  uint8_t *buffer;
  uint32_t item_size;
  uint32_t depth;
  uint32_t head;
  uint32_t tail;
  uint32_t count;
} bbt_sequencer_queue_t;

static bbt_sequencer_queue_t _queue;
static uint8_t               _queue_buffer[BBT_SEQUENCER_QUEUE_ITEM_SIZE * BBT_SEQUENCER_QUEUE_DEPTH];
static bool                  _queue_in_use = false;
static void (*_worker_entry)(void *)       = NULL;
static void                 *_worker_arg   = NULL;
static bool                  _task_created = false;

static void bbt_sequencer_task(void);
static void bbt_sequencer_schedule_worker(void);
static void bbt_sequencer_busy_delay(uint32_t delay_ms);

bbt_os_thread_t bbt_os_thread_create(void (*entry)(void *), void *arg, const char *name, uint32_t stack_size,
                                     int priority)
{
  (void)name;
  (void)stack_size;
  (void)priority;

  if (entry == NULL)
  {
    return NULL;
  }

  _worker_entry = entry;
  _worker_arg   = arg;
  _task_created = true;

  UTIL_SEQ_RegTask(BBT_SEQUENCER_TASK_ID_BIT, UTIL_SEQ_RFU, bbt_sequencer_task);

  return (bbt_os_thread_t)&_task_created;
}

void bbt_os_thread_delete(bbt_os_thread_t thread)
{
  if (thread == NULL)
  {
    return;
  }

  _worker_entry = NULL;
  _worker_arg   = NULL;
  _task_created = false;
}

bbt_os_queue_t bbt_os_queue_create(uint32_t item_size, uint32_t depth)
{
  if ((item_size == 0u) || (depth == 0u))
  {
    return NULL;
  }

  if ((item_size > BBT_SEQUENCER_QUEUE_ITEM_SIZE) || (depth > BBT_SEQUENCER_QUEUE_DEPTH))
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
  bbt_sequencer_queue_t *q = (bbt_sequencer_queue_t *)queue;

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

  bbt_sequencer_queue_t *q = (bbt_sequencer_queue_t *)queue;

  if ((q == NULL) || (item == NULL) || !_queue_in_use)
  {
    return false;
  }

  bool ok = false;

  BBT_SEQUENCER_ENTER_CRITICAL();

  if (q->count < q->depth)
  {
    uint8_t *dst = &q->buffer[q->head * q->item_size];
    memcpy(dst, item, q->item_size);

    q->head = (q->head + 1u) % q->depth;
    q->count++;
    ok = true;
  }

  BBT_SEQUENCER_EXIT_CRITICAL();

  if (ok)
  {
    bbt_sequencer_schedule_worker();
  }

  return ok;
}

bool bbt_os_queue_receive(bbt_os_queue_t queue, void *item, uint32_t timeout_ms)
{
  bbt_sequencer_queue_t *q = (bbt_sequencer_queue_t *)queue;

  if ((q == NULL) || (item == NULL) || !_queue_in_use)
  {
    return false;
  }

  uint32_t start = BBT_SEQUENCER_GET_TIME_MS();

  do
  {
    bool ok = false;

    BBT_SEQUENCER_ENTER_CRITICAL();

    if (q->count > 0u)
    {
      uint8_t *src = &q->buffer[q->tail * q->item_size];
      memcpy(item, src, q->item_size);

      q->tail = (q->tail + 1u) % q->depth;
      q->count--;
      ok = true;
    }

    BBT_SEQUENCER_EXIT_CRITICAL();

    if (ok)
    {
      if (q->count > 0u)
      {
        bbt_sequencer_schedule_worker();
      }
      return true;
    }

    if (timeout_ms == 0u)
    {
      return false;
    }

    BBT_SEQUENCER_DELAY_MS(1u);
  }
  while ((timeout_ms == BBT_OS_WAIT_FOREVER) || ((BBT_SEQUENCER_GET_TIME_MS() - start) < timeout_ms));

  return false;
}

bbt_os_mutex_t bbt_os_mutex_create(void)
{
  return (bbt_os_mutex_t)0x1;
}

void bbt_os_mutex_delete(bbt_os_mutex_t mutex)
{
  (void)mutex;
}

void bbt_os_mutex_lock(bbt_os_mutex_t mutex)
{
  (void)mutex;
  BBT_SEQUENCER_ENTER_CRITICAL();
}

void bbt_os_mutex_unlock(bbt_os_mutex_t mutex)
{
  (void)mutex;
  BBT_SEQUENCER_EXIT_CRITICAL();
}

uint32_t bbt_os_get_time_ms(void)
{
  return BBT_SEQUENCER_GET_TIME_MS();
}

void bbt_os_delay_ms(uint32_t delay_ms)
{
  BBT_SEQUENCER_DELAY_MS(delay_ms);
}

static void bbt_sequencer_task(void)
{
  if (_worker_entry != NULL)
  {
    _worker_entry(_worker_arg);
  }
}

static void bbt_sequencer_schedule_worker(void)
{
#if BBT_SEQUENCER_AUTOSCHEDULE
  if (_task_created)
  {
    UTIL_SEQ_SetTask(BBT_SEQUENCER_TASK_ID_BIT, BBT_SEQUENCER_TASK_PRIORITY);
  }
#endif
}

static void bbt_sequencer_busy_delay(uint32_t delay_ms)
{
  uint32_t start = BBT_SEQUENCER_GET_TIME_MS();

  while ((BBT_SEQUENCER_GET_TIME_MS() - start) < delay_ms)
  {
  }
}

#endif /* BBT_OS_SEQUENCER */
