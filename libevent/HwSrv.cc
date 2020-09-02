#include <time.h>
#include <memory>
#include <cassert>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <cstdlib>
#include <cstring>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

using std::shared_ptr;

constexpr int PORT = 9999;
const char *MESSAGE = "Hello World!";

static void onAccept(evconnlistener *, evutil_socket_t,
    sockaddr *, int, void *);
static void onWrite(bufferevent *, void *);
static void onEvent(bufferevent *, short, void *);
static void onSignal(evutil_socket_t, short, void *);


int 
main(int argc, char **argv) 
{

    sockaddr_in sin = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
    };
    auto base = shared_ptr<event_base>(event_base_new(), event_base_free);

    auto listener = shared_ptr<evconnlistener>(evconnlistener_new_bind(base.get(), onAccept, (void*)base.get(),
        LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1, (sockaddr*)&sin, sizeof(sin)), 
        evconnlistener_free);

    auto sigev = shared_ptr<event>(evsignal_new(base.get(), SIGINT, onSignal, (void*)base.get()), 
        event_free);
    
    if (event_add(sigev.get(), NULL) == -1) {
        printf("cannot add signal event!\n");
        return -1;
    }

    event_base_dispatch(base.get());

    printf("done!\n");

    return 0;
}

static void 
onAccept(evconnlistener *listener, evutil_socket_t fd,
    sockaddr *addr, int addrLen, void *data)
{
    event_base *base = (event_base*)data;
    auto buffevent = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    assert(buffevent != nullptr);

    bufferevent_setcb(buffevent, NULL, onWrite, onEvent, NULL);
    bufferevent_enable(buffevent, EV_WRITE);
    bufferevent_disable(buffevent, EV_READ);

    bufferevent_write(buffevent, MESSAGE, strlen(MESSAGE));
}

static void 
onWrite(bufferevent *buffevent, void *data)
{
    auto opt= bufferevent_get_output(buffevent);
    if (evbuffer_get_length(opt) == 0) {
        printf("flushed answer!\n");
        bufferevent_free(buffevent);
    }
}

static void 
onEvent(bufferevent *buffevent, short events, void *data)
{
    printf("events[0x%x] occures!\n", events);
    if (events & BEV_EVENT_EOF) {
        printf("Connection closed.\n");
    } else if (events & BEV_EVENT_ERROR) {
        printf("Got an error on the connection: %s\n",
        strerror(errno));
    }

    bufferevent_free(buffevent);
}
static void 
onSignal(evutil_socket_t fd, short events, void *data)
{
    timeval tv = {
        .tv_sec = 2,
        .tv_usec = 0
    };

    printf("Caught an interrupt signal; exiting cleanly in two"
        "seconds delay...\n");
    
    event_base_loopexit((event_base*)data, &tv);
}

