#include "unity.h"

#include "bbt_packet.h"
#include "bbt_sender.h"

#include "mock_bbt_core.h"

#include <stdint.h>
#include <string.h>

extern void fake_bbt_port_reset(void);
extern uint32_t fake_time_ms;
extern int fake_ble_notify_result;
extern uint32_t fake_ble_notify_count;
extern int fake_ble_indicate_result;
extern uint8_t fake_ble_indicate_data[BBT_MAX_NOTIFY_SIZE];
extern uint16_t fake_ble_indicate_len;
extern uint32_t fake_ble_indicate_count;

static bbt_core_ctx_t core_ctx;
static uint8_t notify_buf[BBT_MAX_NOTIFY_SIZE];
static uint8_t packet_buf[BBT_MAX_NOTIFY_SIZE];
static uint8_t export_data[512];
static uint32_t prepare_count;
static uint32_t read_count;
static uint32_t complete_count;
static uint32_t error_count;
static uint8_t last_error_transfer_id;
static bbt_proto_status_t last_error_status;
static bbt_result_t prepare_result;
static bbt_result_t read_result;
static bbt_result_t mode_get_result;
static bbt_export_desc_t prepared_desc;
static bbt_device_to_app_vtable_t export_vt;
static bbt_mode_entry_t mode_entry;
static void *const app_ctx = (void *)0x12345678u;

static bbt_result_t prepare_export_cb(const bbt_pull_req_t *req,
                                      bbt_export_desc_t *desc,
                                      void *ctx)
{
    TEST_ASSERT_NOT_NULL(req);
    TEST_ASSERT_EQUAL_PTR(app_ctx, ctx);
    prepare_count++;

    if (prepare_result == BBT_OK)
    {
        *desc = prepared_desc;
    }

    return prepare_result;
}

static bbt_result_t read_export_cb(uint32_t offset,
                                   uint8_t *data,
                                   uint16_t len,
                                   uint16_t chunk_size,
                                   void *ctx)
{
    TEST_ASSERT_EQUAL_PTR(app_ctx, ctx);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_TRUE(offset + len <= sizeof(export_data));
    TEST_ASSERT_TRUE(chunk_size > 0u);

    read_count++;
    if (read_result == BBT_OK)
    {
        memcpy(data, &export_data[offset], len);
    }

    return read_result;
}

static bbt_result_t read_no_write_cb(uint32_t offset,
                                     uint8_t *data,
                                     uint16_t len,
                                     uint16_t chunk_size,
                                     void *ctx)
{
    (void)offset;
    (void)data;
    (void)len;
    (void)chunk_size;
    TEST_ASSERT_EQUAL_PTR(app_ctx, ctx);
    read_count++;
    return BBT_OK;
}

static bbt_result_t complete_cb(void *ctx)
{
    TEST_ASSERT_EQUAL_PTR(app_ctx, ctx);
    complete_count++;
    return BBT_OK;
}

static bbt_result_t error_cb(uint8_t transfer_id,
                             bbt_proto_status_t status,
                             void *ctx)
{
    TEST_ASSERT_EQUAL_PTR(app_ctx, ctx);
    error_count++;
    last_error_transfer_id = transfer_id;
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
        TEST_ASSERT_EQUAL_UINT8(2u, index);
        TEST_ASSERT_NOT_NULL(entry);
        *entry = mode_entry;
    }

    return mode_get_result;
}

static void expect_core_context_any(void)
{
    bbt_core_get_context_IgnoreAndReturn(&core_ctx);
}

static void expect_notify_buffer_once(void)
{
    /* Notify buffer access is stubbed globally in setUp(). */
}

static uint16_t build_pull_req(uint8_t transfer_id, uint16_t max_chunk_size)
{
    bbt_pull_req_t req = {
        .transfer_id = transfer_id,
        .max_chunk_size = max_chunk_size,
    };

    return bbt_packet_build_pull_req(packet_buf, sizeof(packet_buf), &req);
}

static uint16_t build_start_ack(uint8_t transfer_id,
                                bbt_proto_status_t status,
                                uint16_t accepted_chunk_size)
{
    bbt_start_ack_t ack = {
        .transfer_id = transfer_id,
        .status = (uint8_t)status,
        .accepted_chunk_size = accepted_chunk_size,
    };

    return bbt_packet_build_start_ack(packet_buf, sizeof(packet_buf), &ack);
}

static uint16_t build_complete(uint8_t transfer_id)
{
    bbt_packet_id_t id = {
        .transfer_id = transfer_id,
    };

    return bbt_packet_build_complete(packet_buf, sizeof(packet_buf), &id);
}

