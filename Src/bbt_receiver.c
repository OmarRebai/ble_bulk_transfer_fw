/**
 * @file            bbt_receiver.c
 * @brief           Receiver implementation: processes incoming packets, manages bitmap and NACKs.
 * Updated with NACK bitmap refactor and Doxygen headers.
 * @date            25.05.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#include "bbt_core.h"
#include "bbt_packet.h"

#include <string.h>

#define g_worker (bbt_core_get_context()->worker)
#define g_rx_queue (g_worker.queue)
#define g_rx_thread (g_worker.thread)
#define g_mutex (g_worker.mutex)
#define g_running (g_worker.running)
#define g_ctx (bbt_core_get_context()->receiver)
#define g_role (bbt_core_get_context()->role)
#define g_max_chunk_size (bbt_core_get_context()->max_chunk_size)


/* ============================================================================
 * Forward declarations
 * ==========================================================================*/

static void _handle_start(const uint8_t *data, uint16_t len);
static void _handle_chunk(const uint8_t *data, uint16_t len);
static void _handle_end(const uint8_t *data, uint16_t len);
static void _handle_abort(const uint8_t *data, uint16_t len);

static void         _reset_receiver_context(void);
static void         _set_error(bbt_proto_status_t error);
static bbt_result_t _send_start_ack(uint8_t transfer_id, bbt_proto_status_t status,
                                                uint16_t accepted_chunk_size);
static bbt_result_t _send_complete(void);
static bbt_result_t _send_error(bbt_proto_status_t error);
static bbt_result_t _send_nack_missing_ranges(void);

static bool _validate_start_meta(const bbt_start_meta_t *meta, bbt_proto_status_t *err);

static bool _is_chunk_received(uint16_t seq);
static void _mark_chunk_received(uint16_t seq);

static void _verify_transfer(void);

static uint16_t _expected_payload_len(uint16_t seq);

/* ============================================================================
 * Public receiver lifecycle
 * ==========================================================================*/

bbt_result_t bbt_receiver_init(void)
{
  memset(&g_ctx, 0, sizeof(g_ctx));
  g_ctx.state = BBT_STATE_IDLE;

  return BBT_OK;
}

bbt_result_t bbt_receiver_deinit(void)
{
  memset(&g_ctx, 0, sizeof(g_ctx));
  g_ctx.state = BBT_STATE_IDLE;

  return BBT_OK;
}

/* ============================================================================
 * Abort
 * ==========================================================================*/

bbt_result_t bbt_receiver_abort(void)
{
  uint8_t *notify_buf;
  uint8_t  transfer_id;

  if (g_rx_queue == NULL)
  {
    return BBT_ERR_INVALID_STATE;
  }

  bbt_os_mutex_lock(g_mutex);

  transfer_id = g_ctx.transfer_id;
  _reset_receiver_context();
  g_ctx.state = BBT_STATE_IDLE;

  bbt_os_mutex_unlock(g_mutex);

  notify_buf = bbt_core_get_notify_buffer();
  bbt_error_packet_t pkt;
  pkt.transfer_id = transfer_id;
  pkt.error       = (uint8_t)BBT_PROTO_ERR_ABORTED;
  uint16_t len = bbt_packet_build_error(notify_buf, BBT_MAX_NOTIFY_SIZE, &pkt);

  if (len > 0u)
  {
    (void)bbt_port_ble_indicate(notify_buf, len);
  }

  bbt_core_emit_event(BBT_EVENT_ABORTED, NULL);

  return BBT_OK;
}

/* ============================================================================
 * Status
 * ==========================================================================*/

bbt_result_t bbt_receiver_get_status(bbt_status_t *status)
{
  if (status == NULL)
  {
    return BBT_ERR_INVALID_ARG;
  }

  bbt_os_mutex_lock(g_mutex);

  status->state               = g_ctx.state;
  status->transfer_id         = g_ctx.transfer_id;
  status->total_size          = g_ctx.total_size;
  status->chunk_size          = g_ctx.chunk_size;
  status->total_chunks        = g_ctx.total_chunks;
  status->valid_chunks        = g_ctx.valid_chunks;
  status->bytes_written       = g_ctx.bytes_written;
  status->high_watermark_next = g_ctx.high_watermark_next;
  status->queue_drops         = g_ctx.queue_drops;
  status->crc_errors          = g_ctx.crc_errors;
  status->duplicate_chunks    = g_ctx.duplicate_chunks;
  status->storage_errors      = g_ctx.storage_errors;
  status->last_rx_time_ms     = g_ctx.last_rx_time_ms;

  bbt_os_mutex_unlock(g_mutex);

  return BBT_OK;
}

