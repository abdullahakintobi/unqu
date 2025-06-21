#define _POSIX_C_SOURCE 202506L
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util.h"
#include "wire.h"

///////////////////////////////////

typedef int state_t;
#define TS_INVALID  -1
#define TS_INACTIVE 0
#define TS_ACTIVE   1
#define TS_EXITED   2

struct task {
	pid_t  id;
	state_t state;
	int exitcode; /* only relevant if state is TS_EXITED */
	const char* name;
	const char* arg;
};

/* arg is a concatenated list of null-terminated strings: argv0\0argv1\0argv2\0...\0
   it must terminate with a null byte
*/
struct task newtask(const char* name, const char* arg) {
	ASSERT(name != NULL, "task name must be non-null pointer");
	return (struct task) {.id = -1, .state = TS_INACTIVE, .name = name, .arg = arg};
}

bool task_run(struct task* task) {
	ASSERT(task->name != NULL, "task name must be non-null pointer");

	int taskid = fork();

	if (taskid == -1) return false;
	if (taskid > 0) {
		task->id = taskid;
		task->state = TS_ACTIVE;
		ASSERT(waitpid(taskid, NULL, WNOHANG) == 0, "newly start process must be running, this is a bug");
		return true;
	}
	task->id = getpid();
	loginfo("executing %d: '%s'", task->id, task->name);
	if (execlp(task->name, task->name, task->arg, NULL) == -1) {
		perror("execlp");
		return false;
	}
	return true;
}

bool task_isnew(struct task* task) {
	return (task->id == -1) && (task->state == TS_INACTIVE);
}

///////////////////////////////////

struct config {
	bool help;
	bool daemon;
};

static
void printusage(int code) {
	fprintf(stderr,
	        "start the unqu daemon\n"
	        "\n"
	        "usage: " UNQUD " [-h] [-d]\n"
	        "\n"
	        " -d     daemonize, go to background\n"
	        " -h     print this help and exit\n"
	        "\n"
	);
	exit(code);
}

static
struct config parse_conf(int argc, char* argv[]) {
	struct config conf = {
		.help = false,
		.daemon = false,
	};

	int opt;
	while ((opt = getopt(argc, argv, "hd")) != -1) {
		switch (opt) {
		case 'h':
			conf.help = true;
			break;
		case 'd':
			conf.daemon = true;
			break;
		default: abort();
		}
	}
	if (conf.help) {
		printusage(0);
	}
	return conf;
}

int listener(void) {
	int ret;
	int sockfd;
	struct sockaddr_un name;

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd == -1) {
		perror("socket");
		exit(1);
	}

	memset(&name, 0, sizeof(name));

	name.sun_family = AF_UNIX;
	strncpy(name.sun_path, SOCK_PATH, sizeof(name.sun_path)-1);

	ret = bind(sockfd, (struct sockaddr*)&name, sizeof(name));
	if (ret == -1) {
		perror("bind");
		exit(1);
	};

	ret = listen(sockfd, SOCK_QUEUE_SIZE);
        if (ret == -1) {
                perror("listen");
                exit(1);
        }

	return sockfd;
}

///////////////////////////////////

#define MAX_TASK_COUNT 2

struct qu {
	int fd;
	int clientfd;
	size_t ntasks;
	struct task tasks[MAX_TASK_COUNT];
};

static bool had_sigintr = false;

static
void qu_sigintr(UNUSED int sig) {
	had_sigintr = true;
	loginfo("received SIGINT");
}