static uint16_t build_error(uint8_t transfer_id, bbt_proto_status_t status)
{
    bbt_error_packet_t error = {
        .transfer_id = transfer_id,
        .error = (uint8_t)status,
    };

    return bbt_packet_build_error(packet_buf, sizeof(packet_buf), &error);
}

static uint16_t build_nack(uint8_t transfer_id, uint8_t flags,
                           const uint8_t *bitmap, uint16_t bitmap_len)
{
    bbt_nack_header_t header = {
        .transfer_id = transfer_id,
        .flags = flags,
        .bitmap_len = bitmap_len,
    };

    return bbt_packet_build_nack(packet_buf, sizeof(packet_buf), &header, bitmap);
}

static void configure_export(uint32_t total_size, uint16_t chunk_hint)
{
    prepared_desc.read_fn = read_export_cb;
    prepared_desc.total_size = total_size;
    prepared_desc.chunk_size_hint = chunk_hint;
    prepared_desc.label = "test-export";
    prepared_desc.ctx = app_ctx;
}

static void start_export(uint8_t transfer_id, uint16_t requested_chunk)
{
    uint16_t len = build_pull_req(transfer_id, requested_chunk);

    bbt_core_can_start_sender_ExpectAndReturn(true);
    mode_get_result = BBT_OK;
    bbt_mode_get_entry_StubWithCallback(mode_get_entry_cb);
    expect_notify_buffer_once();

    bbt_sender_handle_pull_req(packet_buf, len);
}

void setUp(void)
{
    memset(&core_ctx, 0, sizeof(core_ctx));
    memset(notify_buf, 0, sizeof(notify_buf));
    memset(packet_buf, 0, sizeof(packet_buf));
    memset(export_data, 0, sizeof(export_data));
    for (uint16_t i = 0u; i < sizeof(export_data); ++i)
    {
        export_data[i] = (uint8_t)(i & 0xFFu);
    }

    fake_bbt_port_reset();
    prepare_count = 0u;
    read_count = 0u;
    complete_count = 0u;
    error_count = 0u;
    last_error_transfer_id = 0u;
    last_error_status = BBT_PROTO_STATUS_OK;
    prepare_result = BBT_OK;
    read_result = BBT_OK;
    mode_get_result = BBT_OK;
    core_ctx.worker.mutex = (bbt_os_mutex_t)1;

    export_vt.prepare_export = prepare_export_cb;
    export_vt.on_export_complete = complete_cb;
    export_vt.on_error = error_cb;

    mode_entry.app_to_device = NULL;
    mode_entry.device_to_app = &export_vt;
    mode_entry.ctx = app_ctx;

    configure_export(300u, 128u);

    expect_core_context_any();
    bbt_core_get_notify_buffer_IgnoreAndReturn(notify_buf);
    bbt_core_emit_event_Ignore();
    TEST_ASSERT_EQUAL(BBT_OK, bbt_sender_init());
}

void tearDown(void)
{
    bbt_sender_deinit();
}

void test_init_should_reset_sender_context_and_max_chunk_size(void)
{
    TEST_ASSERT_FALSE(core_ctx.sender.active);
    TEST_ASSERT_EQUAL(BBT_STATE_IDLE, core_ctx.sender.state);
    TEST_ASSERT_EQUAL_UINT16(BBT_MAX_CHUNK_SIZE, core_ctx.max_chunk_size);
}

void test_get_status_should_reject_null_and_idle_sender(void)
{
    bbt_status_t status;

    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_ARG, bbt_sender_get_status(NULL));
    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_STATE, bbt_sender_get_status(&status));
}

void test_set_max_chunk_size_should_accept_zero_clamp_and_smaller_values(void)
{
    bbt_sender_set_max_chunk_size(0u);
    TEST_ASSERT_EQUAL_UINT16(0u, core_ctx.max_chunk_size);

    bbt_sender_set_max_chunk_size(BBT_MAX_CHUNK_SIZE + 1u);
    TEST_ASSERT_EQUAL_UINT16(BBT_MAX_CHUNK_SIZE, core_ctx.max_chunk_size);

    bbt_sender_set_max_chunk_size(64u);
    TEST_ASSERT_EQUAL_UINT16(64u, core_ctx.max_chunk_size);
}

void test_sender_is_active_should_reflect_context(void)
{
    TEST_ASSERT_FALSE(bbt_sender_is_active());

    core_ctx.sender.active = true;

    TEST_ASSERT_TRUE(bbt_sender_is_active());
}

