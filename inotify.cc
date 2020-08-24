#include "common.h"
#include <sys/inotify.h>

#define BUF_LEN (10 * sizeof(inotify_event) + NAME_MAX + 1)
int main(int argc, char **argv)
{

    IF_TRUE_EXIT((argc < 2 || strcmp("--help", argv[1]) == 0),
        "Usage: %s pathname...\n", argv[0]);


    int notifyFd, wd;
    callAndChk(inotify_init, notifyFd, (notifyFd == -1));
    
    for(int i=0; i<argc; ++i) {
        callAndChk(inotify_add_watch, wd, (wd == -1), notifyFd,
        argv[i], IN_ALL_EVENTS);
    }

    auto disp = [](inotify_event *ev) {
        printf("wd = %2d; ", ev->wd);
        if (ev->cookie > 0)
            printf("cookie = %4d;", ev->cookie);
        printf("mask = ");
        #define IF_MASK(x) \
            do {    \
            if (ev->mask & (x)) \
                printf(#x);    \
            } while(0)
        IF_MASK(IN_ACCESS);
        IF_MASK(IN_MODIFY);
        IF_MASK(IN_ATTRIB);
        IF_MASK(IN_CLOSE_WRITE);
        IF_MASK(IN_CLOSE_NOWRITE);
        IF_MASK(IN_OPEN);
        IF_MASK(IN_MOVED_FROM);
        IF_MASK(IN_MOVED_TO);
        IF_MASK(IN_CREATE);
        IF_MASK(IN_DELETE);
        IF_MASK(IN_DELETE_SELF);
        IF_MASK(IN_MOVE_SELF);

        printf("; ");

        if (ev->len > 0)
            printf("name = %s;", ev->name);

        printf("\n");
    };
    for(;;) {
        int num;
        char buf[BUF_LEN];
        callAndChk(read, num, (num <= 0), notifyFd, buf, BUF_LEN);

        printf("read [%ld] bytes from notify fd\n", static_cast<long>(num));

        for(char *p = buf; p < buf + num;) {
            inotify_event *event = (inotify_event*)p;
            disp(event);
            p += sizeof(inotify_event) + event->len;
        }
    }
    exit(EXIT_SUCCESS);
}