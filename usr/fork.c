#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/syscall.h>

static pid_t gettid(void)
{
	syscall(SYS_gettid);
}

static void *thread_1(void *arg)
{
	printf("In %s(), pid: %d, tid: %d \n",
		__func__, getpid(), gettid());

	if (fork()) {
		printf("Parent after fork(): pid:%d\n", getpid());
	} else {
		printf("Child after fork(): pid:%d, parent_pid:%d\n",
			getpid(), getppid());
	}
}

int main(void)
{
	int ret, *j;
	pthread_t tid;

	printf("In %s(), pid: %d, tid: %d \n",
		__func__, getpid(), gettid());

	ret = pthread_create(&tid, NULL, thread_1, NULL);
	if (ret) {
		printf("pthread_create failed\n");
		exit(-1);
	}
	pthread_join(tid, NULL);
	printf("new thread id is: %u\n", tid);
}