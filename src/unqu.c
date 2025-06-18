#define _POSIX_C_SOURCE 199309L
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define UNUSED __attribute__((unused))
#define ASSERT(cond, msg)  do { \
	if (!(cond)) { \
		fprintf(stderr, "assertion failed: %s:%d (%s): %s\n", __FILE__, __LINE__, __func__, msg); \
		abort(); \
	}; \
} while (0)

#define loginfo(fmt, ...) do { \
	fprintf(stderr, "I: " fmt "\n",__VA_ARGS__); \
} while(0)


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

void qu_init(struct qu* qu) {
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

int qu_poll(struct qu *qu) {
	int nactive = 0;
	for (size_t i = 0; i < qu->ntasks; i += 1) {
		if (is_running(qu->tasks[i].id)) {
			nactive += 1;
		}
	}
	return nactive;
}

int main(void) {
	struct qu qu = {0};

	qu_init(&qu);

	struct task t = newtask("sleep", "2");
	int tid = qu_addtask(&qu, &t);
	qu_runtask(&qu, tid);
	loginfo("[task] %d, tid %d", t.id, tid);

	while (1) {
		if (had_sigchld) {
			had_sigchld = false;
			qu_update(&qu, &last_siginfo);
		}
		qu_poll(&qu);
		sleep(1);
	}
}
