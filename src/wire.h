/*

the unqu wire protocol
--------------------

there's message header for a unit of communication:
   <protocol version (for debug and rate limiting)>

there's an enumeration of commands: each command is 1 byte
	commands {
		.LIST
		.KILL <pid: 4 byte>
	}

there's message frame as a unit of communication:
	<message header> (1 B)
	<command> (1 B)
	[command args]
	<end byte: 0x44> (1 B)

*/

#pragma once

#include <stdint.h>
#include <stdio.h>
#include "util.h"

#define WIRE_VERSION 0
#define WIRE_END_BYTE 0x44

#define KIND_LIST 0
#define KIND_KILL 1

#define kind2str(kind) ( \
	(kind) < KIND_LIST || (kind) > KIND_KILL ? \
		"UNKNOWN" : (kind) == KIND_LIST ? \
			"LIST" : "KILL" \
)

struct msgkill {
	int32_t pid;
} attr(packed);

void msgkill_print(struct msgkill* mk) {
	if (mk == NULL) return;

	printf("  pid %d\n", mk->pid);
}

struct msglist {
	uint8_t v: 1;
} attr(packed);

struct wire_frame {
	uint8_t version;
	uint8_t kind;
	 /* XXX:
		the invariant here is that the compiler doesn't put the padding byte
		at the top of the inner structs;
	*/
	union {
		struct msgkill kill;
		struct msglist list;
	} m;
	uint8_t end;
} attr(packed);

void wire_frame_print(struct wire_frame* frame) {
	if (frame == NULL) return;

	printf("Frame {\n"
		"  version %d\n"
		"  kind %s\n",
		frame->version, kind2str(frame->kind)
	);

	switch (frame->kind) {
	case KIND_KILL:
		msgkill_print(&frame->m.kill);
		break;
	case KIND_LIST:
		break;
	default:
		break;
	}

	puts("}");
}
