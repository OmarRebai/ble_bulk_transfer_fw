/**
 * @file            bbt_port_cmsis.c
 * @brief           CMSIS-based port implementations for OS abstractions used by BBT.
 * Updated with Doxygen headers.
 * @date            25.05.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#include "bbt_port.h"

#if defined(BBT_OS_CMSIS)

#include "cmsis_os.h"
#include "string.h"

bbt_os_thread_t bbt_os_thread_create(void (*entry)(void *), void *arg, const char *name, uint32_t stack_size,
                                     int priority)
{
  osThreadAttr_t attr;
  memset(&attr, 0, sizeof(attr));

  attr.name       = name;
  attr.stack_size = stack_size;
  attr.priority   = (osPriority_t)priority;

  return (bbt_os_thread_t)osThreadNew(entry, arg, &attr);
}

void bbt_os_thread_delete(bbt_os_thread_t thread)
{
  if (thread != NULL)
  {
    (void)osThreadTerminate((osThreadId_t)thread);
  }
}

bbt_os_queue_t bbt_os_queue_create(uint32_t item_size, uint32_t depth)
{
  return (bbt_os_queue_t)osMessageQueueNew(depth, item_size, NULL);
}

void bbt_os_queue_delete(bbt_os_queue_t queue)
{
  if (queue != NULL)
  {
    (void)osMessageQueueDelete((osMessageQueueId_t)queue);
  }
}

bool bbt_os_queue_send(bbt_os_queue_t queue, const void *item, uint32_t timeout_ms)
{
  if ((queue == NULL) || (item == NULL))
  {
    return false;
  }

  osStatus_t status = osMessageQueuePut((osMessageQueueId_t)queue, item, 0u, timeout_ms);

  return status == osOK;
}

bool bbt_os_queue_receive(bbt_os_queue_t queue, void *item, uint32_t timeout_ms)
{
  if ((queue == NULL) || (item == NULL))
  {
    return false;
  }

  osStatus_t status = osMessageQueueGet((osMessageQueueId_t)queue, item, NULL, timeout_ms);

  return status == osOK;
}

bbt_os_mutex_t bbt_os_mutex_create(void)
{
  return (bbt_os_mutex_t)osMutexNew(NULL);
}

void bbt_os_mutex_delete(bbt_os_mutex_t mutex)
{
  if (mutex != NULL)
  {
    (void)osMutexDelete((osMutexId_t)mutex);
  }
}

void bbt_os_mutex_lock(bbt_os_mutex_t mutex)
{
  if (mutex != NULL)
  {
    (void)osMutexAcquire((osMutexId_t)mutex, osWaitForever);
  }
}

void bbt_os_mutex_unlock(bbt_os_mutex_t mutex)
{
  if (mutex != NULL)
  {
    (void)osMutexRelease((osMutexId_t)mutex);
  }
}

uint32_t bbt_os_get_time_ms(void)
{
  uint32_t ticks = osKernelGetTickCount();
  uint32_t freq  = osKernelGetTickFreq();

  if (freq == 1000u)
  {
    return ticks;
  }

  return (uint32_t)(((uint64_t)ticks * 1000u) / freq);
}

void bbt_os_delay_ms(uint32_t delay_ms)
{
  (void)osDelay(delay_ms);
}

#endif /* BBT_OS_CMSIS */
