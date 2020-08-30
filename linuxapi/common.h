#ifndef __COMMON_H__
#define __COMMON_H__

#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <limits.h>
#include <errno.h>

#define IF_TRUE_EXIT(cond, ...) \
    do {    \
        if (cond) { \
            fprintf(stderr, __VA_ARGS__); \
            exit(-1);   \
        }   \
    } while (0)


#define callAndChk(func, ret, cond, ...) do {   \
    ret = (func)(__VA_ARGS__);           \
    if(cond) {              \
        errReport(ret, #func);     \
    }                           \
}while(0)


inline void errReport(int code, const char* errmsg) {
    int errcode = errno;
    printf("[%s]: %d\n", errmsg, code);
    printf("%d: %s\n\n", errcode, strerror(errcode));
    exit(-1);
}

#endif//;__COMMON_H__