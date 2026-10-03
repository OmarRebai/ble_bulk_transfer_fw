#include "unity.h"

#include "bbt_packet.h"
#include "bbt_receiver.h"

#include "mock_bbt_core.h"

#include <stdint.h>
#include <string.h>

extern void fake_bbt_port_reset(void);
extern uint32_t fake_time_ms;
extern int fake_ble_indicate_result;
extern uint8_t fake_ble_indicate_data[BBT_MAX_NOTIFY_SIZE];
extern uint16_t fake_ble_indicate_len;
extern uint32_t fake_ble_indicate_count;

static bbt_core_ctx_t core_ctx;
static uint8_t notify_buf[BBT_MAX_NOTIFY_SIZE];
static uint8_t packet_buf[BBT_MAX_NOTIFY_SIZE];
static uint8_t payload_buf[BBT_MAX_CHUNK_SIZE];
static uint32_t start_count;
static uint32_t chunk_count;
static uint32_t complete_count;
static uint32_t abort_count;
static uint32_t error_count;
static uint16_t last_complete_id;
static uint16_t last_abort_id;
static uint16_t last_chunk_seq;
static bbt_proto_status_t last_error_status;
static bbt_result_t mode_get_result;
static bbt_result_t on_start_result;
static bbt_result_t on_chunk_result;
static bbt_app_to_device_vtable_t app_vt;
static bbt_mode_entry_t mode_entry;
static void *const app_ctx = (void *)0xA5A55A5Au;

static bbt_result_t on_start_cb(const bbt_start_meta_t *meta, void *ctx)
{
    TEST_ASSERT_NOT_NULL(meta);
    TEST_ASSERT_EQUAL_PTR(app_ctx, ctx);
    start_count++;
    return on_start_result;
}

static bbt_result_t on_chunk_cb(const bbt_chunk_packet_t *chunk, void *ctx)
{
    TEST_ASSERT_NOT_NULL(chunk);
    TEST_ASSERT_EQUAL_PTR(app_ctx, ctx);
    chunk_count++;
    last_chunk_seq = chunk->header.seq;
    return on_chunk_result;
}

static bbt_result_t on_abort_cb(uint16_t transfer_id, void *ctx)
{
    TEST_ASSERT_EQUAL_PTR(app_ctx, ctx);
    abort_count++;
    last_abort_id = transfer_id;
    return BBT_OK;
}

static bbt_result_t on_complete_cb(uint16_t transfer_id, void *ctx)
{
    TEST_ASSERT_EQUAL_PTR(app_ctx, ctx);
    complete_count++;
    last_complete_id = transfer_id;
    return BBT_OK;
}

static bbt_result_t on_error_cb(uint16_t transfer_id,
                                bbt_proto_status_t status,
                                void *ctx)
{
    TEST_ASSERT_EQUAL_PTR(app_ctx, ctx);
    (void)transfer_id;
    error_count++;
    last_error_status = status;
    return BBT_OK;
}

static bbt_result_t mode_get_entry_cb(uint8_t index,
                                      bbt_mode_entry_t *entry,
                                      int cmock_num_calls)
{
    (void)cmock_num_calls;

    if (mode_get_result == BBT_OK)
    {
        TEST_ASSERT_TRUE(index < BBT_MODE_TABLE_SIZE);
        TEST_ASSERT_NOT_NULL(entry);
        *entry = mode_entry;
    }

    return mode_get_result;
}

static void emit_event_complete_cb(bbt_event_t event,
                                   const void *event_data,
                                   int cmock_num_calls)
{
    (void)cmock_num_calls;
    TEST_ASSERT_EQUAL(BBT_EVENT_COMPLETE, event);
    TEST_ASSERT_NULL(event_data);
    TEST_ASSERT_EQUAL_UINT32(1u, complete_count);
}

static void process_packet(const uint8_t *data, uint16_t len)
{
    bbt_rx_queue_item_t item;
    memset(&item, 0, sizeof(item));
    item.len = len;
    memcpy(item.data, data, len);

    bbt_receiver_process_item(&item);
}

