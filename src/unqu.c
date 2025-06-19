#define  _POSIX_C_SOURCE 2
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "util.h"
#include "wire.h"

enum subcmd {
	SC_NOOP = -1,
	SC_LIST = 0,
	SC_KILL = 1,
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
	        "usage: " UNQU " [-h] <subcommand> [args...]\n"
	        "subcommands:\n"
	        "  list: list all processes, showing their command line and process id\n"
	        "  kill: terminates the specified process\n"
	);
	exit(0);
}

// TODO(Thu 19 Jun 12:53:42 WAT 2025):
// 	rename this to command
struct config {
	enum subcmd subcmd;
	union {
		// SC_KILL
		struct {
			int32_t pid;
		} kill;
	};
};

struct wire_frame config_towire(struct config* conf) {
	struct wire_frame wf_ret = {0};
	wf_ret.version = WIRE_VERSION;
	wf_ret.end = WIRE_END_BYTE;
	wf_ret.kind = (uint8_t) conf->subcmd;

	switch (conf->subcmd) {
	case SC_NOOP: abort();
	case SC_LIST:
		break;
	case SC_KILL:
		wf_ret.m.kill = *(struct msgkill*) &conf->kill;
		break;
	}
	return wf_ret;
}

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
		break;
	default:
		fprintf(stderr, "%s: subcommand 'kill' expects only one argument\n", argv[0]);
		printhelp(SC_KILL);
	}

	int opt = getopt(argc, argv, "h");
	switch (opt) {
	case -1: break;
	default: case 'h': printhelp(SC_KILL);
	}

	errno = 0;
	long int pid = strtol(argv[optind], NULL, 10);
	if (errno > 0) {
		fprintf(stderr, "%s: '%s' is not a valid process id\n", argv[0], argv[optind]);
		exit(1);
	}

	return (struct config) {.subcmd = SC_KILL, .kill = { .pid = (int32_t)pid}};
}

struct config parse_conf(int argc, char* argv[]) {
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
	return conf;
}

int clientsock(void) {
	int ret;
	int sockfd;
	struct sockaddr_un name;

	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	memset(&name, 0, sizeof(name));

	name.sun_family = AF_UNIX;
	strncpy(name.sun_path, SOCK_PATH, sizeof(name.sun_path)-1);

	ret = connect(sockfd, (struct sockaddr*)&name, sizeof(name));
	if (ret == -1) {
		perror("connect");
		exit(1);
	};

	return sockfd;
}

int writeall(int out, const char* bytes, size_t sz) {
	return -1;
}

static char buf[4096];
int main(int argc, char* argv[]) {
	int in;
	struct config conf;
	struct wire_frame wire;

	conf = parse_conf(argc, argv);
	in = clientsock();

	{ /* send the config */
		wire = config_towire(&conf);
		xxd((const uint8_t*)&wire, sizeof(wire));
		if (writeall(in, (const char*)&wire, sizeof(wire)) == -1) {
			perror("writeall");
			exit(1);
		}
	}

        { /* read all */
                int n = -1;
                while (n != 0) {
                        n = read(in, buf, sizeof(buf));
                        switch (n) {
                        case -1:
                                perror("read");
                                exit(1);
                        case 0:
                                break;
                        default:
                                printf("%*.s", n, buf);
                        }
                }

                puts("[client] done.");
        }
}
