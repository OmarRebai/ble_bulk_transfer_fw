/**
 * @file            bbt_core.c
 * @brief           Core runtime: worker loop, dispatching and role management for BBT.
 * Updated with NACK bitmap refactor and Doxygen headers.
 * @date            25.05.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#include "bbt_core.h"

#include <string.h>

static bbt_mode_entry_t _mode_table[BBT_MODE_TABLE_SIZE];
static bbt_os_mutex_t   _mode_mutex  = NULL;
static bbt_event_cb_t   _event_cb    = NULL;
static bool             _initialized = false;
static bbt_core_ctx_t   _core_ctx;
static uint8_t          _notify_buf[BBT_MAX_NOTIFY_SIZE];

typedef enum
{
  BBT_RX_STATE_OPCODE = 0,
  BBT_RX_STATE_START_FIXED,
  BBT_RX_STATE_START_ACK_TAIL,
  BBT_RX_STATE_CHUNK_FIXED,
  BBT_RX_STATE_CHUNK_PAYLOAD,
  BBT_RX_STATE_PULL_REQ_TAIL,
  BBT_RX_STATE_NACK_FIXED,
  BBT_RX_STATE_NACK_BITMAP,
  BBT_RX_STATE_SHORT_TAIL,
} bbt_rx_parse_state_t;

typedef struct
{
  bbt_rx_parse_state_t state;
  uint16_t             packet_len;
  uint16_t             pending_len;
  uint8_t              packet[BBT_MAX_RX_PACKET_SIZE];
} bbt_rx_parser_t;

static bbt_rx_parser_t _rx_parser;

static void         _worker_task(void *arg);
static void         _process_worker_item(const bbt_rx_queue_item_t *item);
static bool         _is_sender_opcode(uint8_t opcode);
static void         _rx_reset(void);
static bbt_result_t _enqueue_complete_packet(const uint8_t *data, uint16_t len);
static bbt_result_t _rx_finish_packet(void);

bbt_result_t bbt_core_enqueue_packet(const uint8_t *data, uint16_t len)
{
  if ((data == NULL) || (len == 0u))
  {
    return BBT_ERR_INVALID_ARG;
  }

  if (!_initialized || (_core_ctx.worker.queue == NULL))
  {
    return BBT_ERR_INVALID_STATE;
  }

  bbt_result_t result = BBT_OK;

  for (uint16_t index = 0u; index < len; ++index)
  {
    if (_rx_parser.packet_len >= sizeof(_rx_parser.packet))
    {
      bbt_port_log(BBT_LOG_WARN, "BBT frame overflow");
      _rx_reset();
      return BBT_ERR_PACKET_TOO_LARGE;
    }

    _rx_parser.packet[_rx_parser.packet_len++] = data[index];

    if (_rx_parser.state == BBT_RX_STATE_OPCODE)
    {
      switch (_rx_parser.packet[0])
      {
      case BBT_OPCODE_START:
        _rx_parser.state       = BBT_RX_STATE_START_FIXED;
        _rx_parser.pending_len = (uint16_t)sizeof(bbt_start_meta_t);
        break;

      case BBT_OPCODE_START_ACK:
        _rx_parser.state       = BBT_RX_STATE_START_ACK_TAIL;
        _rx_parser.pending_len = (uint16_t)sizeof(bbt_start_ack_t);
        break;

      case BBT_OPCODE_CHUNK:
        _rx_parser.state       = BBT_RX_STATE_CHUNK_FIXED;
        _rx_parser.pending_len = (uint16_t)sizeof(bbt_chunk_header_t);
        break;

      case BBT_OPCODE_PULL_REQ:
        _rx_parser.state       = BBT_RX_STATE_PULL_REQ_TAIL;
        _rx_parser.pending_len = (uint16_t)sizeof(bbt_pull_req_t);
        break;

      case BBT_OPCODE_NACK:
        _rx_parser.state       = BBT_RX_STATE_NACK_FIXED;
        _rx_parser.pending_len = (uint16_t)sizeof(bbt_nack_header_t);
        break;

      case BBT_OPCODE_COMPLETE:
        _rx_parser.state       = BBT_RX_STATE_SHORT_TAIL;
        _rx_parser.pending_len = (uint16_t)sizeof(bbt_packet_id_t);
        break;

      case BBT_OPCODE_ERROR:
        _rx_parser.state       = BBT_RX_STATE_SHORT_TAIL;
        _rx_parser.pending_len = (uint16_t)sizeof(bbt_error_packet_t);
        break;

      case BBT_OPCODE_END:
      case BBT_OPCODE_ABORT:
        _rx_parser.state       = BBT_RX_STATE_SHORT_TAIL;
        _rx_parser.pending_len = (uint16_t)sizeof(bbt_packet_id_t);
        break;

      default:
        bbt_port_log(BBT_LOG_WARN, "BBT: unknown opcode 0x%02X", _rx_parser.packet[0]);
        _rx_reset();
        return BBT_ERR_PACKET_INVALID;
      }

      continue;
    }

    if (_rx_parser.pending_len > 0u)
    {
      _rx_parser.pending_len--;
    }

    if (_rx_parser.pending_len != 0u)
    {
      continue;
    }

    switch (_rx_parser.state)
    {
    case BBT_RX_STATE_START_FIXED: {
      bbt_result_t packet_result = _rx_finish_packet();
      if ((result == BBT_OK) && (packet_result != BBT_OK))
      {
        result = packet_result;
      }
      break;
    }

    case BBT_RX_STATE_CHUNK_FIXED: {
      uint16_t payload_len = (uint16_t)_rx_parser.packet[4] | ((uint16_t)_rx_parser.packet[5] << 8);

      if (payload_len > BBT_MAX_CHUNK_SIZE)
      {
        bbt_port_log(BBT_LOG_WARN, "BBT CHUNK payload too large: %u", payload_len);
        _rx_reset();
        return BBT_ERR_PACKET_TOO_LARGE;
      }

      _rx_parser.state       = BBT_RX_STATE_CHUNK_PAYLOAD;
      _rx_parser.pending_len = payload_len;

      if (_rx_parser.pending_len == 0u)
      {
        bbt_result_t packet_result = _rx_finish_packet();
        if ((result == BBT_OK) && (packet_result != BBT_OK))
        {
          result = packet_result;
        }
      }
      break;
    }

    case BBT_RX_STATE_START_ACK_TAIL:
    case BBT_RX_STATE_CHUNK_PAYLOAD:
    case BBT_RX_STATE_PULL_REQ_TAIL:
    case BBT_RX_STATE_SHORT_TAIL: {
      bbt_result_t packet_result = _rx_finish_packet();
      if ((result == BBT_OK) && (packet_result != BBT_OK))
      {
        result = packet_result;
      }
      break;
    }

    case BBT_RX_STATE_NACK_FIXED: {
      uint16_t bitmap_len = (uint16_t)_rx_parser.packet[3] | ((uint16_t)_rx_parser.packet[4] << 8);
      uint16_t max_len    = (uint16_t)(sizeof(_rx_parser.packet) - (1u + sizeof(bbt_nack_header_t)));

      if ((bitmap_len > BBT_BITMAP_SIZE_BYTES) || (bitmap_len > max_len))
      {
        bbt_port_log(BBT_LOG_WARN, "BBT NACK bitmap too large: %u", bitmap_len);
        _rx_reset();
        return BBT_ERR_PACKET_TOO_LARGE;
      }

      _rx_parser.state       = BBT_RX_STATE_NACK_BITMAP;
      _rx_parser.pending_len = bitmap_len;

      if (_rx_parser.pending_len == 0u)
      {
        bbt_result_t packet_result = _rx_finish_packet();
        if ((result == BBT_OK) && (packet_result != BBT_OK))
        {
          result = packet_result;
        }
      }
      break;
    }

    case BBT_RX_STATE_NACK_BITMAP: {
      bbt_result_t packet_result = _rx_finish_packet();
      if ((result == BBT_OK) && (packet_result != BBT_OK))
      {
        result = packet_result;
      }
      break;
    }

    case BBT_RX_STATE_OPCODE:
    default:
      break;
    }
  }

  return result;
}

void bbt_core_worker_service(bool wait_for_item)
{
  if (!_initialized || !_core_ctx.worker.running)
  {
    return;
  }

  bbt_rx_queue_item_t item;

  uint32_t timeout_ms = wait_for_item ? BBT_WORKER_POLL_MS : 0u;

  if (bbt_os_queue_receive(_core_ctx.worker.queue, &item, timeout_ms))
  {
    _process_worker_item(&item);
  }

  bbt_core_check_role_timeouts(_core_ctx.role);
  if (_core_ctx.role == BBT_ROLE_SENDER)
  {
    bbt_sender_poll();
  }
}

bbt_core_ctx_t *bbt_core_get_context(void)
{
  return &_core_ctx;
}

uint8_t *bbt_core_get_notify_buffer(void)
{
  return _notify_buf;
}

bool bbt_core_can_start_sender(void)
{
  if (!_initialized)
  {
    return false;
  }

  bool can_start = true;

  if (_core_ctx.worker.mutex != NULL)
  {
    bbt_os_mutex_lock(_core_ctx.worker.mutex);
  }

  if ((_core_ctx.role == BBT_ROLE_RECEIVER) && (_core_ctx.receiver.state != BBT_STATE_IDLE))
  {
    can_start = false;
  }

  if (_core_ctx.worker.mutex != NULL)
  {
    bbt_os_mutex_unlock(_core_ctx.worker.mutex);
  }

  return can_start;
}

bbt_result_t bbt_core_init(void)
{
  if (_initialized)
  {
    return BBT_OK;
  }

  memset(&_core_ctx, 0, sizeof(_core_ctx));
  _core_ctx.role           = BBT_ROLE_IDLE;
  _core_ctx.max_chunk_size = BBT_MAX_CHUNK_SIZE;
  _rx_reset();

  if (_mode_mutex == NULL)
  {
    _mode_mutex = bbt_os_mutex_create();
  }

  bbt_result_t result = bbt_sender_init();
  if (result != BBT_OK)
  {
    if (_mode_mutex != NULL)
    {
      bbt_os_mutex_delete(_mode_mutex);
      _mode_mutex = NULL;
    }
    memset(_mode_table, 0, sizeof(_mode_table));
    return result;
  }

  result = bbt_receiver_init();
  if (result != BBT_OK)
  {
    (void)bbt_sender_deinit();
    if (_mode_mutex != NULL)
    {
      bbt_os_mutex_delete(_mode_mutex);
      _mode_mutex = NULL;
    }
    memset(_mode_table, 0, sizeof(_mode_table));
    return result;
  }

  _core_ctx.worker.queue = bbt_os_queue_create(sizeof(bbt_rx_queue_item_t), BBT_RX_QUEUE_DEPTH);
  if (_core_ctx.worker.queue == NULL)
  {
    (void)bbt_receiver_deinit();
    (void)bbt_sender_deinit();
    if (_mode_mutex != NULL)
    {
      bbt_os_mutex_delete(_mode_mutex);
      _mode_mutex = NULL;
    }
    memset(_mode_table, 0, sizeof(_mode_table));
    return BBT_ERR_NO_MEMORY;
  }

  _core_ctx.worker.mutex = bbt_os_mutex_create();
  if (_core_ctx.worker.mutex == NULL)
  {
    bbt_os_queue_delete(_core_ctx.worker.queue);
    _core_ctx.worker.queue = NULL;
    (void)bbt_receiver_deinit();
    (void)bbt_sender_deinit();
    if (_mode_mutex != NULL)
    {
      bbt_os_mutex_delete(_mode_mutex);
      _mode_mutex = NULL;
    }
    memset(_mode_table, 0, sizeof(_mode_table));
    return BBT_ERR_NO_MEMORY;
  }

  _core_ctx.worker.running = true;

#if defined(BBT_OS_CMSIS) || defined(BBT_OS_SEQUENCER)
  _core_ctx.worker.thread =
      bbt_os_thread_create(_worker_task, NULL, "bbt_worker", BBT_RX_THREAD_STACK_SIZE, BBT_RX_THREAD_PRIORITY);

  if (_core_ctx.worker.thread == NULL)
  {
    _core_ctx.worker.running = false;
    bbt_os_mutex_delete(_core_ctx.worker.mutex);
    bbt_os_queue_delete(_core_ctx.worker.queue);
    _core_ctx.worker.mutex = NULL;
    _core_ctx.worker.queue = NULL;
    (void)bbt_receiver_deinit();
    (void)bbt_sender_deinit();
    if (_mode_mutex != NULL)
    {
      bbt_os_mutex_delete(_mode_mutex);
      _mode_mutex = NULL;
    }
    memset(_mode_table, 0, sizeof(_mode_table));
    return BBT_ERR_NO_MEMORY;
  }
#else
  _core_ctx.worker.thread = NULL;
#endif

  memset(_mode_table, 0, sizeof(_mode_table));
  _initialized = true;

  return BBT_OK;
}

bbt_result_t bbt_core_deinit(void)
{
  if (!_initialized)
  {
    return BBT_OK;
  }

  (void)bbt_receiver_deinit();
  (void)bbt_sender_deinit();

  _core_ctx.worker.running = false;

#if defined(BBT_OS_CMSIS) || defined(BBT_OS_SEQUENCER)
  if (_core_ctx.worker.thread != NULL)
  {
    bbt_os_thread_delete(_core_ctx.worker.thread);
    _core_ctx.worker.thread = NULL;
  }
#endif

  if (_core_ctx.worker.queue != NULL)
  {
    bbt_os_queue_delete(_core_ctx.worker.queue);
    _core_ctx.worker.queue = NULL;
  }

  if (_core_ctx.worker.mutex != NULL)
  {
    bbt_os_mutex_delete(_core_ctx.worker.mutex);
    _core_ctx.worker.mutex = NULL;
  }

  if (_mode_mutex != NULL)
  {
    bbt_os_mutex_delete(_mode_mutex);
    _mode_mutex = NULL;
  }

  memset(_mode_table, 0, sizeof(_mode_table));
  memset(&_core_ctx, 0, sizeof(_core_ctx));
  _rx_reset();
  _initialized = false;

  return BBT_OK;
}

bbt_result_t bbt_core_abort(void)
{
  if (!_initialized)
  {
    return BBT_ERR_INVALID_STATE;
  }

  if ((_core_ctx.role == BBT_ROLE_SENDER) || _core_ctx.sender.active)
  {
    return bbt_sender_abort();
  }

  return bbt_receiver_abort();
}

bbt_result_t bbt_core_get_status(bbt_status_t *status)
{
  if (!_initialized)
  {
    return BBT_ERR_INVALID_STATE;
  }

  if (status == NULL)
  {
    return BBT_ERR_INVALID_ARG;
  }

  if ((_core_ctx.role == BBT_ROLE_SENDER) || _core_ctx.sender.active)
  {
    return bbt_sender_get_status(status);
  }

  return bbt_receiver_get_status(status);
}

void bbt_core_set_max_chunk_size(uint16_t max_chunk_size)
{
  if (!_initialized)
  {
    return;
  }

  _core_ctx.max_chunk_size = (max_chunk_size > BBT_MAX_CHUNK_SIZE) ? BBT_MAX_CHUNK_SIZE : max_chunk_size;
  bbt_receiver_set_max_chunk_size(_core_ctx.max_chunk_size);
  bbt_sender_set_max_chunk_size(_core_ctx.max_chunk_size);
}

bbt_result_t bbt_mode_register(uint8_t index, const bbt_mode_entry_t *entry)
{
  if (index >= BBT_MODE_TABLE_SIZE)
  {
    return BBT_ERR_INVALID_ARG;
  }

  if (_mode_mutex != NULL)
  {
    bbt_os_mutex_lock(_mode_mutex);
  }

  if (entry != NULL)
  {
    memcpy(&_mode_table[index], entry, sizeof(bbt_mode_entry_t));
  }
  else
  {
    memset(&_mode_table[index], 0, sizeof(bbt_mode_entry_t));
  }

  if (_mode_mutex != NULL)
  {
    bbt_os_mutex_unlock(_mode_mutex);
  }

  return BBT_OK;
}

bbt_result_t bbt_mode_get_entry(uint8_t index, bbt_mode_entry_t *entry)
{
  if ((index >= BBT_MODE_TABLE_SIZE) || (entry == NULL))
  {
    return BBT_ERR_INVALID_ARG;
  }

  if (_mode_mutex != NULL)
  {
    bbt_os_mutex_lock(_mode_mutex);
  }

  *entry = _mode_table[index];

  if (_mode_mutex != NULL)
  {
    bbt_os_mutex_unlock(_mode_mutex);
  }

  return BBT_OK;
}

bbt_role_t bbt_core_select_role(bbt_role_t current_role, uint8_t opcode)
{
  if (current_role != BBT_ROLE_IDLE)
  {
    return current_role;
  }

  if (_is_sender_opcode(opcode))
  {
    return BBT_ROLE_SENDER;
  }

  return BBT_ROLE_RECEIVER;
}

void bbt_core_update_role_after_processing(bbt_role_t *role, bbt_state_t receiver_state)
{
  if (role == NULL)
  {
    return;
  }

  if ((*role == BBT_ROLE_SENDER) && !bbt_sender_is_active())
  {
    *role = BBT_ROLE_IDLE;
    return;
  }

  if (*role == BBT_ROLE_RECEIVER)
  {
    bool upload_active = (receiver_state == BBT_STATE_RECEIVING) || (receiver_state == BBT_STATE_WAITING_REPAIR) ||
                         (receiver_state == BBT_STATE_VERIFYING);

    if (!upload_active)
    {
      *role = BBT_ROLE_IDLE;
    }
  }
}

void bbt_core_check_role_timeouts(bbt_role_t role)
{
  if (role == BBT_ROLE_SENDER)
  {
    bbt_sender_check_timeouts();
    return;
  }

  if (role == BBT_ROLE_RECEIVER)
  {
    bbt_receiver_check_timeouts();
  }
}

void bbt_core_process_sender_packet(const uint8_t *data, uint16_t len)
{
  if ((data == NULL) || (len == 0u))
  {
    return;
  }

  switch (data[0])
  {
  case BBT_OPCODE_PULL_REQ:
    bbt_sender_handle_pull_req(data, len);
    break;

  case BBT_OPCODE_START_ACK:
    bbt_sender_handle_start_ack(data, len);
    break;

  case BBT_OPCODE_NACK:
    bbt_sender_handle_nack(data, len);
    break;

  case BBT_OPCODE_COMPLETE:
    bbt_sender_handle_complete_response(data, len);
    break;

  case BBT_OPCODE_ERROR:
    bbt_sender_handle_error_response(data, len);
    break;

  case BBT_OPCODE_START:
  case BBT_OPCODE_CHUNK:
  case BBT_OPCODE_END:
  case BBT_OPCODE_ABORT:
    bbt_port_log(BBT_LOG_WARN, "BBT: receiver opcode 0x%02X on sender role", data[0]);
    break;

  default:
    bbt_port_log(BBT_LOG_WARN, "BBT: unknown opcode 0x%02X", data[0]);
    break;
  }
}

void bbt_core_register_event_callback(bbt_event_cb_t cb)
{
  _event_cb = cb;
}

void bbt_core_reset_rx_parser(void)
{
  _rx_reset();
}

void bbt_core_emit_event(bbt_event_t event, const void *event_data)
{
  if (_event_cb != NULL)
  {
    _event_cb(event, event_data);
  }
}

/* ============================================================================
 * Static helpers
 * ==========================================================================*/