void test_handle_pull_req_should_clamp_chunk_hint_to_config_max(void)
{
    configure_export(300u, BBT_MAX_CHUNK_SIZE + 50u);

    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 0u);

    TEST_ASSERT_EQUAL_UINT16(BBT_MAX_CHUNK_SIZE, core_ctx.sender.chunk_size);
}

void test_handle_pull_req_should_keep_active_when_start_packet_build_fails(void)
{
    uint16_t len = build_pull_req(BBT_TRANSFER_ID_DIR_BIT | 2u, 128u);

    bbt_core_can_start_sender_ExpectAndReturn(true);
    mode_get_result = BBT_OK;
    bbt_mode_get_entry_StubWithCallback(mode_get_entry_cb);
    bbt_core_get_notify_buffer_StopIgnore();
    bbt_core_get_notify_buffer_ExpectAndReturn(NULL);

    bbt_sender_handle_pull_req(packet_buf, len);

    TEST_ASSERT_TRUE(core_ctx.sender.active);
    TEST_ASSERT_EQUAL(BBT_STATE_WAITING_START_ACK, core_ctx.sender.state);
}

void test_send_error_paths_should_tolerate_build_and_notify_failures(void)
{
    uint16_t len = build_pull_req(2u, 128u);

    bbt_core_get_notify_buffer_StopIgnore();
    bbt_core_get_notify_buffer_ExpectAndReturn(NULL);
    bbt_sender_handle_pull_req(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT32(0u, fake_ble_notify_count);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_sender_init());
    len = build_pull_req(2u, 128u);
    bbt_core_get_notify_buffer_IgnoreAndReturn(notify_buf);
    fake_ble_indicate_result = -1;

    bbt_sender_handle_pull_req(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT32(1u, fake_ble_indicate_count);
}

void test_handle_pull_req_should_start_export_and_send_start_packet(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);

    TEST_ASSERT_TRUE(core_ctx.sender.active);
    TEST_ASSERT_EQUAL(BBT_STATE_WAITING_START_ACK, core_ctx.sender.state);
    TEST_ASSERT_EQUAL_UINT8(BBT_TRANSFER_ID_DIR_BIT | 2u, core_ctx.sender.transfer_id);
    TEST_ASSERT_EQUAL_UINT16(100u, core_ctx.sender.chunk_size);
    TEST_ASSERT_EQUAL_UINT16(3u, core_ctx.sender.total_chunks);
    TEST_ASSERT_EQUAL_UINT32(1u, prepare_count);
    TEST_ASSERT_EQUAL_UINT32(0u, fake_ble_notify_count);
    TEST_ASSERT_EQUAL_UINT32(1u, fake_ble_indicate_count);

    bbt_start_meta_t start;
    TEST_ASSERT_TRUE(bbt_packet_parse_start(fake_ble_indicate_data,
                                            fake_ble_indicate_len,
                                            &start));
    TEST_ASSERT_EQUAL_UINT32(300u, start.total_size);
    TEST_ASSERT_EQUAL_UINT16(100u, start.chunk_size);
    TEST_ASSERT_EQUAL_UINT16(3u, start.total_chunks);
}

void test_handle_pull_req_should_use_max_chunk_when_hint_is_zero(void)
{
    configure_export(244u, 0u);

    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 0u);

    TEST_ASSERT_EQUAL_UINT16(BBT_MAX_CHUNK_SIZE, core_ctx.sender.chunk_size);
    TEST_ASSERT_EQUAL_UINT16(1u, core_ctx.sender.total_chunks);
}

void test_handle_pull_req_should_ignore_invalid_packet(void)
{
    packet_buf[0] = BBT_OPCODE_START;

    bbt_sender_handle_pull_req(packet_buf, 1u);

    TEST_ASSERT_FALSE(core_ctx.sender.active);
    TEST_ASSERT_EQUAL_UINT32(0u, fake_ble_notify_count);
}

void test_handle_pull_req_should_reject_missing_direction_bit(void)
{
    uint16_t len = build_pull_req(2u, 128u);

    expect_notify_buffer_once();

    bbt_sender_handle_pull_req(packet_buf, len);

    TEST_ASSERT_FALSE(core_ctx.sender.active);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, fake_ble_indicate_data[0]);
}

void test_handle_pull_req_should_reject_when_core_cannot_start(void)
{
    uint16_t len = build_pull_req(BBT_TRANSFER_ID_DIR_BIT | 2u, 128u);

    bbt_core_can_start_sender_ExpectAndReturn(false);
    expect_notify_buffer_once();

    bbt_sender_handle_pull_req(packet_buf, len);

    TEST_ASSERT_FALSE(core_ctx.sender.active);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, fake_ble_indicate_data[0]);
}

