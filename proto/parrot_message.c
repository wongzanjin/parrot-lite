#include "parrot_message.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static char parrot_error_data[1024] = "";
static uint16_t parrot_error_len = 0;

c_string parrot_get_last_error() {
    const c_string ret = {
        .data = parrot_error_data,
        .length = parrot_error_len
    };

    return ret;
}

parrot_bool parrot_message_parse(parrot_message *msg, const void *data, const uint16_t length) {
    uint16_t pos = 0;
    const uint8_t *bytes = data;

    memset(msg, 0, sizeof(parrot_message));

#define REQUIRE_MORE_BYTES(n) if (pos + n > length) { \
        parrot_error_len = snprintf(parrot_error_data, sizeof(parrot_error_data) - 1, "data too short"); \
        return parrot_false; \
    }

    REQUIRE_MORE_BYTES(2)
    const uint8_t magic = bytes[pos++];
    if (magic != 0xFF) {
        parrot_error_len = snprintf(parrot_error_data, sizeof(parrot_error_data), "bad magic number");
        return parrot_false;
    }

    const uint8_t flags = bytes[pos++];
    if (flags >> 6 != 1) {
        parrot_error_len = snprintf(parrot_error_data, sizeof(parrot_error_data), "bad version number");
        return parrot_false;
    }

    // device
    const uint8_t device_flag = flags & 0x20;
    const uint8_t command_flag = flags & 0x10;
    const uint8_t serial_flag = flags & 0x08;
    const uint8_t payload_flag = flags & 0x04;
    const uint8_t checksum_flag = flags & 0x02;
    const uint8_t reserved_flag = flags & 0x01;
    if (reserved_flag != 0) {
        parrot_error_len = snprintf(parrot_error_data, sizeof(parrot_error_data), "bad reserved flag");
        return parrot_false;
    }

    if (device_flag != 0) {
        REQUIRE_MORE_BYTES(4)

        uint32_t *device_ptr = (uint32_t*) (bytes + pos);
        msg->device = ntohl(*device_ptr);
        pos += 4;
    }

#define PARSE_OPTIONAL_VARINT(option_flag, msg_field) if (option_flag != 0) { \
        REQUIRE_MORE_BYTES(1) \
        const uint8_t lower_byte = bytes[pos++]; \
        msg->msg_field = lower_byte & 0x7F; \
        if (lower_byte & 0x80) { \
            REQUIRE_MORE_BYTES(1) \
            const uint8_t higher_byte = bytes[pos++]; \
            msg->msg_field |= higher_byte << 7; \
        } \
    }

    PARSE_OPTIONAL_VARINT(command_flag, command)
    PARSE_OPTIONAL_VARINT(serial_flag, serial)
    // PARSE_OPTIONAL_VARINT(payload_flag, length)

    if (payload_flag != 0) {
        if (pos + 1 > length) {
            parrot_error_len = __builtin___snprintf_chk(parrot_error_data, sizeof(parrot_error_data) - 1, 0,
                                                        __builtin_object_size(parrot_error_data, 2 > 1 ? 1 : 0),
                                                        "data too short");
            return 0;
        }
        const uint8_t lower_byte = bytes[pos++];
        msg->payload_len = lower_byte & 0x7F;
        if (lower_byte & 0x80) {
            if (pos + 1 > length) {
                parrot_error_len = __builtin___snprintf_chk(parrot_error_data, sizeof(parrot_error_data) - 1, 0,
                                                            __builtin_object_size(parrot_error_data, 2 > 1 ? 1 : 0),
                                                            "data too short");
                return 0;
            }
            const uint8_t higher_byte = bytes[pos++];
            msg->payload_len |= higher_byte << 7;
        }
    }


    if (msg->payload_len != 0) {
        REQUIRE_MORE_BYTES(msg->payload_len)

        msg->payload_data = (void*) (bytes + pos);
        pos += msg->payload_len;
    } else {
        msg->payload_data = "";
    }

    if (checksum_flag != 0) {
        REQUIRE_MORE_BYTES(2)
        const uint16_t *checksum_ptr = (uint16_t*) (bytes + pos);
        const uint16_t checksum = ntohs(*checksum_ptr);

        uint16_t actual_checksum = 0;
        for (uint16_t i = 0; i < pos; i++) {
            actual_checksum += bytes[i];
        }

        if (checksum != actual_checksum) {
            parrot_error_len = snprintf(parrot_error_data, sizeof(parrot_error_data),
                                        "checksum mismatch. expected %02x, actual %02x", checksum, actual_checksum);
            return parrot_false;
        }

        pos += 2;
    }

    if (pos < length) {
        parrot_error_len = snprintf(parrot_error_data, sizeof(parrot_error_data),
                                    "data too long");
        return parrot_false;
    }

    return parrot_true;
}

static uint16_t var_int2_size(const uint16_t value) {
    if (value == 0)
        return 0;
    if (value < 128)
        return 1;
    return 2;
}

static uint16_t device_size(const uint32_t device_id) {
    return device_id ? 4 : 0;
}

static uint16_t checksum_size(const parrot_bool add_checksum) {
    return add_checksum ? 2 : 0;
}

uint16_t parrot_message_serialize(void *buf, uint16_t size, const parrot_message *msg,
                                   const parrot_bool add_checksum) {
    uint16_t pos = 0;
    uint8_t *bytes = buf;

    if (msg->payload_len > 500) {
        parrot_error_len = snprintf(parrot_error_data, sizeof(parrot_error_data), "msg payload too long");
        return parrot_false;
    }

    const uint16_t required_size = 2 // magic + flags
        + device_size(msg->device) // device
        + var_int2_size(msg->command)
        + var_int2_size(msg->serial)
        + var_int2_size(msg->payload_len)
        + checksum_size(add_checksum)
        + msg->payload_len;
    if (size < required_size) {
        parrot_error_len = snprintf(parrot_error_data, sizeof(parrot_error_data), "buffer too small");
        return parrot_false;
    }

    memset(buf, 0, size);
    bytes[pos++] = 0xFF;
    bytes[pos] |= 0x01 << 6; // version
    if (msg->device) bytes[pos] |= 0x20;
    if (msg->command) bytes[pos] |= 0x10;
    if (msg->serial) bytes[pos] |= 0x08;
    if (msg->payload_len) bytes[pos] |= 0x04;
    if (add_checksum) bytes[pos] |= 0x02;

    ++pos;
    if (msg->device) {
        uint32_t *device_ptr = (uint32_t*) (bytes + pos);
        *device_ptr = htonl(msg->device);
        pos += 4;
    }

#define SERIALIZE_VARINT(msg_field) \
    if (msg->msg_field) { \
        bytes[pos] = msg->msg_field & 0x7F; \
        if (msg->msg_field > 127) { \
            bytes[pos++] |= 0x80; \
            bytes[pos++] |= msg->msg_field >> 7; \
        } else { \
            ++pos; \
        } \
    }

    SERIALIZE_VARINT(command)
    SERIALIZE_VARINT(serial)
    SERIALIZE_VARINT(payload_len)
    if (msg->payload_len) {
        memcpy(bytes + pos, msg->payload_data, msg->payload_len);
        pos += msg->payload_len;
    }

    if (add_checksum) {
        uint16_t checksum = 0;
        for (uint16_t i = 0; i < pos; i++) {
            checksum += bytes[i];
        }

        uint16_t *checksum_ptr = (uint16_t*) (bytes + pos);
        *checksum_ptr = htons(checksum);

        pos += 2;
    }
    return pos;
}