/* ============================================================================
 * Polling / task
 * ==========================================================================*/

void bbt_receiver_poll(void)
{
  bbt_core_worker_service(true);
}

/* ============================================================================
 * Packet dispatch
 * ==========================================================================*/

void bbt_receiver_process_item(const bbt_rx_queue_item_t *item)
{
  if ((item == NULL) || (item->len == 0u))
  {
    return;
  }

  switch (item->data[0])
  {
  case BBT_OPCODE_START:
    _handle_start(item->data, item->len);
    break;

  case BBT_OPCODE_CHUNK:
    _handle_chunk(item->data, item->len);
    break;

  case BBT_OPCODE_END:
    _handle_end(item->data, item->len);
    break;

  case BBT_OPCODE_ABORT:
    _handle_abort(item->data, item->len);
    break;

  case BBT_OPCODE_PULL_REQ:
  case BBT_OPCODE_NACK:
  case BBT_OPCODE_COMPLETE:
  case BBT_OPCODE_ERROR:
    bbt_port_log(BBT_LOG_WARN, "BBT: sender opcode 0x%02X on receiver role", item->data[0]);
    break;

  default:
    bbt_port_log(BBT_LOG_WARN, "BBT: unknown opcode 0x%02X", item->data[0]);
    break;
  }
}

/* ============================================================================
 * START handling
 * ==========================================================================*/

static void _handle_start(const uint8_t *data, uint16_t len)
{
  if ((bbt_core_get_context()->role == BBT_ROLE_SENDER) || bbt_core_get_context()->sender.active)
  {
    return;
  }

  bbt_start_meta_t   meta;
  bbt_proto_status_t err = BBT_PROTO_STATUS_OK;

  if (!bbt_packet_parse_start(data, len, &meta))
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: invalid START packet");
    return;
  }

  if (!_validate_start_meta(&meta, &err))
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: START rejected, err=%u", (unsigned)err);
    (void)_send_start_ack(meta.transfer_id, err, BBT_MAX_CHUNK_SIZE);
    return;
  }

  bbt_port_log(BBT_LOG_INFO, "BBT: START id=%u size=%lu chunk=%u chunks=%u", meta.transfer_id,
               (unsigned long)meta.total_size, meta.chunk_size, meta.total_chunks);

  bbt_os_mutex_lock(g_mutex);

  _reset_receiver_context();

  g_ctx.state               = BBT_STATE_RECEIVING;
  g_ctx.transfer_id         = meta.transfer_id;
  g_ctx.total_size          = meta.total_size;
  g_ctx.chunk_size          = meta.chunk_size;
  g_ctx.total_chunks        = meta.total_chunks;
  g_ctx.last_rx_time_ms     = bbt_os_get_time_ms();
  g_ctx.last_notify_time_ms = g_ctx.last_rx_time_ms;

  bbt_os_mutex_unlock(g_mutex);

  /* Require app callback ownership for storage handling. */
  {
    uint8_t                           idx = (uint8_t)meta.transfer_id & 0x7Fu;
    bbt_mode_entry_t                  entry;
    const bbt_app_to_device_vtable_t *vt = NULL;

    if (bbt_mode_get_entry(idx, &entry) == BBT_OK)
    {
      vt = entry.app_to_device;
    }

    if ((vt == NULL) || (vt->on_start == NULL))
    {
      bbt_port_log(BBT_LOG_ERROR, "BBT: missing app on_start callback for id=%u", meta.transfer_id);

      bbt_os_mutex_lock(g_mutex);
      g_ctx.state = BBT_STATE_ERROR;
      g_ctx.storage_errors++;
      bbt_os_mutex_unlock(g_mutex);

      (void)_send_start_ack(meta.transfer_id, BBT_PROTO_ERR_STORAGE, g_max_chunk_size);
      bbt_core_emit_event(BBT_EVENT_ERROR, NULL);
      return;
    }

    bbt_result_t r = vt->on_start(&meta, entry.ctx);
    if (r != BBT_OK)
    {
      bbt_port_log(BBT_LOG_ERROR, "BBT: app on_start failed id=%u err=%d", meta.transfer_id, (int)r);
      bbt_os_mutex_lock(g_mutex);
      g_ctx.state = BBT_STATE_ERROR;
      g_ctx.storage_errors++;
      bbt_os_mutex_unlock(g_mutex);

      (void)_send_start_ack(meta.transfer_id, BBT_PROTO_ERR_STORAGE, g_max_chunk_size);
      bbt_core_emit_event(BBT_EVENT_ERROR, NULL);
      return;
    }
  }

  (void)_send_start_ack(meta.transfer_id, BBT_PROTO_STATUS_OK, meta.chunk_size);

  bbt_core_emit_event(BBT_EVENT_STARTED, &meta);
}