void test_handle_pull_req_should_reject_when_already_active(void)
{
    core_ctx.sender.active = true;
    uint16_t len = build_pull_req(BBT_TRANSFER_ID_DIR_BIT | 2u, 128u);

    bbt_core_can_start_sender_ExpectAndReturn(true);
    expect_notify_buffer_once();

    bbt_sender_handle_pull_req(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, fake_ble_indicate_data[0]);
}

void test_handle_pull_req_should_reject_mode_lookup_failure(void)
{
    uint16_t len = build_pull_req(BBT_TRANSFER_ID_DIR_BIT | 2u, 128u);

    bbt_core_can_start_sender_ExpectAndReturn(true);
    mode_get_result = BBT_ERR_INVALID_ARG;
    bbt_mode_get_entry_StubWithCallback(mode_get_entry_cb);
    expect_notify_buffer_once();

    bbt_sender_handle_pull_req(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, fake_ble_indicate_data[0]);
}

void test_handle_pull_req_should_reject_missing_export_handler(void)
{
    uint16_t len = build_pull_req(BBT_TRANSFER_ID_DIR_BIT | 2u, 128u);
    mode_entry.device_to_app = NULL;

    bbt_core_can_start_sender_ExpectAndReturn(true);
    mode_get_result = BBT_OK;
    bbt_mode_get_entry_StubWithCallback(mode_get_entry_cb);
    expect_notify_buffer_once();

    bbt_sender_handle_pull_req(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, fake_ble_indicate_data[0]);
}

void test_handle_pull_req_should_report_prepare_failure(void)
{
    uint16_t len = build_pull_req(BBT_TRANSFER_ID_DIR_BIT | 2u, 128u);
    prepare_result = BBT_ERR_STORAGE;

    bbt_core_can_start_sender_ExpectAndReturn(true);
    mode_get_result = BBT_OK;
    bbt_mode_get_entry_StubWithCallback(mode_get_entry_cb);
    expect_notify_buffer_once();

    bbt_sender_handle_pull_req(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT32(1u, error_count);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_STORAGE, last_error_status);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, fake_ble_indicate_data[0]);
}

void test_handle_pull_req_should_reject_missing_read_function(void)
{
    uint16_t len = build_pull_req(BBT_TRANSFER_ID_DIR_BIT | 2u, 128u);
    prepared_desc.read_fn = NULL;

    bbt_core_can_start_sender_ExpectAndReturn(true);
    mode_get_result = BBT_OK;
    bbt_mode_get_entry_StubWithCallback(mode_get_entry_cb);
    expect_notify_buffer_once();

    bbt_sender_handle_pull_req(packet_buf, len);

    TEST_ASSERT_FALSE(core_ctx.sender.active);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, fake_ble_indicate_data[0]);
}

void test_start_ack_should_transition_to_exporting(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);

    uint16_t len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                                   BBT_PROTO_STATUS_OK,
                                   50u);

    bbt_sender_handle_start_ack(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_EXPORTING, core_ctx.sender.state);
    TEST_ASSERT_EQUAL_UINT16(50u, core_ctx.sender.chunk_size);
    TEST_ASSERT_EQUAL_UINT16(6u, core_ctx.sender.total_chunks);
}

void test_start_ack_should_ignore_invalid_or_unexpected_packets(void)
{
    packet_buf[0] = BBT_OPCODE_START;
    bbt_sender_handle_start_ack(packet_buf, 1u);

    uint16_t len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 9u,
                                   BBT_PROTO_STATUS_OK,
                                   100u);
    bbt_sender_handle_start_ack(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_IDLE, core_ctx.sender.state);
}

void test_start_ack_should_set_error_when_rejected_or_chunk_size_invalid(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);

    uint16_t len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                                   BBT_PROTO_ERR_INVALID_STATE,
                                   100u);

    expect_notify_buffer_once();

    bbt_sender_handle_start_ack(packet_buf, len);

    TEST_ASSERT_FALSE(core_ctx.sender.active);
    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_INVALID_STATE, last_error_status);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_sender_init());
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);

    len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                          BBT_PROTO_STATUS_OK,
                          0u);

    expect_notify_buffer_once();

    bbt_sender_handle_start_ack(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_CHUNK_TOO_LARGE, last_error_status);
}

void test_poll_should_send_all_chunks_and_end_packet(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    uint16_t len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                                   BBT_PROTO_STATUS_OK,
                                   100u);
    bbt_sender_handle_start_ack(packet_buf, len);

    bbt_sender_poll();

    TEST_ASSERT_EQUAL_UINT32(3u, read_count);
    TEST_ASSERT_EQUAL_UINT16(3u, core_ctx.sender.next_seq);
    TEST_ASSERT_EQUAL(BBT_STATE_WAITING_EXPORT_REPAIR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_END, fake_ble_indicate_data[0]);
    TEST_ASSERT_EQUAL_UINT32(3u * BBT_SENDER_CHUNK_DELAY_MS, fake_time_ms);
}

