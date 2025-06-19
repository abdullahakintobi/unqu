#pragma once

#include <stdint.h>
#include <stdio.h>

#define UNQU             "unqu"
#define UNQUD            "unqud"
#define SOCK_PATH        "unqu"
#define SOCK_QUEUE_SIZE  44

////////////////////////////////////

#define UNUSED __attribute__((unused))
#define ASSERT(cond, msg)  do { \
	if (!(cond)) { \
		fprintf(stderr, "assertion failed: %s:%d (%s): %s\n", __FILE__, __LINE__, __func__, msg); \
		abort(); \
	}; \
} while (0)

#define loginfo(fmt, ...) do { \
	fprintf(stderr, "I: " fmt "\n",__VA_ARGS__); \
} while(0)

void xxd(const uint8_t *bytes, size_t n) {
	size_t j = 16;
        for (size_t i = 0; i < n; ++i) {
        	if (i%16 == 0) printf("%04lx: ", i);
                printf("%02x ", bytes[i]);
                j -= 1;
                if (j==0) {
                	printf("\n");
                	j = 16;
                }
        }
        printf("#\n");
}
