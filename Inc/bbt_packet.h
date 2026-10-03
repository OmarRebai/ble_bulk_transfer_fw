/**
 * @file            bbt_packet.h
 * @brief           Packet parsing and building helpers for the BLE Bulk Transfer protocol.
 * Updated with NACK bitmap refactor and Doxygen headers.
 * @date            25.05.2026
 * @author          Omar Rebai
 * @copyright       &copy; 2026 habemus! electronic + transfer GmbH
 */
#ifndef BBT_PACKET_H
#define BBT_PACKET_H

#include "bbt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse a START packet into start metadata.
 *
 * @param data Pointer to raw START packet bytes (opcode already present).
 * @param len Length of the packet buffer.
 * @param out Output metadata filled on success.
 * @return true if the START packet was valid and out is populated.
 */
bool bbt_packet_parse_start(const uint8_t *data,
                            uint16_t len,
                            bbt_start_meta_t *out);

/**
 * @brief Parse a CHUNK packet and extract header and payload pointer.
 *
 * @param data Raw packet buffer.
 * @param len Length of the packet buffer.
 * @param out Out parameter receiving parsed header and payload pointer.
 * @return true if parsing succeeded and out is valid.
 */
bool bbt_packet_parse_chunk(const uint8_t *data,
                            uint16_t len,
                            bbt_chunk_packet_t *out);

/**
 * @brief Parse an END packet (transfer completion indication).
 *
 * @param data Raw packet buffer.
 * @param len Packet length in bytes.
 * @param out Packet id output structure.
 * @return true if parsed successfully.
 */
bool bbt_packet_parse_end(const uint8_t *data,
                          uint16_t len,
                          bbt_packet_id_t *out);

/**
 * @brief Parse an ABORT packet and extract transfer id.
 */
bool bbt_packet_parse_abort(const uint8_t *data,
                            uint16_t len,
                            bbt_packet_id_t *out);

/**
 * @brief Parse a PULL_REQ packet used to request an export.
 */
bool bbt_packet_parse_pull_req(const uint8_t *data,
                                                             uint16_t len,
                                                             bbt_pull_req_t *out);

/**
 * @brief Parse a NACK packet (bitmap segmented form).
 *
 * @param data Raw packet buffer.
 * @param len Packet length in bytes.
 * @param out Output header structure (bitmap_len indicates payload size).
 * @param bitmap Out pointer set to the bitmap payload within data (valid if non-NULL).
 * @param bitmap_len Out parameter set to the bitmap payload length in bytes (if non-NULL).
 * @return true on successful parse.
 */
bool bbt_packet_parse_nack(const uint8_t *data,
                           uint16_t len,
                           bbt_nack_header_t *out,
                           const uint8_t **bitmap,
                           uint16_t *bitmap_len);

/**
 * @brief Parse a COMPLETE packet.
 */
bool bbt_packet_parse_complete(const uint8_t *data,
                               uint16_t len,
                               bbt_packet_id_t *out);

/**
 * @brief Parse an ERROR packet and extract error code.
 */
bool bbt_packet_parse_error(const uint8_t *data,
                            uint16_t len,
                            bbt_error_packet_t *out);

/**
 * @brief Build a START packet into the provided buffer.
 *
 * @return Number of bytes written on success, 0 on error.
 */
uint16_t bbt_packet_build_start(uint8_t *buf,
                               uint16_t max_len,
                               const bbt_start_meta_t *meta);

/**
 * @brief Parse a START_ACK packet.
 */
bool bbt_packet_parse_start_ack(const uint8_t *data,
                               uint16_t len,
                               bbt_start_ack_t *out);

/**
 * @brief Build a START_ACK notification.
 */
uint16_t bbt_packet_build_start_ack(uint8_t *buf,
                                   uint16_t max_len,
                                   const bbt_start_ack_t *ack);

/**
 * @brief Build a COMPLETE notification.
 */
uint16_t bbt_packet_build_complete(uint8_t *buf,
                                  uint16_t max_len,
                                  const bbt_packet_id_t *id);

/**
 * @brief Build an ERROR notification.
 */
uint16_t bbt_packet_build_error(uint8_t *buf,
                               uint16_t max_len,
                               const bbt_error_packet_t *error);

/**
 * @brief Build a PULL_REQ packet (device->app export request).
 */
uint16_t bbt_packet_build_pull_req(uint8_t *buf,
                                  uint16_t max_len,
                                  const bbt_pull_req_t *req);

/**
 * @brief Build a CHUNK packet with header and payload.
 */
uint16_t bbt_packet_build_chunk(uint8_t *buf,
                               uint16_t max_len,
                               const bbt_chunk_header_t *header,
                               const uint8_t *payload);

/**
 * @brief Build an END notification.
 */
uint16_t bbt_packet_build_end(uint8_t *buf,
                             uint16_t max_len,
                             const bbt_packet_id_t *id);

/**
 * @brief Build a NACK packet containing a bitmap segment.
 *
 * The header->bitmap_len field controls how many bytes of the bitmap[] are
 * included in the packet.
 */
uint16_t bbt_packet_build_nack(uint8_t *buf,
                              uint16_t max_len,
                              const bbt_nack_header_t *header,
                              const uint8_t *bitmap);

/**
 * @brief Compute CRC16-CCITT over data.
 */
uint16_t bbt_crc16_ccitt(const uint8_t *data, uint16_t len);

/**
 * @brief CRC32 helpers.
 */
uint32_t bbt_crc32_init(void);
uint32_t bbt_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);
uint32_t bbt_crc32_final(uint32_t crc);

#ifdef __cplusplus
}
#endif

#endif /* BBT_PACKET_H */