static uint16_t build_start(uint8_t transfer_id,
                            uint32_t total_size,
                            uint16_t chunk_size,
                            uint16_t total_chunks)
{
    bbt_start_meta_t meta = {
        .transfer_id = transfer_id,
        .total_size = total_size,
        .chunk_size = chunk_size,
        .total_chunks = total_chunks,
    };

    return bbt_packet_build_start(packet_buf, sizeof(packet_buf), &meta);
}

static uint16_t build_chunk(uint8_t transfer_id, uint16_t seq, uint16_t payload_len)
{
    for (uint16_t i = 0u; i < payload_len; ++i)
    {
        payload_buf[i] = (uint8_t)(seq + i);
    }

    bbt_chunk_header_t header = {
        .transfer_id = transfer_id,
        .seq = seq,
        .payload_len = payload_len,
        .payload_crc16 = 0u,
    };

    return bbt_packet_build_chunk(packet_buf, sizeof(packet_buf), &header, payload_buf);
}

static uint16_t build_end(uint8_t transfer_id)
{
    bbt_packet_id_t id = {
        .transfer_id = transfer_id,
    };

    return bbt_packet_build_end(packet_buf, sizeof(packet_buf), &id);
}

static uint16_t build_abort(uint8_t transfer_id)
{
    packet_buf[0] = BBT_OPCODE_ABORT;
    packet_buf[1] = transfer_id;
    return 2u;
}

static void start_receive(uint8_t transfer_id,
                          uint32_t total_size,
                          uint16_t chunk_size,
                          uint16_t total_chunks)
{
    uint16_t len = build_start(transfer_id, total_size, chunk_size, total_chunks);
    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_RECEIVING, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT8(transfer_id, core_ctx.receiver.transfer_id);
}

void setUp(void)
{
    memset(&core_ctx, 0, sizeof(core_ctx));
    memset(notify_buf, 0, sizeof(notify_buf));
    memset(packet_buf, 0, sizeof(packet_buf));
    memset(payload_buf, 0, sizeof(payload_buf));

    fake_bbt_port_reset();
    start_count = 0u;
    chunk_count = 0u;
    complete_count = 0u;
    abort_count = 0u;
    error_count = 0u;
    last_complete_id = 0u;
    last_abort_id = 0u;
    last_chunk_seq = 0u;
    last_error_status = BBT_PROTO_STATUS_OK;
    mode_get_result = BBT_OK;
    on_start_result = BBT_OK;
    on_chunk_result = BBT_OK;

    core_ctx.max_chunk_size = BBT_MAX_CHUNK_SIZE;
    core_ctx.worker.queue = (bbt_os_queue_t)1;
    core_ctx.worker.mutex = (bbt_os_mutex_t)1;

    app_vt.on_start = on_start_cb;
    app_vt.on_chunk = on_chunk_cb;
    app_vt.on_complete = on_complete_cb;
    app_vt.on_abort = on_abort_cb;
    app_vt.on_error = on_error_cb;

    mode_entry.app_to_device = &app_vt;
    mode_entry.device_to_app = NULL;
    mode_entry.ctx = app_ctx;

    bbt_core_get_context_IgnoreAndReturn(&core_ctx);
    bbt_core_get_notify_buffer_IgnoreAndReturn(notify_buf);
    bbt_core_emit_event_Ignore();
    bbt_mode_get_entry_StubWithCallback(mode_get_entry_cb);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_receiver_init());
}

void tearDown(void)
{
    (void)bbt_receiver_deinit();
}