static void _rx_reset(void)
{
  memset(&_rx_parser, 0, sizeof(_rx_parser));
  _rx_parser.state = BBT_RX_STATE_OPCODE;
}

static bbt_result_t _enqueue_complete_packet(const uint8_t *data, uint16_t len)
{
  if ((data == NULL) || (len == 0u))
  {
    return BBT_ERR_INVALID_ARG;
  }

  if (len > BBT_MAX_RX_PACKET_SIZE)
  {
    return BBT_ERR_PACKET_TOO_LARGE;
  }

  if (!_initialized || (_core_ctx.worker.queue == NULL))
  {
    return BBT_ERR_INVALID_STATE;
  }

  bbt_rx_queue_item_t item;
  item.len = len;
  memcpy(item.data, data, len);

  if (!bbt_os_queue_send(_core_ctx.worker.queue, &item, 0u))
  {
    if (_core_ctx.worker.mutex != NULL)
    {
      bbt_os_mutex_lock(_core_ctx.worker.mutex);
      _core_ctx.receiver.queue_drops++;
      bbt_os_mutex_unlock(_core_ctx.worker.mutex);
    }

    return BBT_ERR_QUEUE_FULL;
  }

  return BBT_OK;
}

static bbt_result_t _rx_finish_packet(void)
{
  if (_rx_parser.packet_len == 0u)
  {
    _rx_reset();
    return BBT_ERR_INVALID_ARG;
  }

  bbt_result_t result = _enqueue_complete_packet(_rx_parser.packet, _rx_parser.packet_len);
  if (result != BBT_OK)
  {
    bbt_port_log(BBT_LOG_WARN, "BBT enqueue rejected: %d", (int)result);
  }

  _rx_reset();
  return result;
}

