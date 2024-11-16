// main/uuid_utils.h
#ifndef UUID_UTILS_H
#define UUID_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Prints a UUID in the standard hexadecimal format.
 *
 * @param uuid Pointer to a 16-byte array representing the UUID.
 */
void print_uuid(const uint8_t *uuid);

#ifdef __cplusplus
}
#endif

#endif // UUID_UTILS_H