static bool _validate_start_meta(const bbt_start_meta_t *meta, bbt_proto_status_t *err)
{
  if ((meta == NULL) || (err == NULL))
  {
    return false;
  }

  if (meta->chunk_size == 0u)
  {
    *err = BBT_PROTO_ERR_SIZE_MISMATCH;
    return false;
  }

  if (meta->chunk_size > BBT_MAX_CHUNK_SIZE)
  {
    *err = BBT_PROTO_ERR_CHUNK_TOO_LARGE;
    return false;
  }

  if (meta->chunk_size > g_max_chunk_size)
  {
    *err = BBT_PROTO_ERR_CHUNK_TOO_LARGE;
    return false;
  }

  if ((meta->total_chunks == 0u) || (meta->total_chunks > BBT_MAX_TOTAL_CHUNKS))
  {
    *err = BBT_PROTO_ERR_TOO_MANY_CHUNKS;
    return false;
  }

  if (meta->total_size == 0u)
  {
    *err = BBT_PROTO_ERR_SIZE_MISMATCH;
    return false;
  }

  uint32_t expected_chunks = (meta->total_size + meta->chunk_size - 1u) / meta->chunk_size;

  if (expected_chunks != meta->total_chunks)
  {
    *err = BBT_PROTO_ERR_SIZE_MISMATCH;
    return false;
  }

  *err = BBT_PROTO_STATUS_OK;
  return true;
}

/* ============================================================================
 * CHUNK handling
 * ==========================================================================*/

