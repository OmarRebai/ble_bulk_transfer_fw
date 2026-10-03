/**
 * @file            bbt_sender.c
 * @brief           Sender (export) implementation: reads data and responds to NACKs.
 * Updated with NACK bitmap refactor and Doxygen headers.
 * @date            25.05.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#include "bbt_core.h"
#include "bbt_packet.h"

#include <string.h>

#define g_sender         (bbt_core_get_context()->sender)
#define g_max_chunk_size (bbt_core_get_context()->max_chunk_size)

static void         _reset_context(void);
static bbt_result_t _send_start_packet(void);
static bbt_result_t _send_error(uint8_t transfer_id, bbt_proto_status_t error);
static bbt_result_t _send_export_chunk(uint16_t seq);
static bbt_result_t _send_export_end(void);
static void         _set_error(bbt_proto_status_t error);

bbt_result_t bbt_sender_init(void)
{
  _reset_context();
  g_max_chunk_size = BBT_MAX_CHUNK_SIZE;
  return BBT_OK;
}

bbt_result_t bbt_sender_deinit(void)
{
  _reset_context();
  return BBT_OK;
}

bbt_result_t bbt_sender_abort(void)
{
  if (!g_sender.active)
  {
    return BBT_ERR_INVALID_STATE;
  }

  uint8_t                           transfer_id = g_sender.transfer_id;
  const bbt_device_to_app_vtable_t *vt          = g_sender.vt;
  void                             *ctx         = g_sender.vt_ctx;

  _reset_context();

  (void)_send_error(transfer_id, BBT_PROTO_ERR_ABORTED);

  if ((vt != NULL) && (vt->on_error != NULL))
  {
    (void)vt->on_error(transfer_id, BBT_PROTO_ERR_ABORTED, ctx);
  }

  bbt_core_emit_event(BBT_EVENT_ABORTED, NULL);
  return BBT_OK;
}

bbt_result_t bbt_sender_get_status(bbt_status_t *status)
{
  if (status == NULL)
  {
    return BBT_ERR_INVALID_ARG;
  }

  if ((!g_sender.active) && (g_sender.state == BBT_STATE_IDLE))
  {
    return BBT_ERR_INVALID_STATE;
  }

  status->state         = g_sender.state;
  status->transfer_id   = g_sender.transfer_id;
  status->total_size    = g_sender.desc.total_size;
  status->chunk_size    = g_sender.chunk_size;
  status->total_chunks  = g_sender.total_chunks;
  status->valid_chunks  = g_sender.next_seq;
  status->bytes_written = ((uint32_t)g_sender.next_seq) * ((uint32_t)g_sender.chunk_size);
  if (status->bytes_written > status->total_size)
  {
    status->bytes_written = status->total_size;
  }
  status->high_watermark_next = g_sender.next_seq;
  status->queue_drops         = 0u;
  status->crc_errors          = 0u;
  status->duplicate_chunks    = 0u;
  status->storage_errors      = 0u;
  status->last_rx_time_ms     = g_sender.last_rx_time_ms;

  return BBT_OK;
}

bool bbt_sender_is_active(void)
{
  return g_sender.active;
}

void bbt_sender_set_max_chunk_size(uint16_t max_chunk_size)
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

void bbt_sender_handle_pull_req(const uint8_t *data, uint16_t len)
{
  bbt_pull_req_t req;

  if (!bbt_packet_parse_pull_req(data, len, &req))
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: invalid PULL_REQ packet");
    return;
  }

  // Ensure direction bit is set for Device-to-App transfer
  uint8_t transfer_id = req.transfer_id | BBT_TRANSFER_ID_DIR_BIT;

  if ((req.transfer_id & BBT_TRANSFER_ID_DIR_BIT) == 0u)
  {
    (void)_send_error(transfer_id, BBT_PROTO_ERR_INVALID_STATE);
    return;
  }

  if (!bbt_core_can_start_sender())
  {
    (void)_send_error(transfer_id, BBT_PROTO_ERR_INVALID_STATE);
    return;
  }

  if (g_sender.active)
  {
    (void)_send_error(transfer_id, BBT_PROTO_ERR_INVALID_STATE);
    return;
  }

  uint8_t          idx = (uint8_t)(req.transfer_id & BBT_TRANSFER_ID_INDEX_MASK);
  bbt_mode_entry_t entry;

  if (bbt_mode_get_entry(idx, &entry) != BBT_OK)
  {
    (void)_send_error(transfer_id, BBT_PROTO_ERR_INVALID_STATE);
    return;
  }

  const bbt_device_to_app_vtable_t *vt = entry.device_to_app;

  if ((vt == NULL) || (vt->prepare_export == NULL))
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: missing export handler for id=%u", transfer_id);
    (void)_send_error(transfer_id, BBT_PROTO_ERR_INVALID_STATE);
    return;
  }

  bbt_export_desc_t desc;
  memset(&desc, 0, sizeof(desc));

  bbt_result_t result = vt->prepare_export(&req, &desc, entry.ctx);
  if (result != BBT_OK)
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: export prepare failed id=%u err=%d", transfer_id, (int)result);
    (void)_send_error(transfer_id, BBT_PROTO_ERR_STORAGE);

    if (vt->on_error != NULL)
    {
      (void)vt->on_error(transfer_id, BBT_PROTO_ERR_STORAGE, entry.ctx);
    }
    return;
  }

  if (desc.read_fn == NULL)
  {
    (void)_send_error(transfer_id, BBT_PROTO_ERR_STORAGE);
    return;
  }

  g_sender.desc = desc;
  if (g_sender.desc.chunk_size_hint == 0u)
  {
    g_sender.desc.chunk_size_hint = g_max_chunk_size;
  }

  uint16_t chunk_size = g_sender.desc.chunk_size_hint;
  if (req.max_chunk_size != 0u)
  {
    chunk_size = (chunk_size < req.max_chunk_size) ? chunk_size : req.max_chunk_size;
  }
  if (chunk_size > BBT_MAX_CHUNK_SIZE)
  {
    chunk_size = BBT_MAX_CHUNK_SIZE;
  }

  g_sender.vt           = vt;
  g_sender.vt_ctx       = entry.ctx;
  g_sender.transfer_id  = transfer_id;
  g_sender.chunk_size   = chunk_size;
  g_sender.total_chunks = (uint16_t)((desc.total_size + chunk_size - 1u) / chunk_size);
  g_sender.next_seq     = 0u;
  g_sender.active       = true;
  g_sender.state        = BBT_STATE_WAITING_START_ACK;

  g_sender.last_rx_time_ms     = bbt_os_get_time_ms();
  g_sender.last_notify_time_ms = g_sender.last_rx_time_ms;
  g_sender.nack_retry_count    = 0u;

  _send_start_packet();
}

void bbt_sender_handle_start_ack(const uint8_t *data, uint16_t len)
{
  bbt_start_ack_t ack;

  if (!bbt_packet_parse_start_ack(data, len, &ack))
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: invalid START_ACK packet");
    return;
  }

  bool active =
      g_sender.active && (ack.transfer_id == g_sender.transfer_id) && (g_sender.state == BBT_STATE_WAITING_START_ACK);
  if (!active)
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: unexpected START_ACK for id=%u", ack.transfer_id);
    return;
  }

  bbt_proto_status_t status = (bbt_proto_status_t)ack.status;
  if (status != BBT_PROTO_STATUS_OK)
  {
    bbt_port_log(BBT_LOG_ERROR, "BBT: START_ACK rejected with status=%u", (unsigned)status);
    _set_error(status);
    return;
  }

  if ((ack.accepted_chunk_size == 0u) || (ack.accepted_chunk_size > g_sender.chunk_size))
  {
    bbt_port_log(BBT_LOG_ERROR, "BBT: invalid accepted chunk size %u", ack.accepted_chunk_size);
    _set_error(BBT_PROTO_ERR_CHUNK_TOO_LARGE);
    return;
  }

  g_sender.chunk_size = ack.accepted_chunk_size;
  g_sender.total_chunks =
      (uint16_t)((g_sender.desc.total_size + ack.accepted_chunk_size - 1u) / ack.accepted_chunk_size);

  g_sender.state               = BBT_STATE_EXPORTING;
  g_sender.last_rx_time_ms     = bbt_os_get_time_ms();
  g_sender.last_notify_time_ms = g_sender.last_rx_time_ms;
  g_sender.nack_retry_count    = 0u;

  bbt_port_log(BBT_LOG_INFO, "BBT: START_ACK accepted, starting stream, chunk_size=%u, total_chunks=%u",
               g_sender.chunk_size, g_sender.total_chunks);
}

void bbt_sender_poll(void)
{
  if (!g_sender.active || (g_sender.state != BBT_STATE_EXPORTING))
  {
    return;
  }

  const uint16_t chunks_per_cycle = 3u;

  while (g_sender.next_seq < g_sender.total_chunks)
  {
    bbt_result_t result = _send_export_chunk(g_sender.next_seq);
    if (result != BBT_OK)
    {
      bbt_port_log(BBT_LOG_ERROR, "BBT: failed to send chunk %u, err=%d", g_sender.next_seq, (int)result);
      _set_error(BBT_PROTO_ERR_STORAGE);
      return;
    }

    g_sender.next_seq++;
    bbt_os_delay_ms(BBT_SENDER_CHUNK_DELAY_MS);
  }

  if (g_sender.next_seq >= g_sender.total_chunks)
  {
    g_sender.state               = BBT_STATE_WAITING_EXPORT_REPAIR;
    g_sender.last_notify_time_ms = bbt_os_get_time_ms();

    if (_send_export_end() != BBT_OK)
    {
      _set_error(BBT_PROTO_ERR_STORAGE);
    }
  }
}

void bbt_sender_handle_nack(const uint8_t *data, uint16_t len)
{
  bbt_nack_header_t hdr;
  const uint8_t    *bitmap_chunk = NULL;
  uint16_t          chunk_len    = 0u;

  if (!bbt_packet_parse_nack(data, len, &hdr, &bitmap_chunk, &chunk_len))
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: invalid NACK packet");
    return;
  }

  bool active = g_sender.active && (hdr.transfer_id == g_sender.transfer_id) &&
                ((g_sender.state == BBT_STATE_WAITING_EXPORT_REPAIR) || (g_sender.state == BBT_STATE_EXPORTING));

  if (!active)
  {
    return;
  }

  g_sender.last_rx_time_ms = bbt_os_get_time_ms();

  if ((bitmap_chunk == NULL) || (chunk_len == 0u))
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: empty NACK payload");
    return;
  }

  if ((g_sender.nack_bitmap_pos + chunk_len) > BBT_BITMAP_SIZE_BYTES)
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: NACK payload too large");
    _set_error(BBT_PROTO_ERR_INVALID_PACKET);
    return;
  }

  memcpy(&g_sender.nack_bitmap[g_sender.nack_bitmap_pos], bitmap_chunk, chunk_len);
  g_sender.nack_bitmap_pos += chunk_len;

  /* If more segments follow, wait until final segment is received. */
  if ((hdr.flags & 0x01u) != 0u)
  {
    return;
  }

  /* Final segment received: process assembled bitmap */
  uint16_t total_bitmap_bytes = g_sender.nack_bitmap_pos;
  uint16_t total_chunks       = g_sender.total_chunks;

  for (uint16_t seq = 0u; seq < total_chunks; seq++)
  {
    uint16_t byte_index = (uint16_t)(seq / 8u);
    uint8_t  bit_index  = (uint8_t)(seq % 8u);
    bool     received   = false;

    if (byte_index < total_bitmap_bytes)
    {
      received = (g_sender.nack_bitmap[byte_index] & (uint8_t)(1u << bit_index)) != 0u;
    }

    if (!received)
    {
      if (_send_export_chunk(seq) != BBT_OK)
      {
        _set_error(BBT_PROTO_ERR_STORAGE);
        g_sender.nack_bitmap_pos = 0u;
        return;
      }
    }
  }

  if (_send_export_end() != BBT_OK)
  {
    _set_error(BBT_PROTO_ERR_STORAGE);
    g_sender.nack_bitmap_pos = 0u;
    return;
  }

  g_sender.state               = BBT_STATE_WAITING_EXPORT_REPAIR;
  g_sender.last_notify_time_ms = bbt_os_get_time_ms();

  /* reset accumulator */
  g_sender.nack_bitmap_pos = 0u;

  (void)hdr.flags;
}

