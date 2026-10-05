/**
 * @file            bbt_port_esp_idf.c
 * @brief           ESP-IDF (FreeRTOS) port implementations for OS abstractions used by BBT.
 * Mirrors bbt_port_cmsis.c on the native FreeRTOS API.
 * @date            03.10.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#include "bbt_port.h"

#if defined(BBT_OS_ESP_IDF)

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* Optional, in bbt_config.h: core the worker task is pinned to (0, 1), or tskNO_AFFINITY. */
#ifndef BBT_ESP_IDF_TASK_CORE
#define BBT_ESP_IDF_TASK_CORE tskNO_AFFINITY
#endif

/* Longest log line of bbt_port_log() (the default ESP-IDF log hook), truncated beyond. */
#ifndef BBT_ESP_IDF_LOG_LINE_MAX
#define BBT_ESP_IDF_LOG_LINE_MAX 128u
#endif

static const char *TAG = "bbt";

typedef struct
{
  void (*entry)(void *);
  void       *arg;
  UBaseType_t priority;
} bbt_esp_idf_thread_start_t;

/**
 * @brief Convert a BBT timeout to FreeRTOS ticks (BBT_OS_WAIT_FOREVER is osWaitForever in the CMSIS port).
 */
static TickType_t _ms_to_ticks(uint32_t timeout_ms)
{
  return (timeout_ms == BBT_OS_WAIT_FOREVER) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
}

/**
 * @brief FreeRTOS task entry wrapping the BBT worker entry.
 *
 * The task is created at idle priority and raises itself to the requested one here.
 * On CMSIS the worker is created before osKernelStart() and only runs once
 * bbt_core_init() has returned. The ESP-IDF scheduler already runs when app_main()
 * calls it, so a worker of higher priority than the caller would run as soon as it is
 * created, before the core marks itself initialized, and spin without ever blocking.
 * At idle priority it only gets scheduled once the caller is done.
 *
 * A FreeRTOS task function must not return (osThreadNew() handles it on CMSIS). When
 * the BBT entry returns (bbt_core_deinit() cleared the running flag), the task
 * suspends itself until bbt_os_thread_delete(), which bbt_core_deinit() calls right after.
 */
static void _thread_trampoline(void *param)
{
  bbt_esp_idf_thread_start_t start = *(bbt_esp_idf_thread_start_t *)param;
  free(param);

  vTaskPrioritySet(NULL, start.priority);
  start.entry(start.arg);

  for (;;)
  {
    vTaskSuspend(NULL);
  }
}

bbt_os_thread_t bbt_os_thread_create(void (*entry)(void *), void *arg, const char *name, uint32_t stack_size,
                                     int priority)
{
  bbt_esp_idf_thread_start_t *start = malloc(sizeof(*start));
  if (start == NULL)
  {
    return NULL;
  }

  start->entry    = entry;
  start->arg      = arg;
  start->priority = (UBaseType_t)priority;

  TaskHandle_t handle = NULL;

  /* ESP-IDF FreeRTOS: the stack depth is in bytes, like attr.stack_size on CMSIS. */
  if (xTaskCreatePinnedToCore(_thread_trampoline, name, stack_size, start, tskIDLE_PRIORITY, &handle,
                              BBT_ESP_IDF_TASK_CORE) != pdPASS)
  {
    free(start);
    return NULL;
  }

  return (bbt_os_thread_t)handle;
}

void bbt_os_thread_delete(bbt_os_thread_t thread)
{
  if (thread != NULL)
  {
    vTaskDelete((TaskHandle_t)thread);
  }
}

bbt_os_queue_t bbt_os_queue_create(uint32_t item_size, uint32_t depth)
{
  return (bbt_os_queue_t)xQueueCreate(depth, item_size);
}

void bbt_os_queue_delete(bbt_os_queue_t queue)
{
  if (queue != NULL)
  {
    vQueueDelete((QueueHandle_t)queue);
  }
}

bool bbt_os_queue_send(bbt_os_queue_t queue, const void *item, uint32_t timeout_ms)
{
  if ((queue == NULL) || (item == NULL))
  {
    return false;
  }

  return xQueueSend((QueueHandle_t)queue, item, _ms_to_ticks(timeout_ms)) == pdTRUE;
}

bool bbt_os_queue_receive(bbt_os_queue_t queue, void *item, uint32_t timeout_ms)
{
  if ((queue == NULL) || (item == NULL))
  {
    return false;
  }

  return xQueueReceive((QueueHandle_t)queue, item, _ms_to_ticks(timeout_ms)) == pdTRUE;
}

bbt_os_mutex_t bbt_os_mutex_create(void)
{
  return (bbt_os_mutex_t)xSemaphoreCreateMutex();
}

void bbt_os_mutex_delete(bbt_os_mutex_t mutex)
{
  if (mutex != NULL)
  {
    vSemaphoreDelete((SemaphoreHandle_t)mutex);
  }
}

void bbt_os_mutex_lock(bbt_os_mutex_t mutex)
{
  if (mutex != NULL)
  {
    (void)xSemaphoreTake((SemaphoreHandle_t)mutex, portMAX_DELAY);
  }
}

void bbt_os_mutex_unlock(bbt_os_mutex_t mutex)
{
  if (mutex != NULL)
  {
    (void)xSemaphoreGive((SemaphoreHandle_t)mutex);
  }
}

uint32_t bbt_os_get_time_ms(void)
{
  /* Wraps after ~49 days: BBT only compares differences. */
  return (uint32_t)(esp_timer_get_time() / 1000);
}

void bbt_os_delay_ms(uint32_t delay_ms)
{
  vTaskDelay(_ms_to_ticks(delay_ms));
}

/**
 * @brief Default logging hook: ESP-IDF log, tag "bbt".
 *
 * Weak: the application can provide its own bbt_port_log().
 */
__attribute__((weak)) void bbt_port_log(int level, const char *fmt, ...)
{
  char    line[BBT_ESP_IDF_LOG_LINE_MAX];
  va_list args;

  va_start(args, fmt);
  (void)vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  switch (level)
  {
  case BBT_LOG_ERROR:
    ESP_LOGE(TAG, "%s", line);
    break;
  case BBT_LOG_WARN:
    ESP_LOGW(TAG, "%s", line);
    break;
  case BBT_LOG_INFO:
    ESP_LOGI(TAG, "%s", line);
    break;
  default:
    ESP_LOGD(TAG, "%s", line);
    break;
  }
}

#endif /* BBT_OS_ESP_IDF */