void test_poll_should_do_nothing_when_not_exporting(void)
{
    bbt_sender_poll();

    TEST_ASSERT_EQUAL_UINT32(0u, read_count);
    TEST_ASSERT_EQUAL_UINT32(0u, fake_ble_notify_count);
}

void test_poll_should_set_error_when_read_fails(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    uint16_t len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                                   BBT_PROTO_STATUS_OK,
                                   100u);
    bbt_sender_handle_start_ack(packet_buf, len);
    read_result = BBT_ERR_STORAGE;

    expect_notify_buffer_once();

    bbt_sender_poll();

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_STORAGE, last_error_status);
}

void test_poll_should_set_error_when_end_notify_fails(void)
{
    core_ctx.sender.active = true;
    core_ctx.sender.state = BBT_STATE_EXPORTING;
    core_ctx.sender.transfer_id = BBT_TRANSFER_ID_DIR_BIT | 2u;
    core_ctx.sender.vt = &export_vt;
    core_ctx.sender.vt_ctx = app_ctx;
    core_ctx.sender.total_chunks = 0u;
    core_ctx.sender.next_seq = 0u;
    fake_ble_indicate_result = -1;

    bbt_sender_poll();

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_STORAGE, last_error_status);
}

void test_poll_should_set_error_when_end_packet_build_fails(void)
{
    core_ctx.sender.active = true;
    core_ctx.sender.state = BBT_STATE_EXPORTING;
    core_ctx.sender.transfer_id = BBT_TRANSFER_ID_DIR_BIT | 2u;
    core_ctx.sender.vt = &export_vt;
    core_ctx.sender.vt_ctx = app_ctx;
    core_ctx.sender.total_chunks = 0u;
    core_ctx.sender.next_seq = 0u;
    bbt_core_get_notify_buffer_StopIgnore();
    bbt_core_get_notify_buffer_IgnoreAndReturn(NULL);

    bbt_sender_poll();

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_STORAGE, last_error_status);
}

void test_poll_should_set_error_when_chunk_notify_fails(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    uint16_t len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                                   BBT_PROTO_STATUS_OK,
                                   100u);
    bbt_sender_handle_start_ack(packet_buf, len);
    fake_ble_notify_result = -1;

    bbt_sender_poll();

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_STORAGE, last_error_status);
}

void test_nack_should_resend_missing_chunks_and_end(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    uint16_t len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                                   BBT_PROTO_STATUS_OK,
                                   100u);
    bbt_sender_handle_start_ack(packet_buf, len);
    bbt_sender_poll();
    read_count = 0u;

    const uint8_t bitmap[] = {0x05u};
    len = build_nack(BBT_TRANSFER_ID_DIR_BIT | 2u, 0u, bitmap, sizeof(bitmap));

    bbt_sender_handle_nack(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT32(1u, read_count);
    TEST_ASSERT_EQUAL(BBT_STATE_WAITING_EXPORT_REPAIR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL_UINT16(0u, core_ctx.sender.nack_bitmap_pos);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_END, fake_ble_indicate_data[0]);
}

void test_nack_should_accumulate_segmented_bitmap(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    uint16_t len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                                   BBT_PROTO_STATUS_OK,
                                   100u);
    bbt_sender_handle_start_ack(packet_buf, len);
    bbt_sender_poll();
    read_count = 0u;

    const uint8_t first[] = {0xFFu};
    len = build_nack(BBT_TRANSFER_ID_DIR_BIT | 2u, 1u, first, sizeof(first));
    bbt_sender_handle_nack(packet_buf, len);
    TEST_ASSERT_EQUAL_UINT16(1u, core_ctx.sender.nack_bitmap_pos);
    TEST_ASSERT_EQUAL_UINT32(0u, read_count);

    const uint8_t second[] = {0xFFu};
    len = build_nack(BBT_TRANSFER_ID_DIR_BIT | 2u, 0u, second, sizeof(second));
    bbt_sender_handle_nack(packet_buf, len);
    TEST_ASSERT_EQUAL_UINT16(0u, core_ctx.sender.nack_bitmap_pos);
}

