// main/uuid_utils.c
#include "uuid_utils.h"
#include <stdio.h>

void print_uuid(const uint8_t *uuid) {
    for (int i = 0; i < 16; i++) {
        printf("%02x", uuid[i]);
        if (i < 15) printf(":");
    }
    printf("\n");
}

