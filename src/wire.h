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
#include "util.h"

#define WIRE_VERSION 0
#define WIRE_END_BYTE 0x44

struct msgkill {
	int32_t pid;
} attr(packed);

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