static void _handle_chunk(const uint8_t *data, uint16_t len)
{
  if ((bbt_core_get_context()->role == BBT_ROLE_SENDER) || bbt_core_get_context()->sender.active)
  {
    return;
  }

  bbt_chunk_packet_t chunk;

  if (!bbt_packet_parse_chunk(data, len, &chunk))
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: invalid CHUNK packet");
    return;
  }

  bbt_os_mutex_lock(g_mutex);

  if ((g_ctx.state != BBT_STATE_RECEIVING) && (g_ctx.state != BBT_STATE_WAITING_REPAIR))
  {
    bbt_os_mutex_unlock(g_mutex);
    return;
  }

  if (chunk.header.transfer_id != g_ctx.transfer_id)
  {
    bbt_os_mutex_unlock(g_mutex);
    return;
  }

  if (chunk.header.seq >= g_ctx.total_chunks)
  {
    bbt_os_mutex_unlock(g_mutex);
    return;
  }

  uint16_t expected_len = _expected_payload_len(chunk.header.seq);

  bbt_os_mutex_unlock(g_mutex);

  if (chunk.header.payload_len != expected_len)
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: invalid chunk length seq=%u len=%u expected=%u", chunk.header.seq,
                 chunk.header.payload_len, expected_len);
    return;
  }

  uint16_t computed_crc = bbt_crc16_ccitt(chunk.payload, chunk.header.payload_len);

  if (computed_crc != chunk.header.payload_crc16)
  {
    bbt_os_mutex_lock(g_mutex);
    g_ctx.crc_errors++;
    g_ctx.last_rx_time_ms = bbt_os_get_time_ms();
    bbt_os_mutex_unlock(g_mutex);

    bbt_port_log(BBT_LOG_WARN, "BBT: CRC error seq=%u", chunk.header.seq);
    return;
  }

  bbt_os_mutex_lock(g_mutex);

  if (_is_chunk_received(chunk.header.seq))
  {
    g_ctx.duplicate_chunks++;
    g_ctx.last_rx_time_ms  = bbt_os_get_time_ms();
    g_ctx.nack_retry_count = 0u;
    bbt_os_mutex_unlock(g_mutex);
    return;
  }

  bbt_os_mutex_unlock(g_mutex);

  /* Require app callback ownership for chunk handling. */
  {
    uint8_t                           idx = (uint8_t)chunk.header.transfer_id & 0x7Fu;
    bbt_mode_entry_t                  entry;
    const bbt_app_to_device_vtable_t *vt = NULL;

    if (bbt_mode_get_entry(idx, &entry) == BBT_OK)
    {
      vt = entry.app_to_device;
    }

    if ((vt == NULL) || (vt->on_chunk == NULL))
    {
      bbt_port_log(BBT_LOG_ERROR, "BBT: missing app on_chunk callback for seq=%u", chunk.header.seq);
      bbt_os_mutex_lock(g_mutex);
      g_ctx.storage_errors++;
      g_ctx.state = BBT_STATE_ERROR;
      bbt_os_mutex_unlock(g_mutex);

      (void)_send_error(BBT_PROTO_ERR_STORAGE);
      bbt_core_emit_event(BBT_EVENT_ERROR, NULL);
      return;
    }

    bbt_result_t r = vt->on_chunk(&chunk, entry.ctx);
    if (r != BBT_OK)
    {
      bbt_os_mutex_lock(g_mutex);
      g_ctx.storage_errors++;
      g_ctx.state = BBT_STATE_ERROR;
      bbt_os_mutex_unlock(g_mutex);

      bbt_port_log(BBT_LOG_ERROR, "BBT: app on_chunk failed seq=%u err=%d", chunk.header.seq, (int)r);
      (void)_send_error(BBT_PROTO_ERR_STORAGE);
      bbt_core_emit_event(BBT_EVENT_ERROR, NULL);
      return;
    }
  }

  bbt_os_mutex_lock(g_mutex);

  _mark_chunk_received(chunk.header.seq);

  g_ctx.valid_chunks++;
  g_ctx.bytes_written += chunk.header.payload_len;

  if (chunk.header.seq >= g_ctx.high_watermark_next)
  {
    g_ctx.high_watermark_next = (uint16_t)(chunk.header.seq + 1u);
  }

  g_ctx.last_rx_time_ms  = bbt_os_get_time_ms();
  g_ctx.nack_retry_count = 0u;

  bbt_status_t status;
  status.state               = g_ctx.state;
  status.transfer_id         = g_ctx.transfer_id;
  status.total_size          = g_ctx.total_size;
  status.chunk_size          = g_ctx.chunk_size;
  status.total_chunks        = g_ctx.total_chunks;
  status.valid_chunks        = g_ctx.valid_chunks;
  status.bytes_written       = g_ctx.bytes_written;
  status.high_watermark_next = g_ctx.high_watermark_next;
  status.queue_drops         = g_ctx.queue_drops;
  status.crc_errors          = g_ctx.crc_errors;
  status.duplicate_chunks    = g_ctx.duplicate_chunks;
  status.storage_errors      = g_ctx.storage_errors;
  status.last_rx_time_ms     = g_ctx.last_rx_time_ms;

  bbt_os_mutex_unlock(g_mutex);

  bbt_core_emit_event(BBT_EVENT_PROGRESS, &status);
}

/* ============================================================================
 * END handling
 * ==========================================================================*/

static void _handle_end(const uint8_t *data, uint16_t len)
{
  if ((bbt_core_get_context()->role == BBT_ROLE_SENDER) || bbt_core_get_context()->sender.active)
  {
    return;
  }

  bbt_packet_id_t pkt;

  if (!bbt_packet_parse_end(data, len, &pkt))
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: invalid END packet");
    return;
  }

  bbt_os_mutex_lock(g_mutex);

  if ((g_ctx.state != BBT_STATE_RECEIVING) && (g_ctx.state != BBT_STATE_WAITING_REPAIR))
  {
    bbt_os_mutex_unlock(g_mutex);
    return;
  }

  if (pkt.transfer_id != g_ctx.transfer_id)
  {
    bbt_os_mutex_unlock(g_mutex);
    return;
  }

  g_ctx.state           = BBT_STATE_VERIFYING;
  g_ctx.last_rx_time_ms = bbt_os_get_time_ms();

  bbt_os_mutex_unlock(g_mutex);

  _verify_transfer();
}