void test_init_deinit_and_idle_helpers_should_reset_context(void)
{
    core_ctx.receiver.state = BBT_STATE_RECEIVING;
    core_ctx.receiver.valid_chunks = 3u;

    TEST_ASSERT_EQUAL(BBT_OK, bbt_receiver_deinit());
    TEST_ASSERT_EQUAL(BBT_STATE_IDLE, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(0u, core_ctx.receiver.valid_chunks);
    TEST_ASSERT_TRUE(bbt_receiver_is_upload_idle());
}

void test_set_max_chunk_size_should_accept_zero_clamp_and_smaller_values(void)
{
    bbt_receiver_set_max_chunk_size(0u);
    TEST_ASSERT_EQUAL_UINT16(0u, core_ctx.max_chunk_size);

    bbt_receiver_set_max_chunk_size(BBT_MAX_CHUNK_SIZE + 1u);
    TEST_ASSERT_EQUAL_UINT16(BBT_MAX_CHUNK_SIZE, core_ctx.max_chunk_size);

    bbt_receiver_set_max_chunk_size(64u);
    TEST_ASSERT_EQUAL_UINT16(64u, core_ctx.max_chunk_size);
}

void test_get_status_should_reject_null_and_copy_receiver_snapshot(void)
{
    bbt_status_t status;

    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_ARG, bbt_receiver_get_status(NULL));

    core_ctx.receiver.state = BBT_STATE_RECEIVING;
    core_ctx.receiver.transfer_id = 0x22u;
    core_ctx.receiver.total_size = 100u;
    core_ctx.receiver.chunk_size = 25u;
    core_ctx.receiver.total_chunks = 4u;
    core_ctx.receiver.valid_chunks = 2u;
    core_ctx.receiver.bytes_written = 50u;
    core_ctx.receiver.high_watermark_next = 2u;
    core_ctx.receiver.queue_drops = 1u;
    core_ctx.receiver.crc_errors = 2u;
    core_ctx.receiver.duplicate_chunks = 3u;
    core_ctx.receiver.storage_errors = 4u;
    core_ctx.receiver.last_rx_time_ms = 123u;

    TEST_ASSERT_EQUAL(BBT_OK, bbt_receiver_get_status(&status));
    TEST_ASSERT_EQUAL(BBT_STATE_RECEIVING, status.state);
    TEST_ASSERT_EQUAL_UINT8(0x22u, status.transfer_id);
    TEST_ASSERT_EQUAL_UINT32(100u, status.total_size);
    TEST_ASSERT_EQUAL_UINT32(50u, status.bytes_written);
    TEST_ASSERT_EQUAL_UINT32(4u, status.storage_errors);
}

void test_poll_should_delegate_to_core_worker_service(void)
{
    bbt_core_worker_service_Expect(false);

    bbt_receiver_poll();
}

void test_process_item_should_ignore_null_empty_sender_and_unknown_opcodes(void)
{
    bbt_rx_queue_item_t item;
    memset(&item, 0, sizeof(item));

    bbt_receiver_process_item(NULL);
    bbt_receiver_process_item(&item);

    item.len = 1u;
    item.data[0] = BBT_OPCODE_PULL_REQ;
    bbt_receiver_process_item(&item);

    item.data[0] = 0xFFu;
    bbt_receiver_process_item(&item);

    TEST_ASSERT_EQUAL(BBT_STATE_IDLE, core_ctx.receiver.state);
}

