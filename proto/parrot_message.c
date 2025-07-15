#include "parrot_message.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static char parrot_error_data[1024] = "";
static uint16_t parrot_error_len = 0;

#define PARROT_RETURN_FAIL(fmt, ...) { \
    parrot_error_len = snprintf(parrot_error_data, sizeof(parrot_error_data), fmt, ##__VA_ARGS__); \
    return parrot_false; \
    }

c_string parrot_get_last_error() {
    const c_string ret = {
        .data = parrot_error_data,
        .length = parrot_error_len
    };

    return ret;
}

typedef struct parrot_parse_buf {
    const uint8_t *bytes;
    uint16_t pos;
    uint16_t len;
} parrot_parse_buf;

static parrot_bool parrot_parse_buf_get_byte(parrot_parse_buf *buf, uint8_t *out_value_ptr) {
    if (buf->pos >= buf->len) {
        return parrot_false;
    }

    *out_value_ptr = buf->bytes[buf->pos++];
    return parrot_true;
}

static parrot_bool parrot_parse_buf_get_uint32(parrot_parse_buf *buf, uint32_t *out_value_ptr) {
    if (buf->pos + 4 > buf->len) {
        return parrot_false;
    }

    *out_value_ptr = ntohl(*((uint32_t *) (buf->bytes + buf->pos)));
    buf->pos += 4;
    return parrot_true;
}

static parrot_bool parrot_parse_buf_get_uint16(parrot_parse_buf *buf, uint16_t *out_value_ptr) {
    if (buf->pos + 2 > buf->len) {
        return parrot_false;
    }
    *out_value_ptr = ntohs(*((uint16_t *) (buf->bytes + buf->pos)));
    buf->pos += 2;
    return parrot_true;
}

static parrot_bool parrot_parse_buf_get_varint(parrot_parse_buf *buf, uint16_t *out_value_ptr) {
    if (buf->pos + 1 > buf->len) {
        return parrot_false;
    }
    const uint8_t lower_byte = buf->bytes[buf->pos++];
    *out_value_ptr = lower_byte & 0x7F;
    if (lower_byte & 0x80) {
        if (buf->pos + 1 > buf->len) {
            return parrot_false;
        }

        const uint8_t higher_byte = buf->bytes[buf->pos++];
        *out_value_ptr |= higher_byte << 7;
    }
    return parrot_true;
}


parrot_bool parrot_message_parse(parrot_message *msg, const void *data, const uint16_t length) {
    parrot_parse_buf buf = {
        .bytes = data,
        .pos = 0,
        .len = length
    };

    memset(msg, 0, sizeof(parrot_message));

    uint8_t magic = 0;
    if (!parrot_parse_buf_get_byte(&buf, &magic)) PARROT_RETURN_FAIL("data too short")

    if (magic != 0xFF) PARROT_RETURN_FAIL("bad invalid magic");

    uint8_t flags = 0;
    if (!parrot_parse_buf_get_byte(&buf, &flags)) PARROT_RETURN_FAIL("data too short");

    // version flags
    if (flags >> 6 != 1) PARROT_RETURN_FAIL("bad version number");

    // device
    const uint8_t device_flag = flags & 0x20;
    const uint8_t command_flag = flags & 0x10;
    const uint8_t serial_flag = flags & 0x08;
    const uint8_t payload_flag = flags & 0x04;
    const uint8_t checksum_flag = flags & 0x02;
    const uint8_t reserved_flag = flags & 0x01;
    if (reserved_flag != 0) PARROT_RETURN_FAIL("bad reserved flag");

    if (device_flag != 0) {
        if (!parrot_parse_buf_get_uint32(&buf, &msg->device)) PARROT_RETURN_FAIL("data too short");
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

    if (command_flag) {
        if (!parrot_parse_buf_get_varint(&buf, &msg->command)) PARROT_RETURN_FAIL("data too short");
    }

    if (serial_flag) {
        if (!parrot_parse_buf_get_varint(&buf, &msg->serial)) PARROT_RETURN_FAIL("data too short");
    }

    if (payload_flag) {
        if (!parrot_parse_buf_get_varint(&buf, &msg->payload_len)) PARROT_RETURN_FAIL("data too short");
    }

    if (msg->payload_len != 0) {
        if (buf.pos + msg->payload_len > buf.len) PARROT_RETURN_FAIL("data too short");

        msg->payload_data = (void*) (buf.bytes + buf.pos);
        buf.pos += msg->payload_len;
    } else {
        msg->payload_data = "";
    }

    if (checksum_flag != 0) {
        uint16_t checksum = 0;
        if (!parrot_parse_buf_get_uint16(&buf, &checksum)) PARROT_RETURN_FAIL("data too short");

        uint16_t actual_checksum = 0;
        for (uint16_t i = 0; i < buf.pos - 2; i++) {
            actual_checksum += buf.bytes[i];
        }

        if (checksum != actual_checksum) PARROT_RETURN_FAIL("checksum mismatch. expected %02x, actual %02x", checksum,
                                                            actual_checksum);
    }

    if (buf.pos < length) PARROT_RETURN_FAIL("data too long");

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


static uint16_t serialize_varint(uint8_t *buffer, const uint16_t value) {
    if (value == 0) {
        return 0;
    }

    buffer[0] = value & 0x7F;
    if (value > 127) {
        buffer[0] |= 0x80;
        buffer[1] = value >> 7;
        return 2;
    }

    buffer[0] = value;
    return 1;
}

uint16_t parrot_message_serialize(void *buf, uint16_t size, const parrot_message *msg,
                                  const parrot_bool add_checksum) {
    uint16_t pos = 0;
    uint8_t *bytes = buf;

    if (msg->payload_len > 500) PARROT_RETURN_FAIL("msg payload too long")

    const uint16_t required_size = 2 // magic + flags
        + device_size(msg->device) // device
        + var_int2_size(msg->command)
        + var_int2_size(msg->serial)
        + var_int2_size(msg->payload_len)
        + checksum_size(add_checksum)
        + msg->payload_len;
    if (size < required_size) PARROT_RETURN_FAIL("buffer too small")

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

    pos += serialize_varint(bytes + pos, msg->command);
    pos += serialize_varint(bytes + pos, msg->serial);
    pos += serialize_varint(bytes + pos, msg->payload_len);

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
