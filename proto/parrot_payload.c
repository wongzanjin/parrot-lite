#include "parrot_payload.h"
#include <string.h>

typedef enum meta_type {
    kPositiveInt,
    kNegativeInt,
    kFixedString,
} meta_type;


static void parrot_payload_put_varint(c_string *payload, uint64_t value) {
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;

        if (value != 0) {
            byte |= 0x80;
        }

        c_string_add_char(payload, (char) byte);
    } while (value != 0);
}

parrot_bool parrot_payload_put_integer(c_string *payload, const uint8_t field_index, int64_t value) {
    if (field_index > 63)
        return parrot_false;

    uint8_t meta = 0;
    // field type
    if (value < 0) {
        meta |= kNegativeInt << 6;
        value = -value;
    } else {
        meta |= kPositiveInt << 6;
    }

    // field index
    meta |= field_index & 0x3F;

    // put meta byte
    c_string_add_char(payload, (char) meta);

    // put value
    parrot_payload_put_varint(payload, value);
    return parrot_true;
}

parrot_bool parrot_payload_put_string(c_string *payload, const uint8_t field_index, const char *data, int16_t length) {
    if (field_index > 63 || data == NULL)
        return parrot_false;

    if (length < 0) {
        length = (short) strlen(data);
    }

    if (length == 0) {
        return parrot_false;
    }

    uint8_t meta = 0;
    // field type
    meta |= kFixedString << 6;

    // field index
    meta |= field_index & 0x3F;

    // put meta byte
    c_string_add_char(payload, (char) meta);

    // put length
    parrot_payload_put_varint(payload, length);

    // put data
    c_string_append(payload, data, length);
    return parrot_true;
}


void parrot_payload_parse_init(payload_parse *parse, const void *data, const uint16_t len) {
    parse->pos = 0;
    parse->data = data;
    parse->length = len;
}

uint16_t parrot_payload_parse_entry(payload_entry *out, payload_parse *parse) {
    memset(out, 0, sizeof(*out));
    if (parse->pos >= parse->length)
        return 0;

    const uint16_t start = parse->pos;

    const uint8_t *bytes = parse->data;
    // read meta type
    const uint8_t meta = bytes[parse->pos];
    const uint8_t meta_type = meta >> 6;
    if (meta_type != kPositiveInt && meta_type != kNegativeInt && meta_type != kFixedString) {
        return parse->pos - start;
    }
    const uint8_t field_index = meta & 0x3F;

    // read varint
    ++parse->pos;
    int64_t value = 0;
    uint8_t var_byte;
    uint8_t bit_offset = 0;
    if (parse->pos >= parse->length) return parse->pos - start;
    do {
        if (bit_offset == 63) return parse->pos - start;

        var_byte = bytes[parse->pos];
        value |= (int64_t) (var_byte & 0x7F) << bit_offset;
        bit_offset += 7;
        ++parse->pos;
    } while ((var_byte & 0x80) != 0);

    switch (meta_type) {
        case kPositiveInt:
            out->key = field_index;
            out->value.i64 = value;
            out->is_string = 0;
            return parse->pos - start;
        case kNegativeInt:
            out->key = field_index;
            out->value.i64 = -value;
            out->is_string = 0;
            return parse->pos - start;
        case kFixedString:
            if (value > 512) {
                parse->pos = parse->length;
                return 0;
            }
            if (parse->pos + value > parse->length) {
                parse->pos = parse->length;
                return 0;
            }

            out->key = field_index;
            out->is_string = 1;
            out->value.str.data = (void*) bytes + parse->pos;
            out->value.str.length = value;
            parse->pos += value;
            return parse->pos - start;
        default:
            parse->pos = parse->length;
            return 0;
    }
}