void test_nack_should_ignore_invalid_unexpected_and_empty_packets(void)
{
    packet_buf[0] = BBT_OPCODE_START;
    bbt_sender_handle_nack(packet_buf, 1u);

    const uint8_t bitmap[] = {0x00u};
    uint16_t len = build_nack(BBT_TRANSFER_ID_DIR_BIT | 9u, 0u, bitmap, sizeof(bitmap));
    bbt_sender_handle_nack(packet_buf, len);

    core_ctx.sender.active = true;
    core_ctx.sender.state = BBT_STATE_EXPORTING;
    core_ctx.sender.transfer_id = BBT_TRANSFER_ID_DIR_BIT | 2u;
    bbt_nack_header_t empty = {
        .transfer_id = BBT_TRANSFER_ID_DIR_BIT | 2u,
        .flags = 0u,
        .bitmap_len = 0u,
    };
    len = bbt_packet_build_nack(packet_buf, sizeof(packet_buf), &empty, bitmap);
    bbt_sender_handle_nack(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT16(0u, core_ctx.sender.nack_bitmap_pos);
}

void test_nack_should_set_error_when_bitmap_overflows_or_resend_fails(void)
{
    core_ctx.sender.active = true;
    core_ctx.sender.state = BBT_STATE_EXPORTING;
    core_ctx.sender.transfer_id = BBT_TRANSFER_ID_DIR_BIT | 2u;
    core_ctx.sender.vt = &export_vt;
    core_ctx.sender.vt_ctx = app_ctx;
    core_ctx.sender.nack_bitmap_pos = BBT_BITMAP_SIZE_BYTES;

    const uint8_t bitmap[] = {0x00u};
    uint16_t len = build_nack(BBT_TRANSFER_ID_DIR_BIT | 2u, 0u, bitmap, sizeof(bitmap));

    expect_notify_buffer_once();

    bbt_sender_handle_nack(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_INVALID_PACKET, last_error_status);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_sender_init());
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                          BBT_PROTO_STATUS_OK,
                          100u);
    bbt_sender_handle_start_ack(packet_buf, len);
    read_result = BBT_ERR_STORAGE;
    len = build_nack(BBT_TRANSFER_ID_DIR_BIT | 2u, 0u, bitmap, sizeof(bitmap));

    expect_notify_buffer_once();

    bbt_sender_handle_nack(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_STORAGE, last_error_status);
}

void test_nack_should_set_error_when_end_resend_fails(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    uint16_t len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                                   BBT_PROTO_STATUS_OK,
                                   100u);
    bbt_sender_handle_start_ack(packet_buf, len);
    bbt_sender_poll();
    const uint8_t bitmap[] = {0xFFu};
    len = build_nack(BBT_TRANSFER_ID_DIR_BIT | 2u, 0u, bitmap, sizeof(bitmap));
    fake_ble_indicate_result = -1;

    bbt_sender_handle_nack(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_STORAGE, last_error_status);
}

void test_nack_should_set_error_when_export_chunk_is_invalid_state_or_too_large(void)
{
    core_ctx.sender.active = true;
    core_ctx.sender.state = BBT_STATE_EXPORTING;
    core_ctx.sender.transfer_id = BBT_TRANSFER_ID_DIR_BIT | 2u;
    core_ctx.sender.total_chunks = 1u;
    core_ctx.sender.vt = &export_vt;
    core_ctx.sender.vt_ctx = app_ctx;
    core_ctx.sender.desc.read_fn = NULL;

    const uint8_t bitmap[] = {0x00u};
    uint16_t len = build_nack(BBT_TRANSFER_ID_DIR_BIT | 2u, 0u, bitmap, sizeof(bitmap));

    bbt_sender_handle_nack(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_STORAGE, last_error_status);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_sender_init());
    core_ctx.sender.active = true;
    core_ctx.sender.state = BBT_STATE_EXPORTING;
    core_ctx.sender.transfer_id = BBT_TRANSFER_ID_DIR_BIT | 2u;
    core_ctx.sender.total_chunks = 1u;
    core_ctx.sender.chunk_size = BBT_MAX_CHUNK_SIZE + 1u;
    core_ctx.sender.desc.total_size = BBT_MAX_CHUNK_SIZE + 1u;
    core_ctx.sender.desc.read_fn = read_export_cb;
    core_ctx.sender.desc.ctx = app_ctx;
    core_ctx.sender.vt = &export_vt;
    core_ctx.sender.vt_ctx = app_ctx;
    len = build_nack(BBT_TRANSFER_ID_DIR_BIT | 2u, 0u, bitmap, sizeof(bitmap));

    bbt_sender_handle_nack(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_STORAGE, last_error_status);
}

