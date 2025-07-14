#include "c_string.h"


#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t c_calculate_capacity(const size_t size) {
    size_t capacity = 16;
    while (capacity < size) {
        capacity = capacity << 1;
    }
    return capacity;
}

static void c_string_ensure_capacity(c_string *str, const size_t length) {
    if (str->capacity > length + 1) {
        return;
    }

    const size_t capacity = c_calculate_capacity(length + 1);

    void *data = realloc(str->data, capacity);
    assert(data != NULL);

    str->data = data;
    str->capacity = capacity;
}

void c_string_append(c_string *str, const char *data, int length) {
    if (length < 0) {
        length = (int) strlen(data);
    }

    c_string_ensure_capacity(str, str->length + length + 1);

    memcpy(str->data + str->length, data, length);
    str->length += length;
    str->data[str->length] = '\0';
}

void c_string_add_char(c_string *str, const char ch) {
    c_string_ensure_capacity(str, str->length + 1 + 1);
    str->data[str->length] = ch;
    str->data[++str->length] = '\0';
}

void c_string_assign(c_string *str, const char *data, const int length) {
    c_string_soft_clear(str);

    c_string_append(str, data, length);
}

void c_string_soft_clear(c_string *str) {
    str->length = 0;
}

void c_string_hard_clear(c_string *str) {
    if (str->data == NULL) {
        return;
    }

    free(str->data);
    str->data = NULL;

    str->capacity = 0;
    str->length = 0;
}

void c_string_erase(c_string *str, const uint32_t pos, const uint32_t count) {
    if (str->data == NULL || pos >= str->length) {
        return;
    }

    if (pos + count >= str->length) {
        str->length = pos;
        return;
    }

    const size_t move_count = str->length - pos - count;

    if (move_count > 0) {
        memmove(str->data + pos, str->data + pos + count, move_count);
    }

    str->length = pos + move_count;
    str->data[str->length] = '\0';
}