void bbt_sender_handle_complete_response(const uint8_t *data, uint16_t len)
{
  bbt_packet_id_t pkt;

  if (!bbt_packet_parse_complete(data, len, &pkt))
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: invalid COMPLETE packet");
    return;
  }

  bool active = g_sender.active && (pkt.transfer_id == g_sender.transfer_id);

  if (!active)
  {
    return;
  }

  const bbt_device_to_app_vtable_t *vt  = g_sender.vt;
  void                             *ctx = g_sender.vt_ctx;

  g_sender.active = false;
  g_sender.state  = BBT_STATE_COMPLETE;

  if ((vt != NULL) && (vt->on_export_complete != NULL))
  {
    (void)vt->on_export_complete(ctx);
  }
}

void bbt_sender_handle_error_response(const uint8_t *data, uint16_t len)
{
  bbt_error_packet_t pkt;

  if (!bbt_packet_parse_error(data, len, &pkt))
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: invalid ERROR packet");
    return;
  }

  if (!g_sender.active || (pkt.transfer_id != g_sender.transfer_id))
  {
    return;
  }

  _set_error((bbt_proto_status_t)pkt.error);
}

void bbt_sender_check_timeouts(void)
{
  bbt_os_mutex_t mutex = bbt_core_get_context()->worker.mutex;
  if (mutex != NULL)
  {
    bbt_os_mutex_lock(mutex);
  }

  bool export_active = g_sender.active && ((g_sender.state == BBT_STATE_WAITING_START_ACK) ||
                                           (g_sender.state == BBT_STATE_WAITING_EXPORT_REPAIR));

  if (!export_active)
  {
    if (mutex != NULL)
    {
      bbt_os_mutex_unlock(mutex);
    }
    return;
  }

  uint32_t now     = bbt_os_get_time_ms();
  uint32_t elapsed = now - g_sender.last_notify_time_ms;

  if (elapsed < BBT_IDLE_TIMEOUT_MS)
  {
    if (mutex != NULL)
    {
      bbt_os_mutex_unlock(mutex);
    }
    return;
  }

  if (g_sender.nack_retry_count >= BBT_NACK_RETRY_MAX)
  {
    if (mutex != NULL)
    {
      bbt_os_mutex_unlock(mutex);
    }
    _set_error(BBT_PROTO_ERR_TIMEOUT);
    return;
  }

  g_sender.nack_retry_count++;
  g_sender.last_notify_time_ms = now;

  bbt_state_t current_state = g_sender.state;

  if (mutex != NULL)
  {
    bbt_os_mutex_unlock(mutex);
  }

  if (current_state == BBT_STATE_WAITING_START_ACK)
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: START_ACK timeout, re-sending START packet (retry %u)", g_sender.nack_retry_count);
    if (_send_start_packet() != BBT_OK)
    {
      _set_error(BBT_PROTO_ERR_UNKNOWN);
    }
  }
  else if (current_state == BBT_STATE_WAITING_EXPORT_REPAIR)
  {
    bbt_port_log(BBT_LOG_WARN, "BBT: Repair response timeout, re-sending END packet (retry %u)",
                 g_sender.nack_retry_count);
    if (_send_export_end() != BBT_OK)
    {
      _set_error(BBT_PROTO_ERR_STORAGE);
    }
  }
}