/* ============================================================================
 * ABORT handling
 * ==========================================================================*/

static void _handle_abort(const uint8_t *data, uint16_t len)
{
  if ((bbt_core_get_context()->role == BBT_ROLE_SENDER) || bbt_core_get_context()->sender.active)
  {
    return;
  }

  bbt_packet_id_t pkt;

  if (!bbt_packet_parse_abort(data, len, &pkt))
  {
    return;
  }

  bbt_os_mutex_lock(g_mutex);

  if (pkt.transfer_id != g_ctx.transfer_id)
  {
    bbt_os_mutex_unlock(g_mutex);
    return;
  }

  _reset_receiver_context();
  g_ctx.state = BBT_STATE_IDLE;

  bbt_os_mutex_unlock(g_mutex);

  /* App callback for abort */
  {
    uint8_t                           idx = (uint8_t)pkt.transfer_id & 0x7Fu;
    bbt_mode_entry_t                  entry;
    const bbt_app_to_device_vtable_t *vt = NULL;
    if (bbt_mode_get_entry(idx, &entry) == BBT_OK)
    {
      vt = entry.app_to_device;
    }
    if ((vt != NULL) && (vt->on_abort != NULL))
    {
      (void)vt->on_abort(pkt.transfer_id, entry.ctx);
    }
  }

  (void)_send_error(BBT_PROTO_ERR_ABORTED);
  bbt_core_emit_event(BBT_EVENT_ABORTED, NULL);
}

/* ============================================================================
 * Verification
 * ==========================================================================*/

static void _verify_transfer(void)
{
  bbt_os_mutex_lock(g_mutex);

  bool all_chunks_received = (g_ctx.valid_chunks == g_ctx.total_chunks);

  bbt_os_mutex_unlock(g_mutex);

  if (!all_chunks_received)
  {
    bbt_os_mutex_lock(g_mutex);
    g_ctx.state               = BBT_STATE_WAITING_REPAIR;
    g_ctx.last_notify_time_ms = bbt_os_get_time_ms();
    bbt_os_mutex_unlock(g_mutex);

    (void)_send_nack_missing_ranges();
    bbt_core_emit_event(BBT_EVENT_NACK_SENT, NULL);
    return;
  }

  bbt_os_mutex_lock(g_mutex);
  g_ctx.state = BBT_STATE_COMPLETE;
  uint8_t transfer_id = g_ctx.transfer_id;
  bbt_os_mutex_unlock(g_mutex);

  (void)_send_complete();

  {
    uint8_t                           idx = (uint8_t)transfer_id & 0x7Fu;
    bbt_mode_entry_t                  entry;
    const bbt_app_to_device_vtable_t *vt = NULL;

    if (bbt_mode_get_entry(idx, &entry) == BBT_OK)
    {
      vt = entry.app_to_device;
    }

    if ((vt != NULL) && (vt->on_complete != NULL))
    {
      (void)vt->on_complete(transfer_id, entry.ctx);
    }
  }

  bbt_core_emit_event(BBT_EVENT_COMPLETE, NULL);
}

/* ============================================================================
 * Timeout handling
 * ==========================================================================*/

void bbt_receiver_check_timeouts(void)
{
  uint32_t now = bbt_os_get_time_ms();

  bbt_os_mutex_lock(g_mutex);

  bool upload_active = (g_ctx.state == BBT_STATE_RECEIVING) || (g_ctx.state == BBT_STATE_WAITING_REPAIR);

  if (!upload_active)
  {
    bbt_os_mutex_unlock(g_mutex);
    return;
  }

  uint32_t elapsed = now - g_ctx.last_rx_time_ms;

  if (elapsed < BBT_IDLE_TIMEOUT_MS)
  {
    bbt_os_mutex_unlock(g_mutex);
    return;
  }

  if (g_ctx.nack_retry_count >= BBT_NACK_RETRY_MAX)
  {
    g_ctx.state = BBT_STATE_ERROR;
    bbt_os_mutex_unlock(g_mutex);

    _set_error(BBT_PROTO_ERR_TIMEOUT);
    return;
  }

  g_ctx.nack_retry_count++;
  g_ctx.last_notify_time_ms = now;

  bool all_chunks_received = (g_ctx.valid_chunks == g_ctx.total_chunks);

  bbt_os_mutex_unlock(g_mutex);

  if (all_chunks_received)
  {
    _verify_transfer();
  }
  else
  {
    (void)_send_nack_missing_ranges();
    bbt_core_emit_event(BBT_EVENT_NACK_SENT, NULL);
  }
}

