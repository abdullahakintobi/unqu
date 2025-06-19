#define _POSIX_C_SOURCE 199309L
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
	int exitcode; // only relevant if state is TS_EXITED
	const char* name;
	const char* arg;
};

static
bool is_running(pid_t pid) {
	int id = waitpid(pid, NULL, WNOHANG);
	// ASSERT(id >= 0, "must not fail, currently");
	return id == 0;
}

// arg is a concatenated list of null-terminated strings: argv0\0argv1\0argv2\0...\0
// it must terminate with a null byte
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
		ASSERT(is_running(taskid), "newly start process must be running, this is a bug");
		return true;
	}
	loginfo("executing '%s'", task->name);
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
	struct task tasks[MAX_TASK_COUNT];
	size_t ntasks;
};

static bool had_sigchld = false;
static siginfo_t last_siginfo;

static
void task_signalled(int sig, siginfo_t *si, UNUSED void* uctx) {
	ASSERT(sig == SIGCHLD, "this handler is only for SIGCHLD");
	printf("sigchld called on %d\n", sig);
	last_siginfo = *si;
	had_sigchld = true;
}

void qu_init(struct qu* qu, UNUSED struct config* conf) {
	if (qu == NULL) return;

	qu->ntasks = 0;
	for (size_t i = 0; i < MAX_TASK_COUNT; i += 1)
		qu->tasks[i].state = TS_INVALID;

	struct sigaction act = {0};
	sigemptyset(&act.sa_mask);
	act.sa_sigaction = &task_signalled;
	act.sa_flags |= SA_NOCLDWAIT | SA_SIGINFO;
	if (sigaction(SIGCHLD, &act, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
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
	// unnecessary but WTH
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

static
int qu_update(struct qu* qu, siginfo_t* last_si) {
	ASSERT(qu->ntasks > 0, "we shouldn't receive SIGCHLD when no process was ever added). this is a bug");

	for (size_t i = 0; i < qu->ntasks; i += 1) {
		struct task *task = &qu->tasks[i];
		if (task->id == last_si->si_pid) {
			loginfo("task %d done", task->id);
			task->state = TS_EXITED;
			break;
		}
	}
	qu->ntasks -= 1;

	return 0;
}

int qu_poll(struct qu* qu, uint32_t timeout) {
	int nactive = 0;
	int status = 0;
	for (size_t i = 0; i < qu->ntasks; i += 1) {
		struct task* task = &qu->tasks[i];
		ASSERT(task->id > 0, "this task was not initialized");

		// maybe this task terminated before we got to poll on it
		if (task->state == TS_EXITED) continue;

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
				task->state = TS_EXITED;
				task->exitcode = WEXITSTATUS(status);
			} else ASSERT(false, "what are we even doing?");
		}
	}
	sleep(timeout);

	return nactive;
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

int main(int argc, char* argv[]) {
	int tid;
	struct qu qu = {0};
	struct task t = {0};
	struct config conf = parse_conf(argc, argv);

	qu_init(&qu, &conf);

	t = newtask("sleep", "5");
	tid = qu_addtask(&qu, &t);
	qu_runtask(&qu, tid);
	loginfo("[task] %d, tid 0x%d", t.id, tid);

	t = newtask("sleep", "7");
	tid = qu_addtask(&qu, &t);
	qu_runtask(&qu, tid);
	loginfo("[task] %d, tid 0x%x", t.id, tid);

	while (1) {
		if (had_sigchld) {
			had_sigchld = false;
			qu_update(&qu, &last_siginfo);
		}
		qu_poll(&qu, 1);

		for (size_t i = 0; i < qu.ntasks; i += 1) {
			struct task* t = &qu.tasks[i];
			switch (t->state) {
			case TS_ACTIVE:
				loginfo("[%d] still active in 2025", t->id);
				break;
			case TS_EXITED:
				loginfo("[%d] got shtudown in 2025", t->id);
			}
		}
	}
}
