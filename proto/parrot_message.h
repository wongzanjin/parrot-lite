#pragma once

#if __cplusplus
extern "C" {
#endif
#include <stdint.h>

#include "c_string.h"


typedef struct parrot_message {
    uint32_t device;
    uint16_t command;
    uint16_t serial;
    uint16_t payload_len;
    const void *payload_data;
} parrot_message;

/**
 *
 * @return last error (message)
 */
c_string parrot_get_last_error();

/**
 * @brief Parse one message (array bytes)
 * @param msg [out] message structure
 * @param data [in] message data pointer
 * @param length [in] message data length
 * @return parrot_true (0x01) for success. parrot_false (0x00) for failure.
 */
parrot_bool parrot_message_parse(parrot_message *msg, const void *data, uint16_t length);

/**
 * Serialize message to byte array
 * @param buf [out] output buffer pointer
 * @param size [out] output buffer length
 * @param msg [in] message to be serialized
 * @param add_checksum [in] whether add optional checksum to serialized message (0: No, 1: Yes)
 * @return Length of serialized message in bytes
 */
uint16_t parrot_message_serialize(void *buf, uint16_t size, const parrot_message *msg, parrot_bool add_checksum);

#if __cplusplus
}
#endif