/**
 * @brief Process a queued RX item and dispatch to the appropriate role handler.
 *
 * This function inspects the packet opcode and transfer direction bit to
 * determine whether the item belongs to the sender or receiver and forwards
 * the buffer to the responsible module.
 *
 * @param item Pointer to the queued RX item (copied from BLE callback).
 */
static void _process_worker_item(const bbt_rx_queue_item_t *item)
{
  if ((item == NULL) || (item->len == 0u))
  {
    return;
  }

  bbt_role_t role;

  bbt_os_mutex_lock(_core_ctx.worker.mutex);
  role = _core_ctx.role;
  bbt_os_mutex_unlock(_core_ctx.worker.mutex);

  uint8_t opcode = item->data[0];
  if (opcode == BBT_OPCODE_START)
  {
    bbt_port_log(BBT_LOG_INFO, "BBT Core: Received new START packet, resetting protocol state");
    bbt_os_mutex_lock(_core_ctx.worker.mutex);
    (void)bbt_sender_deinit();
    (void)bbt_receiver_deinit();
    _core_ctx.role = BBT_ROLE_RECEIVER;
    role           = BBT_ROLE_RECEIVER;
    bbt_os_mutex_unlock(_core_ctx.worker.mutex);
  }
  else if (opcode == BBT_OPCODE_PULL_REQ)
  {
    bbt_port_log(BBT_LOG_INFO, "BBT Core: Received new PULL_REQ packet, resetting protocol state");
    bbt_os_mutex_lock(_core_ctx.worker.mutex);
    (void)bbt_receiver_deinit();
    (void)bbt_sender_deinit();
    _core_ctx.role = BBT_ROLE_SENDER;
    role           = BBT_ROLE_SENDER;
    bbt_os_mutex_unlock(_core_ctx.worker.mutex);
  }
  else
  {
    role = bbt_core_select_role(role, opcode);

    if (item->len >= 2u)
    {
      if ((item->data[1] & BBT_TRANSFER_ID_DIR_BIT) != 0u)
      {
        role = BBT_ROLE_SENDER;
      }
      else
      {
        role = BBT_ROLE_RECEIVER;
      }
    }
  }

  if (role == BBT_ROLE_SENDER)
  {
    bbt_core_process_sender_packet(item->data, item->len);
  }
  else
  {
    bbt_receiver_process_item(item);
  }

  bbt_os_mutex_lock(_core_ctx.worker.mutex);
  _core_ctx.role = role;
  bbt_core_update_role_after_processing(&_core_ctx.role, _core_ctx.receiver.state);
  bbt_os_mutex_unlock(_core_ctx.worker.mutex);
}

/**
 * @brief Worker thread main loop used when an OS thread is created.
 *
 * Continuously services the core worker while the running flag is set.
 *
 * @param arg Unused context pointer passed by thread creation helper.
 */
static void _worker_task(void *arg)
{
  (void)arg;

#if defined(BBT_OS_SEQUENCER)
  bbt_core_worker_service(true);
#else
  while (_core_ctx.worker.running)
  {
    bbt_core_worker_service(true);
  }
#endif
}

/**
 * @brief Determine whether the opcode is a sender-side control opcode.
 *
 * Returns true for opcodes that are generated by the receiver side and should
 * be handled by the sender role (e.g. NACK, START_ACK, COMPLETE, ERROR).
 *
 * @param opcode First byte of the packet (opcode).
 * @return true if opcode belongs to sender messages.
 */
static bool _is_sender_opcode(uint8_t opcode)
{
  switch (opcode)
  {
  case BBT_OPCODE_PULL_REQ:
  case BBT_OPCODE_START_ACK:
  case BBT_OPCODE_NACK:
  case BBT_OPCODE_COMPLETE:
  case BBT_OPCODE_ERROR:
    return true;

  default:
    return false;
  }
}
