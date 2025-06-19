#define _POSIX_C_SOURCE 2
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum subcmd {
	SC_NOOP = -1,
	SC_LIST = 0,
	SC_KILL = 1,
};

const char* subcmd_opts[] = {
	// SC_LIST
	"h",

	// SC_KILL
	"h"
};

const char* subcmd_help[] = {
	// SC_LIST
	"list: list all processes, showing their command line and process id\n"
	"\nusage: list [-h]\n"
	,

	// SC_KILL
	"kill: terminates the specified process\n"
	"\nusage: kill [-h] <pid>\n"
	,

	//
};

void printhelp(enum subcmd cmd) {
	if (cmd == SC_NOOP) abort();
	fprintf(stderr, "%s", subcmd_help[cmd]);
	exit(0);
}

void printusage(void) {
	fprintf(stderr,
	        "usage: ./args [-h] <subcommand> [args...]\n"
	        "subcommands:\n"
	        "  list: list all processes, showing their command line and process id\n"
	        "  kill: terminates the specified process\n"
	);
	exit(0);
}

struct config {
	enum subcmd subcmd;
	union {
		// SC_KILL
		struct {
			int pid;
		};
	};
};

static
struct config parse_list(int argc, char* argv[]) {
	(void)argv;
	if (argc - optind > 0) {
		printhelp(SC_LIST);
	}
	return (struct config) {.subcmd = SC_LIST};
}

static
struct config parse_kill(int argc, char* argv[]) {
	switch (argc - optind) {
	case 1: break;
	case 0:
		fprintf(stderr, "%s: subcommand 'kill' expected a process id\n", argv[0]);
		printhelp(SC_KILL);
	default:
		fprintf(stderr, "%s: subcommand 'kill' expects only one argument\n", argv[0]);
		printhelp(SC_KILL);
	}

	int opt = getopt(argc, argv, subcmd_opts[SC_KILL]);
	switch (opt) {
	case -1: break;
	default: case 'h': printhelp(SC_KILL);
	}

	printf("arg=%s\n", argv[optind]);
	errno = 0;
	long int pid = strtol(argv[optind], NULL, 10);
	if (errno > 0) {
		fprintf(stderr, "%s: '%s' is not a valid process id\n", argv[0], argv[optind]);
		exit(1);
	}
	return (struct config) {.subcmd = SC_KILL, .pid = (int)pid};
}

int main(int argc, char* argv[]) {
	if (argc == 1 || (argc > 1 && strcmp(argv[1], "-h") == 0))
		printusage();

	enum subcmd cmd = SC_NOOP;
	struct config (*parse)(int argc, char* argv[]);
	if (strcmp(argv[1], "list") == 0) {
		cmd = SC_LIST;
		parse = &parse_list;
	} else if (strcmp(argv[1], "kill") == 0) {
		cmd = SC_KILL;
		parse = &parse_kill;
	} else {
		printusage();
	}

	// parsing begins after the subcommand string
	optind = 2;
	struct config conf = parse(argc, argv);
	assert(conf.subcmd == cmd);

	return 0;
}
