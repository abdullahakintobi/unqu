#define _POSIX_C_SOURCE 2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum subcmd {
	SC_NOOP = -1,
	SC_LIST = 0,
};

const char* subcmd_opts[] = {
	// SC_LIST
	"h",

	//
};

const char* subcmd_help[] = {
	// SC_LIST
	"list: list all processes, showing their command line and process id\n"
	"\nusage: list [-h]\n"
	,

	//
};

void printhelp(enum subcmd cmd) {
	if (cmd == SC_NOOP) abort();
	fprintf(stderr, "%s", subcmd_help[cmd]);
}

void printusage(void) {
	fprintf(stderr,
	        "usage: ./args [-h] <subcommand> [args...]\n"
	        "subcommands:\n"
	        "  list: list all processes, showing their command line and process id\n"
	);
	exit(0);
}

int main(void) {
	int opt;
	int argc = 3;
	char *argv[] = {"args", "list", "-h"};

	if ((argc > 1) && (strcmp(argv[1], "-h") == 0)) {
		printusage();
	}

	enum subcmd cmd = SC_NOOP;
	if (strcmp(argv[1], "list") == 0) {
		cmd = SC_LIST;
	} else {
		printusage();
	}

	optind = 2;
	while ((opt = getopt(argc, argv, subcmd_opts[cmd])) != -1) {
		switch (opt) {
		case 'h': printhelp(cmd); break;
		}
	};

	return 0;
}