void test_start_should_initialize_receive_context_and_send_ok_ack(void)
{
    uint16_t len = build_start(0x02u, 300u, 100u, 3u);

    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT32(1u, start_count);
    TEST_ASSERT_EQUAL(BBT_STATE_RECEIVING, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(300u, core_ctx.receiver.total_size);
    TEST_ASSERT_EQUAL_UINT16(100u, core_ctx.receiver.chunk_size);
    TEST_ASSERT_EQUAL_UINT16(3u, core_ctx.receiver.total_chunks);

    bbt_start_ack_t ack;
    TEST_ASSERT_TRUE(bbt_packet_parse_start_ack(fake_ble_indicate_data,
                                                fake_ble_indicate_len,
                                                &ack));
    TEST_ASSERT_EQUAL_UINT8(0x02u, ack.transfer_id);
    TEST_ASSERT_EQUAL_UINT8(BBT_PROTO_STATUS_OK, ack.status);
    TEST_ASSERT_EQUAL_UINT16(100u, ack.accepted_chunk_size);
}

void test_start_should_ignore_when_sender_role_or_sender_active(void)
{
    uint16_t len = build_start(0x02u, 300u, 100u, 3u);

    core_ctx.role = BBT_ROLE_SENDER;
    process_packet(packet_buf, len);
    TEST_ASSERT_EQUAL_UINT32(0u, start_count);

    core_ctx.role = BBT_ROLE_IDLE;
    core_ctx.sender.active = true;
    process_packet(packet_buf, len);
    TEST_ASSERT_EQUAL_UINT32(0u, start_count);
}

void test_start_should_ignore_invalid_packet(void)
{
    packet_buf[0] = BBT_OPCODE_CHUNK;

    process_packet(packet_buf, 1u);

    TEST_ASSERT_EQUAL(BBT_STATE_IDLE, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(0u, fake_ble_indicate_count);
}

void test_start_should_reject_invalid_metadata(void)
{
    uint16_t len = build_start(0x02u, 300u, 0u, 3u);

    process_packet(packet_buf, len);

    bbt_start_ack_t ack;
    TEST_ASSERT_TRUE(bbt_packet_parse_start_ack(fake_ble_indicate_data,
                                                fake_ble_indicate_len,
                                                &ack));
    TEST_ASSERT_EQUAL_UINT8(BBT_PROTO_ERR_SIZE_MISMATCH, ack.status);

    fake_bbt_port_reset();
    len = build_start(0x02u, 300u, BBT_MAX_CHUNK_SIZE + 1u, 2u);
    process_packet(packet_buf, len);
    TEST_ASSERT_TRUE(bbt_packet_parse_start_ack(fake_ble_indicate_data,
                                                fake_ble_indicate_len,
                                                &ack));
    TEST_ASSERT_EQUAL_UINT8(BBT_PROTO_ERR_CHUNK_TOO_LARGE, ack.status);

    fake_bbt_port_reset();
    len = build_start(0x02u, 300u, 100u, 4u);
    process_packet(packet_buf, len);
    TEST_ASSERT_TRUE(bbt_packet_parse_start_ack(fake_ble_indicate_data,
                                                fake_ble_indicate_len,
                                                &ack));
    TEST_ASSERT_EQUAL_UINT8(BBT_PROTO_ERR_SIZE_MISMATCH, ack.status);

    fake_bbt_port_reset();
    len = build_start(0x02u, 300u, 100u, 0u);
    process_packet(packet_buf, len);
    TEST_ASSERT_TRUE(bbt_packet_parse_start_ack(fake_ble_indicate_data,
                                                fake_ble_indicate_len,
                                                &ack));
    TEST_ASSERT_EQUAL_UINT8(BBT_PROTO_ERR_TOO_MANY_CHUNKS, ack.status);

    fake_bbt_port_reset();
    len = build_start(0x02u, 0u, 100u, 1u);
    process_packet(packet_buf, len);
    TEST_ASSERT_TRUE(bbt_packet_parse_start_ack(fake_ble_indicate_data,
                                                fake_ble_indicate_len,
                                                &ack));
    TEST_ASSERT_EQUAL_UINT8(BBT_PROTO_ERR_SIZE_MISMATCH, ack.status);

    fake_bbt_port_reset();
    core_ctx.max_chunk_size = 64u;
    len = build_start(0x02u, 300u, 100u, 3u);
    process_packet(packet_buf, len);
    TEST_ASSERT_TRUE(bbt_packet_parse_start_ack(fake_ble_indicate_data,
                                                fake_ble_indicate_len,
                                                &ack));
    TEST_ASSERT_EQUAL_UINT8(BBT_PROTO_ERR_CHUNK_TOO_LARGE, ack.status);
    core_ctx.max_chunk_size = BBT_MAX_CHUNK_SIZE;
}

void test_start_should_reject_missing_or_failing_start_callback(void)
{
    uint16_t len = build_start(0x02u, 300u, 100u, 3u);
    app_vt.on_start = NULL;

    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(1u, core_ctx.receiver.storage_errors);

    bbt_start_ack_t ack;
    TEST_ASSERT_TRUE(bbt_packet_parse_start_ack(fake_ble_indicate_data,
                                                fake_ble_indicate_len,
                                                &ack));
    TEST_ASSERT_EQUAL_UINT8(BBT_PROTO_ERR_STORAGE, ack.status);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_receiver_init());
    app_vt.on_start = on_start_cb;
    on_start_result = BBT_ERR_STORAGE;
    fake_bbt_port_reset();

    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(1u, start_count);
    TEST_ASSERT_EQUAL_UINT32(1u, core_ctx.receiver.storage_errors);
}

void test_start_ack_notify_should_tolerate_build_and_notify_failures(void)
{
    uint16_t len = build_start(0x02u, 300u, 100u, 3u);

    bbt_core_get_notify_buffer_StopIgnore();
    bbt_core_get_notify_buffer_ExpectAndReturn(NULL);
    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_RECEIVING, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(0u, fake_ble_indicate_count);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_receiver_init());
    bbt_core_get_notify_buffer_IgnoreAndReturn(notify_buf);
    fake_ble_indicate_result = -1;

    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_RECEIVING, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(1u, fake_ble_indicate_count);
}