void qu_init(struct qu* qu, UNUSED struct config* conf) {
	if (qu == NULL) return;

	for (size_t i = 0; i < MAX_TASK_COUNT; i += 1)
		qu->tasks[i].state = TS_INVALID;

	struct sigaction act = {0};
	sigemptyset(&act.sa_mask);

	act.sa_handler = &qu_sigintr;
	if (sigaction(SIGINT, &act, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	qu->fd = listener();
	qu->ntasks = 0;
	qu->clientfd = -1;
}

static inline
int task_getid(struct task* task) {
	if (task == NULL) return -1;
	return (int)((size_t)task & 0xffffffff);
}

int qu_addtask(struct qu* qu, struct task* task) {
	if (qu->ntasks == MAX_TASK_COUNT) return -1;
	ASSERT(task != NULL, "cannot add null task");

	qu->tasks[qu->ntasks] = *task;
	qu->ntasks += 1;
	/* unnecessary but WTH */
	return task_getid(&qu->tasks[qu->ntasks - 1]);
}

int qu_runtask(UNUSED struct qu* qu, int taskid) {
	struct task *task = NULL;
	for (size_t i = 0; i < qu->ntasks; i += 1) {
		if (taskid == task_getid(qu->tasks + i)) {
			task = qu->tasks + i;
			break;
		}
	}

	if (task == NULL) return -1;

	task_run(task);

	return 0;
}

int qu_poll(struct qu* qu, uint32_t timeout /* in seconds */) {
	int nactive = 0;
	int status = 0;
	for (size_t i = 0; i < qu->ntasks; i += 1) {
		struct task* task = &qu->tasks[i];
		ASSERT(task->id > 0, "this task was not initialized");

		/* maybe this task terminated before we got to poll on it */
		if (task->state == TS_EXITED) continue;

		/* TODO:
			this returns -1 (and exit with 1) if there's no task is in TS_ACTIVE state.
			it's results should instead be interpreted as part of what is returned by poll(),
			with the task states updated approriately
		*/
		int id = waitpid(task->id, &status, WNOHANG);
		switch (id) {
		case -1:
			perror("waitpid");
			exit(1);
		case 0:
			if (task->state == TS_ACTIVE) nactive += 1;
			continue;
		default:
			if (WIFEXITED(status)) {
				loginfo("task %d done", task->id);
				task->state = TS_EXITED;
				task->exitcode = WEXITSTATUS(status);
			} else ASSERT(false, "what are we even doing?");
		}
	}

	struct pollfd pfd = {
		.fd = qu->fd,
		.events = POLLIN,
		.revents = -1,
	};

	while (1) {
		status = poll(&pfd, 1, 1000*timeout);
		if (status == -1) {
			/* TODO(Thu 19 Jun 23:14:51 WAT 2025):
				the more sensible thing to do here is to block signals that may have interrupted
				the poll
			*/
			if (errno == EINTR) {
				return -1;
			}
			perror("poll");
			exit(1);
		}
		break;
	}

	bool has_client = (status > 0) && ((pfd.revents & POLLIN) == POLLIN);
	if (has_client) {
		int fd = accept(qu->fd, NULL, NULL);
		if (fd == -1) {
			perror("accept");
			exit(1);
		}
		qu->clientfd = fd;
	}

	return nactive;
}

void qu_handleclient(struct qu* qu, struct wire_frame* frame) {
	ASSERT(frame != NULL, "got null frame");
	ASSERT(qu->clientfd > -1, "clientfd is negative");

	/* handle client */
	for (size_t i = 0; i < qu->ntasks; i += 1) {
		struct task* t = &qu->tasks[i];
		if (t->state == TS_EXITED) {
			loginfo("[%d] got shtudown in 2025", t->id);
		}
	}
	close(qu->clientfd);
	qu->clientfd = -1;
}

int main(int argc, char* argv[]) {
	int tid;
	int code;
	struct qu qu = {0};
	struct task t = {0};
	struct wire_frame* frame;
	static char buf[4096];
	struct config conf = parse_conf(argc, argv);

	qu_init(&qu, &conf);
	code = 0;

	t = newtask("sleep", "5");
	tid = qu_addtask(&qu, &t);
	qu_runtask(&qu, tid);

	t = newtask("sleep", "7");
	tid = qu_addtask(&qu, &t);
	qu_runtask(&qu, tid);

	for (;;) {
		if (had_sigintr) {
			code = 1;
			goto qu_shutdown;
		}
		qu_poll(&qu, -1);

		if (qu.clientfd > -1) {
			loginfo("has client");

			/* TODO:
				the more sensible thing to do here is to block signals that may have interrupted
				the poll
			* see TODO(Thu 19 Jun 23:14:51 WAT 2025)
			*/
			int n = read(qu.clientfd, buf, sizeof(buf));
			switch (n) {
			default:
				printf("got: ");
				xxd((const uint8_t*)buf, n);

				ASSERT((size_t)n >= sizeof(struct wire_frame), "wire frame size is incorrect");

				frame = (struct wire_frame*)buf;
				wire_frame_print(frame);

				qu_handleclient(&qu, frame);
				break;
			case 0:
				loginfo("client disconnected");
				 qu.clientfd = -1;
				continue;
			case -1:
				perror("read");
				code = 1;
				goto qu_shutdown;
			}
		}
	}
qu_shutdown:
	close(qu.fd);
	unlink(SOCK_PATH);
	exit(code);
}
