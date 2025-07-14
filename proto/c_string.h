#pragma once


#if __cplusplus
extern "C" {
#endif
#include <stdint.h>

typedef unsigned char parrot_bool;
#define parrot_true 1
#define parrot_false 0


typedef struct c_string c_string;

/**
 * @brief String (TEXT or BINARY) representation in C
 */
struct c_string {
    char *data; // String data
    uint32_t capacity;  // Length of string
    uint32_t length;    // Actual
};

/**
 * @brief Assign content to string
 *
 * @param str [out] String to be modified
 * @param data [in] Data pointer
 * @param length [in] Data length
 */
void c_string_assign(c_string *str, const char *data, int length);

/**
 * @brief Append to string
 *
 * @param str [out] String to be modified
 * @param data [in] Data pointer
 * @param length [in] Data length
 */
void c_string_append(c_string *str, const char *data, int length);

/**
 * @brief Append a character to string
 * @param str [out] String to be modified
 * @param ch [in] Character to append
 */
void c_string_add_char(c_string *str, char ch);

/**
 * @brief Clears a string's contents, without deallocating the storage
 *
 * @param str [out] String to be cleared
 */
void c_string_soft_clear(c_string *str);

 /**
 * @brief Clears a string's contents, and deallocates the storage
 *
 * @param str [out] String to be cleared
 */
void c_string_hard_clear(c_string *str);

/**
 * @brief Erase characters from string
 *
 * @param str [out] String to be modified
 * @param pos [in] Position of the first character to be erased.
 * @param count [in] Number of characters to erase (if the string is shorter, as many characters as possible are erased).
 */
void c_string_erase(c_string *str, uint32_t pos, uint32_t count);

#if __cplusplus
}
#endif