void test_chunk_should_store_valid_chunks_and_update_progress(void)
{
    start_receive(0x02u, 250u, 100u, 3u);

    uint16_t len = build_chunk(0x02u, 0u, 100u);
    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT32(1u, chunk_count);
    TEST_ASSERT_EQUAL_UINT32(1u, core_ctx.receiver.valid_chunks);
    TEST_ASSERT_EQUAL_UINT32(100u, core_ctx.receiver.bytes_written);
    TEST_ASSERT_EQUAL_UINT16(1u, core_ctx.receiver.high_watermark_next);
    TEST_ASSERT_TRUE((core_ctx.receiver.bitmap[0] & 0x01u) != 0u);

    len = build_chunk(0x02u, 2u, 50u);
    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT32(2u, chunk_count);
    TEST_ASSERT_EQUAL_UINT32(2u, core_ctx.receiver.valid_chunks);
    TEST_ASSERT_EQUAL_UINT32(150u, core_ctx.receiver.bytes_written);
    TEST_ASSERT_EQUAL_UINT16(3u, core_ctx.receiver.high_watermark_next);
}

void test_chunk_should_ignore_invalid_state_id_range_length_and_crc(void)
{
    uint16_t len = build_chunk(0x02u, 0u, 100u);
    process_packet(packet_buf, len);
    TEST_ASSERT_EQUAL_UINT32(0u, chunk_count);

    start_receive(0x02u, 150u, 100u, 2u);

    len = build_chunk(0x03u, 0u, 100u);
    process_packet(packet_buf, len);
    TEST_ASSERT_EQUAL_UINT32(0u, chunk_count);

    len = build_chunk(0x02u, 2u, 100u);
    process_packet(packet_buf, len);
    TEST_ASSERT_EQUAL_UINT32(0u, chunk_count);

    len = build_chunk(0x02u, 1u, 100u);
    process_packet(packet_buf, len);
    TEST_ASSERT_EQUAL_UINT32(0u, chunk_count);

    len = build_chunk(0x02u, 0u, 100u);
    packet_buf[BBT_PACKET_CHUNK_HEADER_SIZE] ^= 0x01u;
    process_packet(packet_buf, len);
    TEST_ASSERT_EQUAL_UINT32(0u, chunk_count);
    TEST_ASSERT_EQUAL_UINT32(1u, core_ctx.receiver.crc_errors);
}

void test_chunk_should_count_duplicate_without_calling_storage_again(void)
{
    start_receive(0x02u, 200u, 100u, 2u);

    uint16_t len = build_chunk(0x02u, 0u, 100u);
    process_packet(packet_buf, len);
    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT32(1u, chunk_count);
    TEST_ASSERT_EQUAL_UINT32(1u, core_ctx.receiver.duplicate_chunks);
}

void test_chunk_should_set_error_when_storage_callback_missing_or_fails(void)
{
    start_receive(0x02u, 200u, 100u, 2u);
    app_vt.on_chunk = NULL;

    uint16_t len = build_chunk(0x02u, 0u, 100u);
    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(1u, core_ctx.receiver.storage_errors);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, fake_ble_indicate_data[0]);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_receiver_init());
    start_receive(0x02u, 200u, 100u, 2u);
    app_vt.on_chunk = on_chunk_cb;
    on_chunk_result = BBT_ERR_STORAGE;
    fake_bbt_port_reset();

    len = build_chunk(0x02u, 0u, 100u);
    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(1u, core_ctx.receiver.storage_errors);
}

