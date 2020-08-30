#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#define callAndChk(func, args) do {   \
    int ret = (func)(args);           \
    if(ret != 0) {              \
        errReport(ret, #func);     \
    }                           \
}while(0);

#define callAndChk1(func, argv1, argv2) do {   \
    int ret = (func)((argv1), (argv2));           \
    if(ret != 0) {              \
        errReport(ret, #func);     \
    }                           \
}while(0);

#define callAndChk2(func, argv1, argv2, argv3) do {   \
    int ret = (func)((argv1), (argv2), (argv3));           \
    if(ret != 0) {              \
        errReport(ret, #func);     \
    }                           \
}while(0);

#define callAndChk3(func, argv1, argv2, argv3, argv4) do {   \
    int ret = (func)((argv1), (argv2), (argv3), (argv4));           \
    if(ret != 0) {              \
        errReport(ret, #func);     \
    }                           \
}while(0);

void errReport(int code, const char* errmsg) {
    int errcode = errno;
    printf("[%s]: %d\n", errmsg, code);
    printf("%d: %s\n\n", errcode, strerror(errcode));
    exit(-1);
}
static pthread_cond_t threadDied = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t threadMutex = PTHREAD_MUTEX_INITIALIZER;

static int totThreads = 0;
static int numLive = 0;

static int numUnJoined = 0;

enum tstate {
    TS_ALIVE,
    TS_TERMINATED,
    TS_JOIN
};

static struct {
    pthread_t tid;
    enum tstate state;
    int sleepTime;
} *thread;

static void *threadFunc(void *arg) {
    int idx = *((int*)arg);

    sleep(thread[idx].sleepTime);
    printf("Thread %d terminating\n", idx);

    callAndChk(pthread_mutex_lock, &threadMutex);

    ++numUnJoined;
    thread[idx].state = TS_TERMINATED;

    callAndChk(pthread_mutex_unlock, &threadMutex);

    callAndChk(pthread_cond_signal, &threadDied);
    return NULL;
}

int main(int argc, char** argv) {

    if(argc < 2 || !strcmp(argv[1], "--help") 
        || !strcmp(argv[1], "-h")) {
        printf("Usage: %s nsecs...\n", argv[0]);
        return -1;
    }

    thread = calloc(argc - 1, sizeof(*thread));
    if(NULL == thread) {
        printf("calloc failed!\n");
        return -1;
    }

    for(int idx=0; idx<argc-1; ++idx) {
        thread[idx].sleepTime = 5;
        thread[idx].state = TS_ALIVE;
        callAndChk3(pthread_create, &thread[idx].tid, NULL, threadFunc, &idx);

    }

    totThreads = argc - 1;

    numLive = totThreads;

    while(numLive > 0) {
        callAndChk(pthread_mutex_lock, &threadMutex);
        while(numUnJoined == 0) {
            callAndChk1(pthread_cond_wait, &threadDied, &threadMutex);
        }

        for(int idx=0; idx<totThreads; ++idx) {
            if(thread[idx].state == TS_TERMINATED) {
                callAndChk1(pthread_join, thread[idx].tid, NULL);

                thread[idx].state = TS_JOIN;
                --numLive;
                --numUnJoined;
                printf("Reaped thread %d (numLive=%d)\n", idx, numLive);
            }
        }

        callAndChk(pthread_mutex_unlock, &threadMutex);
    }

    return 0;
}