/* ============================================================================
 * Notifications
 * ==========================================================================*/

static bbt_result_t _send_start_ack(uint8_t transfer_id, bbt_proto_status_t status,
                                                uint16_t accepted_chunk_size)
{
  uint8_t *notify_buf = bbt_core_get_notify_buffer();
  bbt_start_ack_t ack;
  ack.transfer_id         = transfer_id;
  ack.status              = (uint8_t)status;
  ack.accepted_chunk_size = accepted_chunk_size;
  uint16_t len = bbt_packet_build_start_ack(notify_buf, BBT_MAX_NOTIFY_SIZE, &ack);

  if (len == 0u)
  {
    return BBT_ERR_INTERNAL;
  }

  if (bbt_port_ble_indicate(notify_buf, len) != 0)
  {
    return BBT_ERR_BLE_NOTIFY;
  }

  return BBT_OK;
}

static bbt_result_t _send_complete(void)
{
  uint8_t *notify_buf;
  uint8_t  transfer_id;

  bbt_os_mutex_lock(g_mutex);
  transfer_id = g_ctx.transfer_id;
  bbt_os_mutex_unlock(g_mutex);

  notify_buf = bbt_core_get_notify_buffer();
  bbt_packet_id_t pkt;
  pkt.transfer_id = transfer_id;
  uint16_t len = bbt_packet_build_complete(notify_buf, BBT_MAX_NOTIFY_SIZE, &pkt);

  if (len == 0u)
  {
    return BBT_ERR_INTERNAL;
  }

  if (bbt_port_ble_indicate(notify_buf, len) != 0)
  {
    return BBT_ERR_BLE_NOTIFY;
  }

  return BBT_OK;
}

static bbt_result_t _send_error(bbt_proto_status_t error)
{
  uint8_t *notify_buf;
  uint8_t  transfer_id;

  bbt_os_mutex_lock(g_mutex);
  transfer_id = g_ctx.transfer_id;
  bbt_os_mutex_unlock(g_mutex);

  notify_buf = bbt_core_get_notify_buffer();
  bbt_error_packet_t pkt;
  pkt.transfer_id = transfer_id;
  pkt.error       = (uint8_t)error;
  uint16_t len = bbt_packet_build_error(notify_buf, BBT_MAX_NOTIFY_SIZE, &pkt);

  if (len == 0u)
  {
    return BBT_ERR_INTERNAL;
  }

  if (bbt_port_ble_indicate(notify_buf, len) != 0)
  {
    return BBT_ERR_BLE_NOTIFY;
  }

  return BBT_OK;
}

static bbt_result_t _send_nack_missing_ranges(void)
{
  uint8_t *notify_buf;
  uint8_t  transfer_id;
  const uint16_t nack_header_len = (uint16_t)(1u + sizeof(bbt_nack_header_t));
  const uint16_t payload_space   = (BBT_MAX_NOTIFY_SIZE > nack_header_len) ? (uint16_t)(BBT_MAX_NOTIFY_SIZE - nack_header_len) : 0u;

  if (payload_space == 0u)
  {
    return BBT_ERR_INTERNAL;
  }

  bbt_os_mutex_lock(g_mutex);
  transfer_id = g_ctx.transfer_id;
  bool all_received = (g_ctx.valid_chunks == g_ctx.total_chunks);
  bbt_os_mutex_unlock(g_mutex);

  if (all_received)
  {
    return BBT_OK;
  }

  /* Send the full fixed-length bitmap in sequential segments. */
  const uint16_t total_bytes = BBT_BITMAP_SIZE_BYTES;
  uint16_t offset = 0u;

  while (offset < total_bytes)
  {
    uint16_t chunk_len = (uint16_t)((total_bytes - offset) > payload_space ? payload_space : (total_bytes - offset));

    bbt_nack_header_t hdr;
    hdr.transfer_id = transfer_id;
    hdr.flags       = ((offset + chunk_len) < total_bytes) ? 0x01u : 0x00u; /* more */
    hdr.bitmap_len  = chunk_len;

    notify_buf = bbt_core_get_notify_buffer();

    /* Build packet while holding mutex so the bitmap segment is stable. */
    bbt_os_mutex_lock(g_mutex);
    uint16_t len = bbt_packet_build_nack(notify_buf, BBT_MAX_NOTIFY_SIZE, &hdr, &g_ctx.bitmap[offset]);
    bbt_os_mutex_unlock(g_mutex);

    if (len == 0u)
    {
      return BBT_ERR_INTERNAL;
    }

    if (bbt_port_ble_indicate(notify_buf, len) != 0)
    {
      return BBT_ERR_BLE_NOTIFY;
    }

    offset += chunk_len;
  }

  return BBT_OK;
}

