#pragma once

#include <stdint.h>

#define WIRE_VERSION 0
#define WIRE_END_BYTE 0x44

struct msgkill {
	int32_t pid;
} __attribute__((packed));

struct msglist {
	int v: 1;
} __attribute__((packed));

struct wire_frame {
	uint8_t version;
	uint8_t kind;
	// XXX:
	//	the invariant here is that the compiler doesn't put the padding byte
	//	at the top of the inner structs;
	union {
		struct msgkill kill;
		struct msglist list;
	} m;
	uint8_t end;
} __attribute__((packed));
