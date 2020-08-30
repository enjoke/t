#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <signal.h>

#include <fcntl.h>
#include <errno.h>
#include <event2/event.h>
const char *fifo = "event.fifo";

static void onRead(evutil_socket_t fd, short events, void *data) {

    char buf[255];
    fprintf(stderr, "read function called with fd[%d], "
        "events[0x%x], data[%p]\n", fd, events, data);
    event *ev = (event*)data;
    auto len = ::read(fd, buf, sizeof(buf) - 1);
    if (len <= 0) {
        if (len == -1)
            perror("read");
        else 
            fprintf(stderr, "connection closed!\n");
        
        event_del(ev);
        event_base_loopbreak(event_get_base(ev));
        return;
    }

    buf[len] = '\0';
    fprintf(stdout, "read %s\n", buf);
}

static void onWrite(evutil_socket_t fd, short events, void *data) {

    char buf[255];
    fprintf(stderr, "write function called with fd[%d], "
        "events[0x%x], data[%p]\n", fd, events, data);
    event *ev = (event*)data;
    snprintf(buf, sizeof(buf), "Hello World!\n");
    auto len = ::write(fd, buf, strlen(buf));
    if (len <= 0) {
        if (len == -1)
            perror("write");
        else 
            fprintf(stderr, "connection closed!\n");
        
        event_del(ev);
        event_base_loopbreak(event_get_base(ev));
        return;
    }

    event_del(ev);
}
static void onSignal(evutil_socket_t fd, short event, void *data) {
    event_base_loopbreak((event_base*)data);
}

int main(int argc, char **argv) 
{
    struct stat st;

    if (lstat(fifo, &st) == 0) {
        if ((st.st_mode & S_IFMT) == S_IFREG) {
            errno = EEXIST;
            perror("lstat");
            exit(-1);
        }
    }

    unlink(fifo);
    if (mkfifo(fifo, 0600) == -1) {
        perror("mkfifo");
        exit(-1);
    }

    auto fd = open(fifo, O_RDWR | O_NONBLOCK, 0);
    if (fd == -1) {
        perror("open");
        exit(-1);
    }

    fprintf(stderr, "Write data to %s \n", fifo);

    auto base = std::shared_ptr<event_base>(event_base_new(),
        event_base_free);
    auto evRead = std::shared_ptr<event>(event_new(base.get(), fd, EV_READ | EV_PERSIST,
        onRead, event_self_cbarg()), event_free);
    auto evWrite = std::shared_ptr<event>(event_new(base.get(), fd, EV_WRITE | EV_PERSIST,
        onWrite, event_self_cbarg()), event_free);
    auto evSig = std::shared_ptr<event>(evsignal_new(base.get(), SIGINT, onSignal, (void*)base.get()), 
        event_free);
    
    event_add(evSig.get(), NULL);
    event_add(evRead.get(), NULL);
    event_add(evWrite.get(), NULL);
    event_base_dispatch(base.get());

    close(fd);
    unlink(fifo);
    libevent_global_shutdown();
    return 0;
}