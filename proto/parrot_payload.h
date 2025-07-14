#pragma once

#if __cplusplus
extern "C" {
#endif
#include <stdint.h>

#include "parrot_message.h"

parrot_bool parrot_payload_put_integer(c_string *, uint8_t field_index, int64_t value);
parrot_bool parrot_payload_put_string(c_string *, uint8_t field_index, const char *data, int16_t length);


typedef struct {
    void (*on_integer_field)(void *user_data, uint8_t field_index, int64_t value);
    void (*on_string_field)(void *user_data, uint8_t field_index, const char *data, int16_t length);
} parrot_payload_callbacks;

typedef struct payload_entry {
    uint8_t key;
    uint8_t is_string;

    union {
        c_string str;
        int64_t i64;
    } value;
} payload_entry;

typedef struct payload_parse {
    const void *data;
    uint16_t length;
    uint16_t pos;
} payload_parse;

void parrot_payload_parse_init(payload_parse *parse, const void *data, uint16_t len);

uint16_t parrot_payload_parse_entry(payload_entry *out, payload_parse *parse);

#if __cplusplus
}
#endif