void test_end_should_complete_when_all_chunks_received(void)
{
    start_receive(0x02u, 200u, 100u, 2u);

    uint16_t len = build_chunk(0x02u, 0u, 100u);
    process_packet(packet_buf, len);
    len = build_chunk(0x02u, 1u, 100u);
    process_packet(packet_buf, len);

    fake_bbt_port_reset();
    bbt_core_emit_event_StopIgnore();
    bbt_core_emit_event_StubWithCallback(emit_event_complete_cb);
    len = build_end(0x02u);
    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_COMPLETE, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_COMPLETE, fake_ble_indicate_data[0]);
    TEST_ASSERT_EQUAL_UINT32(1u, complete_count);
    TEST_ASSERT_EQUAL_UINT16(0x02u, last_complete_id);
}

void test_end_should_request_repair_when_chunks_are_missing(void)
{
    start_receive(0x02u, 200u, 100u, 2u);

    uint16_t len = build_chunk(0x02u, 0u, 100u);
    process_packet(packet_buf, len);

    fake_bbt_port_reset();
    len = build_end(0x02u);
    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_WAITING_REPAIR, core_ctx.receiver.state);
    TEST_ASSERT_TRUE(fake_ble_indicate_count > 0u);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_NACK, fake_ble_indicate_data[0]);
}

void test_end_should_ignore_invalid_state_and_wrong_transfer(void)
{
    uint16_t len = build_end(0x02u);
    process_packet(packet_buf, len);
    TEST_ASSERT_EQUAL(BBT_STATE_IDLE, core_ctx.receiver.state);

    packet_buf[0] = BBT_OPCODE_END;
    process_packet(packet_buf, 1u);
    TEST_ASSERT_EQUAL(BBT_STATE_IDLE, core_ctx.receiver.state);

    start_receive(0x02u, 200u, 100u, 2u);
    len = build_end(0x03u);
    process_packet(packet_buf, len);
    TEST_ASSERT_EQUAL(BBT_STATE_RECEIVING, core_ctx.receiver.state);
}

void test_abort_packet_should_reset_context_call_app_and_send_error(void)
{
    start_receive(0x02u, 200u, 100u, 2u);

    fake_bbt_port_reset();
    uint16_t len = build_abort(0x02u);
    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_IDLE, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(1u, abort_count);
    TEST_ASSERT_EQUAL_UINT16(0x02u, last_abort_id);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, fake_ble_indicate_data[0]);
}

void test_abort_packet_should_ignore_invalid_or_wrong_transfer(void)
{
    start_receive(0x02u, 200u, 100u, 2u);

    packet_buf[0] = BBT_OPCODE_ABORT;
    process_packet(packet_buf, 1u);
    TEST_ASSERT_EQUAL(BBT_STATE_RECEIVING, core_ctx.receiver.state);

    uint16_t len = build_abort(0x03u);
    process_packet(packet_buf, len);
    TEST_ASSERT_EQUAL(BBT_STATE_RECEIVING, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(0u, abort_count);
}

void test_public_abort_should_reject_missing_queue_or_abort_active_receiver(void)
{
    core_ctx.worker.queue = NULL;
    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_STATE, bbt_receiver_abort());

    core_ctx.worker.queue = (bbt_os_queue_t)1;
    core_ctx.receiver.transfer_id = 0x02u;
    core_ctx.receiver.state = BBT_STATE_RECEIVING;

    TEST_ASSERT_EQUAL(BBT_OK, bbt_receiver_abort());
    TEST_ASSERT_EQUAL(BBT_STATE_IDLE, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, fake_ble_indicate_data[0]);
}

void test_public_abort_should_tolerate_null_notify_buffer(void)
{
    core_ctx.receiver.transfer_id = 0x02u;
    core_ctx.receiver.state = BBT_STATE_RECEIVING;
    bbt_core_get_notify_buffer_StopIgnore();
    bbt_core_get_notify_buffer_ExpectAndReturn(NULL);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_receiver_abort());
    TEST_ASSERT_EQUAL(BBT_STATE_IDLE, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(0u, fake_ble_indicate_count);
}

