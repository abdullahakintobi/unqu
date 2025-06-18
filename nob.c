#define nob_cc_flags(cmd) cmd_append(cmd, "-Wall", "-Wextra", "-Wswitch-enum", "-std=c99", "-D_POSIX_SOURCE", "-ggdb", "-I.");

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

// Folder must end with forward slash /
#define BUILD "build/"
#define SRC "src/"

int main(int argc, char** argv) {
	NOB_GO_REBUILD_URSELF(argc, argv);

	if (!mkdir_if_not_exists(BUILD)) return 1;

	const char* progname = shift(argv, argc);

	Cmd cmd = {0};
	nob_cc(&cmd);
	nob_cc_flags(&cmd);
	nob_cc_inputs(&cmd, SRC"unqu.c");
	nob_cc_output(&cmd, BUILD"unqu");

	if (!cmd_run_sync_and_reset(&cmd)) return 1;

	nob_cc(&cmd);
	nob_cc_flags(&cmd);
	nob_cc_inputs(&cmd, SRC"args.c");
	nob_cc_output(&cmd, BUILD"args");
	if (!cmd_run_sync_and_reset(&cmd)) return 1;

	printf("progname = %s\n", progname);

	return 0;
}
