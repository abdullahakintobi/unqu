#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#define UNQU             "unqu"
#define UNQUD            "unqud"
#define SOCK_PATH        "unqu.sock"
#define SOCK_QUEUE_SIZE  44

////////////////////////////////////

#define attr(a) __attribute__((a))
#define UNUSED  attr(unused)
#define ASSERT(cond, msg)  do { \
	if (!(cond)) { \
		fprintf(stderr, "assertion failed: %s:%d (%s): %s\n", __FILE__, __LINE__, __func__, msg); \
		abort(); \
	}; \
} while (0)


void loginfo(const char *fmt, ...) attr(format(printf, 1, 2));

void loginfo(const char *fmt, ...)
{
	fprintf(stderr, "I: ");

	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
}

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