void test_nack_should_set_error_when_chunk_packet_build_fails(void)
{
    core_ctx.sender.active = true;
    core_ctx.sender.state = BBT_STATE_EXPORTING;
    core_ctx.sender.transfer_id = BBT_TRANSFER_ID_DIR_BIT | 2u;
    core_ctx.sender.total_chunks = 1u;
    core_ctx.sender.chunk_size = 10u;
    core_ctx.sender.desc.total_size = 10u;
    core_ctx.sender.desc.read_fn = read_no_write_cb;
    core_ctx.sender.desc.ctx = app_ctx;
    core_ctx.sender.vt = &export_vt;
    core_ctx.sender.vt_ctx = app_ctx;
    const uint8_t bitmap[] = {0x00u};
    uint16_t len = build_nack(BBT_TRANSFER_ID_DIR_BIT | 2u, 0u, bitmap, sizeof(bitmap));
    bbt_core_get_notify_buffer_StopIgnore();
    bbt_core_get_notify_buffer_IgnoreAndReturn(NULL);

    bbt_sender_handle_nack(packet_buf, len);

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_STORAGE, last_error_status);
}

void test_complete_response_should_finish_active_export(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);

    uint16_t len = build_complete(BBT_TRANSFER_ID_DIR_BIT | 2u);
    bbt_sender_handle_complete_response(packet_buf, len);

    TEST_ASSERT_FALSE(core_ctx.sender.active);
    TEST_ASSERT_EQUAL(BBT_STATE_COMPLETE, core_ctx.sender.state);
    TEST_ASSERT_EQUAL_UINT32(1u, complete_count);
}

void test_complete_response_should_finish_without_optional_callback(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    export_vt.on_export_complete = NULL;

    uint16_t len = build_complete(BBT_TRANSFER_ID_DIR_BIT | 2u);
    bbt_sender_handle_complete_response(packet_buf, len);

    TEST_ASSERT_FALSE(core_ctx.sender.active);
    TEST_ASSERT_EQUAL(BBT_STATE_COMPLETE, core_ctx.sender.state);
    TEST_ASSERT_EQUAL_UINT32(0u, complete_count);
}

void test_complete_response_should_ignore_invalid_or_unexpected_packet(void)
{
    packet_buf[0] = BBT_OPCODE_START;
    bbt_sender_handle_complete_response(packet_buf, 1u);

    uint16_t len = build_complete(BBT_TRANSFER_ID_DIR_BIT | 9u);
    bbt_sender_handle_complete_response(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT32(0u, complete_count);
}

void test_error_response_should_set_error_for_active_export(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);

    uint16_t len = build_error(BBT_TRANSFER_ID_DIR_BIT | 2u,
                               BBT_PROTO_ERR_TIMEOUT);

    expect_notify_buffer_once();

    bbt_sender_handle_error_response(packet_buf, len);

    TEST_ASSERT_FALSE(core_ctx.sender.active);
    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_TIMEOUT, last_error_status);
}

void test_error_response_should_ignore_invalid_or_unexpected_packet(void)
{
    packet_buf[0] = BBT_OPCODE_START;
    bbt_sender_handle_error_response(packet_buf, 1u);

    uint16_t len = build_error(BBT_TRANSFER_ID_DIR_BIT | 9u,
                               BBT_PROTO_ERR_TIMEOUT);
    bbt_sender_handle_error_response(packet_buf, len);

    TEST_ASSERT_EQUAL_UINT32(0u, error_count);
}

void test_abort_should_reject_idle_and_abort_active_export(void)
{
    TEST_ASSERT_EQUAL(BBT_ERR_INVALID_STATE, bbt_sender_abort());

    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);

    expect_notify_buffer_once();

    TEST_ASSERT_EQUAL(BBT_OK, bbt_sender_abort());
    TEST_ASSERT_FALSE(core_ctx.sender.active);
    TEST_ASSERT_EQUAL_UINT32(1u, error_count);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_ABORTED, last_error_status);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_ERROR, fake_ble_indicate_data[0]);
}

void test_get_status_should_report_active_export_progress(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    uint16_t len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                                   BBT_PROTO_STATUS_OK,
                                   100u);
    bbt_sender_handle_start_ack(packet_buf, len);
    bbt_sender_poll();

    bbt_status_t status;
    TEST_ASSERT_EQUAL(BBT_OK, bbt_sender_get_status(&status));
    TEST_ASSERT_EQUAL(BBT_STATE_WAITING_EXPORT_REPAIR, status.state);
    TEST_ASSERT_EQUAL_UINT32(300u, status.total_size);
    TEST_ASSERT_EQUAL_UINT16(100u, status.chunk_size);
    TEST_ASSERT_EQUAL_UINT16(3u, status.total_chunks);
    TEST_ASSERT_EQUAL_UINT32(3u, status.valid_chunks);
    TEST_ASSERT_EQUAL_UINT32(300u, status.bytes_written);
}