static bbt_result_t _send_start_packet(void)
{
  bbt_start_meta_t start_meta;
  memset(&start_meta, 0, sizeof(start_meta));
  start_meta.transfer_id  = g_sender.transfer_id;
  start_meta.total_size   = g_sender.desc.total_size;
  start_meta.chunk_size   = g_sender.chunk_size;
  start_meta.total_chunks = g_sender.total_chunks;

  uint8_t *notify_buf = bbt_core_get_notify_buffer();
  uint16_t start_len  = bbt_packet_build_start(notify_buf, BBT_MAX_NOTIFY_SIZE, &start_meta);

  if (start_len == 0u)
  {
    return BBT_ERR_INTERNAL;
  }

  if (bbt_port_ble_indicate(notify_buf, start_len) != 0)
  {
    return BBT_ERR_BLE_NOTIFY;
  }

  bbt_core_emit_event(BBT_EVENT_STARTED, &start_meta);

  return BBT_OK;
}

static bbt_result_t _send_error(uint8_t transfer_id, bbt_proto_status_t error)
{
  uint8_t           *notify_buf = bbt_core_get_notify_buffer();
  bbt_error_packet_t pkt;
  pkt.transfer_id = transfer_id;
  pkt.error       = (uint8_t)error;
  uint16_t len    = bbt_packet_build_error(notify_buf, BBT_MAX_NOTIFY_SIZE, &pkt);

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

static bbt_result_t _send_export_chunk(uint16_t seq)
{
  if (!g_sender.active || (seq >= g_sender.total_chunks) || (g_sender.desc.read_fn == NULL))
  {
    return BBT_ERR_INVALID_STATE;
  }

  uint32_t offset      = ((uint32_t)seq) * ((uint32_t)g_sender.chunk_size);
  uint16_t payload_len = g_sender.chunk_size;

  if (seq == (uint16_t)(g_sender.total_chunks - 1u))
  {
    payload_len = (uint16_t)(g_sender.desc.total_size - offset);
  }

  if (payload_len > BBT_MAX_CHUNK_SIZE)
  {
    return BBT_ERR_PACKET_TOO_LARGE;
  }

  if (BBT_MAX_NOTIFY_SIZE < (uint16_t)(BBT_PACKET_CHUNK_HEADER_SIZE + payload_len))
  {
    return BBT_ERR_PACKET_TOO_LARGE;
  }

  uint8_t *notify_buf = bbt_core_get_notify_buffer();
  uint8_t *payload    = &notify_buf[BBT_PACKET_CHUNK_HEADER_SIZE];

  if (g_sender.desc.read_fn(offset, payload, payload_len, g_sender.chunk_size, g_sender.desc.ctx) != BBT_OK)
  {
    return BBT_ERR_STORAGE;
  }

  bbt_chunk_header_t header;
  header.transfer_id   = g_sender.transfer_id;
  header.seq           = seq;
  header.payload_len   = payload_len;
  header.payload_crc16 = 0u;

  uint16_t len = bbt_packet_build_chunk(notify_buf, BBT_MAX_NOTIFY_SIZE, &header, payload);

  if (len == 0u)
  {
    return BBT_ERR_INTERNAL;
  }

  if (bbt_port_ble_notify(notify_buf, len) != 0)
  {
    return BBT_ERR_BLE_NOTIFY;
  }

  return BBT_OK;
}

static bbt_result_t _send_export_end(void)
{
  uint8_t        *notify_buf = bbt_core_get_notify_buffer();
  bbt_packet_id_t pkt;
  pkt.transfer_id = g_sender.transfer_id;
  uint16_t len    = bbt_packet_build_end(notify_buf, BBT_MAX_NOTIFY_SIZE, &pkt);

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

/* stream_export and begin_export removed/refactored into non-blocking start_ack flow and bbt_sender_poll */

static void _set_error(bbt_proto_status_t error)
{
  uint8_t                           transfer_id = g_sender.transfer_id;
  const bbt_device_to_app_vtable_t *vt          = g_sender.vt;
  void                             *ctx         = g_sender.vt_ctx;

  g_sender.active = false;
  g_sender.state  = BBT_STATE_ERROR;

  if ((vt != NULL) && (vt->on_error != NULL))
  {
    (void)vt->on_error(transfer_id, error, ctx);
  }

  (void)_send_error(transfer_id, error);
  bbt_core_emit_event(BBT_EVENT_ERROR, NULL);
}

static void _reset_context(void)
{
  memset(&g_sender, 0, sizeof(g_sender));
  g_sender.state = BBT_STATE_IDLE;
}