/* ============================================================================
 * Error handling
 * ==========================================================================*/

static void _set_error(bbt_proto_status_t error)
{
  uint8_t transfer_id = 0u;

  if (g_mutex != NULL)
  {
    bbt_os_mutex_lock(g_mutex);
    transfer_id = g_ctx.transfer_id;
    g_ctx.state = BBT_STATE_ERROR;
    bbt_os_mutex_unlock(g_mutex);
  }

  /* App callback for error */
  {
    uint8_t                           idx = (uint8_t)transfer_id & 0x7Fu;
    bbt_mode_entry_t                  entry;
    const bbt_app_to_device_vtable_t *vt = NULL;
    if (bbt_mode_get_entry(idx, &entry) == BBT_OK)
    {
      vt = entry.app_to_device;
    }
    if ((vt != NULL) && (vt->on_error != NULL))
    {
      (void)vt->on_error(transfer_id, error, entry.ctx);
    }
  }

  (void)_send_error(error);
  bbt_core_emit_event(BBT_EVENT_ERROR, NULL);
}

/* ============================================================================
 * Bitmap helpers
 * ==========================================================================*/

static bool _is_chunk_received(uint16_t seq)
{
  uint16_t byte_index = (uint16_t)(seq / 8u);
  uint8_t  bit_index  = (uint8_t)(seq % 8u);

  return (g_ctx.bitmap[byte_index] & (uint8_t)(1u << bit_index)) != 0u;
}

static void _mark_chunk_received(uint16_t seq)
{
  uint16_t byte_index = (uint16_t)(seq / 8u);
  uint8_t  bit_index  = (uint8_t)(seq % 8u);

  g_ctx.bitmap[byte_index] |= (uint8_t)(1u << bit_index);
}


bool bbt_receiver_is_upload_idle(void)
{
  bool idle = true;

  if (g_mutex != NULL)
  {
    bbt_os_mutex_lock(g_mutex);
  }

  idle = (g_ctx.state == BBT_STATE_IDLE);

  if (g_mutex != NULL)
  {
    bbt_os_mutex_unlock(g_mutex);
  }

  return idle;
}


/* ============================================================================
 * Helpers
 * ==========================================================================*/

static uint16_t _expected_payload_len(uint16_t seq)
{
  /*
   * Caller must already hold g_mutex. This helper only reads the current
   * transfer snapshot and must not try to lock recursively.
   */
  if (seq == (uint16_t)(g_ctx.total_chunks - 1u))
  {
    uint32_t offset = ((uint32_t)seq) * ((uint32_t)g_ctx.chunk_size);
    return (uint16_t)(g_ctx.total_size - offset);
  }

  return g_ctx.chunk_size;
}

static void _reset_receiver_context(void)
{
  memset(&g_ctx, 0, sizeof(g_ctx));
  g_ctx.state = BBT_STATE_IDLE;
}

void bbt_receiver_set_max_chunk_size(uint16_t max_chunk_size)
{
  if (max_chunk_size == 0u)
  {
    g_max_chunk_size = 0u;
    return;
  }

  if (max_chunk_size > BBT_MAX_CHUNK_SIZE)
  {
    g_max_chunk_size = BBT_MAX_CHUNK_SIZE;
  }
  else
  {
    g_max_chunk_size = max_chunk_size;
  }
}