void test_get_status_should_clamp_bytes_written_to_total_size(void)
{
    core_ctx.sender.active = true;
    core_ctx.sender.state = BBT_STATE_EXPORTING;
    core_ctx.sender.transfer_id = BBT_TRANSFER_ID_DIR_BIT | 2u;
    core_ctx.sender.desc.total_size = 250u;
    core_ctx.sender.chunk_size = 100u;
    core_ctx.sender.total_chunks = 3u;
    core_ctx.sender.next_seq = 3u;

    bbt_status_t status;

    TEST_ASSERT_EQUAL(BBT_OK, bbt_sender_get_status(&status));
    TEST_ASSERT_EQUAL_UINT32(250u, status.bytes_written);
}

void test_check_timeouts_should_ignore_inactive_and_short_elapsed(void)
{
    bbt_sender_check_timeouts();

    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    fake_time_ms = BBT_IDLE_TIMEOUT_MS - 1u;

    bbt_sender_check_timeouts();

    TEST_ASSERT_EQUAL_UINT8(0u, core_ctx.sender.nack_retry_count);
}

void test_check_timeouts_should_resend_start_then_error_after_max_retries(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);

    for (uint8_t i = 1u; i <= BBT_NACK_RETRY_MAX; ++i)
    {
        fake_time_ms = (uint32_t)i * BBT_IDLE_TIMEOUT_MS;
        expect_notify_buffer_once();
        bbt_sender_check_timeouts();
        TEST_ASSERT_EQUAL_UINT8(i, core_ctx.sender.nack_retry_count);
    }

    fake_time_ms += BBT_IDLE_TIMEOUT_MS;
    expect_notify_buffer_once();

    bbt_sender_check_timeouts();

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_TIMEOUT, last_error_status);
}

void test_check_timeouts_should_resend_end_while_waiting_for_repair(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    uint16_t len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                                   BBT_PROTO_STATUS_OK,
                                   100u);
    bbt_sender_handle_start_ack(packet_buf, len);
    bbt_sender_poll();

    fake_time_ms = core_ctx.sender.last_notify_time_ms + BBT_IDLE_TIMEOUT_MS;
    expect_notify_buffer_once();

    bbt_sender_check_timeouts();

    TEST_ASSERT_EQUAL_UINT8(1u, core_ctx.sender.nack_retry_count);
    TEST_ASSERT_EQUAL_UINT8(BBT_OPCODE_END, fake_ble_indicate_data[0]);
}

void test_check_timeouts_should_set_error_when_start_or_end_resend_fails(void)
{
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    fake_time_ms = BBT_IDLE_TIMEOUT_MS;
    fake_ble_indicate_result = -1;

    bbt_sender_check_timeouts();

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_UNKNOWN, last_error_status);

    TEST_ASSERT_EQUAL(BBT_OK, bbt_sender_init());
    start_export(BBT_TRANSFER_ID_DIR_BIT | 2u, 100u);
    uint16_t len = build_start_ack(BBT_TRANSFER_ID_DIR_BIT | 2u,
                                   BBT_PROTO_STATUS_OK,
                                   100u);
    bbt_sender_handle_start_ack(packet_buf, len);
    bbt_sender_poll();
    fake_time_ms = core_ctx.sender.last_notify_time_ms + BBT_IDLE_TIMEOUT_MS;
    fake_ble_indicate_result = -1;

    bbt_sender_check_timeouts();

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_STORAGE, last_error_status);
}

void test_check_timeouts_should_set_error_when_end_resend_build_fails(void)
{
    core_ctx.sender.active = true;
    core_ctx.sender.state = BBT_STATE_WAITING_EXPORT_REPAIR;
    core_ctx.sender.transfer_id = BBT_TRANSFER_ID_DIR_BIT | 2u;
    core_ctx.sender.vt = &export_vt;
    core_ctx.sender.vt_ctx = app_ctx;
    core_ctx.sender.last_notify_time_ms = 0u;
    fake_time_ms = BBT_IDLE_TIMEOUT_MS;
    bbt_core_get_notify_buffer_StopIgnore();
    bbt_core_get_notify_buffer_IgnoreAndReturn(NULL);

    bbt_sender_check_timeouts();

    TEST_ASSERT_EQUAL(BBT_STATE_ERROR, core_ctx.sender.state);
    TEST_ASSERT_EQUAL(BBT_PROTO_ERR_STORAGE, last_error_status);
}
