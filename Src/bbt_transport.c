/**
 * @file            bbt_transport.c
 * @brief           BLE transport retry wrapper for BBT.
 */

#include "bbt_port.h"

#ifndef BBT_BLE_SEND_RETRY_MAX
#define BBT_BLE_SEND_RETRY_MAX 10u
#endif

#ifndef BBT_BLE_SEND_RETRY_DELAY_MS
#ifdef BBT_BLE_NOTIFY_GAP_MS
#define BBT_BLE_SEND_RETRY_DELAY_MS BBT_BLE_NOTIFY_GAP_MS
#else
#define BBT_BLE_SEND_RETRY_DELAY_MS 20u
#endif
#endif

static int bbt_ble_send_with_retry(const uint8_t *data,
                                   uint16_t len,
                                   bool is_reliable)
{
  if ((data == NULL) || (len == 0u))
  {
    return -1;
  }

  uint32_t retry_count = BBT_BLE_SEND_RETRY_MAX;

  while (retry_count > 0u)
  {
    if (app_bbt_send(data, len, is_reliable) == 0)
    {
      return 0;
    }

    retry_count--;
    if (retry_count > 0u)
    {
      bbt_os_delay_ms(BBT_BLE_SEND_RETRY_DELAY_MS);
    }
  }

  return -1;
}

int bbt_port_ble_notify(const uint8_t *data, uint16_t len)
{
  return bbt_ble_send_with_retry(data, len, false);
}

int bbt_port_ble_indicate(const uint8_t *data, uint16_t len)
{
  return bbt_ble_send_with_retry(data, len, true);
}