void test_chunk_error_should_tolerate_null_notify_buffer(void)
{
    start_receive(0x02u, 200u, 100u, 2u);
    app_vt.on_chunk = NULL;
    uint16_t len = build_chunk(0x02u, 0u, 100u);
    fake_bbt_port_reset();
    bbt_core_get_notify_buffer_StopIgnore();
    bbt_core_get_notify_buffer_ExpectAndReturn(NULL);

    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(0u, fake_ble_indicate_count);
}

void test_timeouts_should_ignore_inactive_and_short_elapsed(void)
{
    bbt_receiver_check_timeouts();

    start_receive(0x02u, 200u, 100u, 2u);
    fake_time_ms = BBT_IDLE_TIMEOUT_MS - 1u;
    bbt_receiver_check_timeouts();

    TEST_ASSERT_EQUAL_UINT8(0u, core_ctx.receiver.nack_retry_count);
}

void test_timeouts_should_send_nack_then_timeout_error_after_retries(void)
{
    start_receive(0x02u, 200u, 100u, 2u);

    for (uint8_t i = 1u; i <= BBT_NACK_RETRY_MAX; ++i)
    {
        fake_time_ms = (uint32_t)i * BBT_IDLE_TIMEOUT_MS;
        bbt_receiver_check_timeouts();
        TEST_ASSERT_EQUAL_UINT8(i, core_ctx.receiver.nack_retry_count);
        TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_NACK, fake_ble_indicate_data[0]);
    }

    fake_time_ms += BBT_IDLE_TIMEOUT_MS;
    bbt_receiver_check_timeouts();

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(1u, error_count);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_TIMEOUT, last_error_status);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, fake_ble_indicate_data[0]);
}

void test_timeouts_should_tolerate_nack_and_error_notify_failures(void)
{
    start_receive(0x02u, 200u, 100u, 2u);
    fake_time_ms = BBT_IDLE_TIMEOUT_MS;
    fake_ble_indicate_result = -1;

    bbt_receiver_check_timeouts();

    TEST_ASSERT_EQUAL(BBT_STATE_RECEIVING, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT8(1u, core_ctx.receiver.nack_retry_count);

    core_ctx.receiver.nack_retry_count = BBT_NACK_RETRY_MAX;
    fake_time_ms += BBT_IDLE_TIMEOUT_MS;
    bbt_receiver_check_timeouts();

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_TIMEOUT, last_error_status);
}

void test_timeout_should_complete_when_all_chunks_are_received(void)
{
    start_receive(0x02u, 200u, 100u, 2u);

    uint16_t len = build_chunk(0x02u, 0u, 100u);
    process_packet(packet_buf, len);
    len = build_chunk(0x02u, 1u, 100u);
    process_packet(packet_buf, len);

    fake_bbt_port_reset();
    fake_time_ms = BBT_IDLE_TIMEOUT_MS;
    bbt_receiver_check_timeouts();

    TEST_ASSERT_EQUAL(BBT_STATE_COMPLETE, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_COMPLETE, fake_ble_indicate_data[0]);
}

void test_complete_notify_should_tolerate_build_and_notify_failures(void)
{
    start_receive(0x02u, 200u, 100u, 2u);

    uint16_t len = build_chunk(0x02u, 0u, 100u);
    process_packet(packet_buf, len);
    len = build_chunk(0x02u, 1u, 100u);
    process_packet(packet_buf, len);

    bbt_core_get_notify_buffer_StopIgnore();
    bbt_core_get_notify_buffer_ExpectAndReturn(NULL);
    fake_bbt_port_reset();
    len = build_end(0x02u);
    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_COMPLETE, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(0u, fake_ble_indicate_count);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_receiver_init());
    bbt_core_get_notify_buffer_IgnoreAndReturn(notify_buf);
    start_receive(0x02u, 200u, 100u, 2u);
    len = build_chunk(0x02u, 0u, 100u);
    process_packet(packet_buf, len);
    len = build_chunk(0x02u, 1u, 100u);
    process_packet(packet_buf, len);
    fake_bbt_port_reset();
    fake_ble_indicate_result = -1;
    len = build_end(0x02u);
    process_packet(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_COMPLETE, core_ctx.receiver.state);
    TEST_ASSERT_EQUAL_UINT32(1u, fake_ble_indicate_count);
}
