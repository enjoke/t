
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <assert.h>
#include <signal.h>

using std::shared_ptr;
const char *host = "127.0.0.1";
const int port = 9999;

static void onRead(bufferevent *buffevent, void *data);
static void onEvent(bufferevent *, short, void *);
static void onSignal(evutil_socket_t, short, void *);

int 
main(int argc, char **argv) 
{

    auto base = shared_ptr<event_base>(
        event_base_new(), 
        event_base_free
        );
    auto buffevent = shared_ptr<bufferevent>(
        bufferevent_socket_new(base.get(), -1, BEV_OPT_CLOSE_ON_FREE), 
        bufferevent_free
        );
    
    bufferevent_setcb(buffevent.get(), onRead, NULL, onEvent, (void*)base.get());
    bufferevent_enable(buffevent.get(), EV_READ);
    bufferevent_disable(buffevent.get(), EV_WRITE);

    auto sigev_ = shared_ptr<event>(
        evsignal_new(base.get(), SIGINT, onSignal, (void*)base.get()),
        event_free     
        );

    if(bufferevent_socket_connect_hostname(
        buffevent.get(), NULL, AF_INET, host, port
    ) == -1) {
        printf("cannot connect to host[%s]\n", host);
        return -1;
    }
    event_base_dispatch(base.get());

    printf("done!\n");
    return 0;
}

static void 
onRead(bufferevent *buffevent, void *data)
{
    char buff[4096] = {0};
    size_t bytes;
    
    while ((bytes = bufferevent_read(
        buffevent, buff, 4096
    )) > 0) {
        printf("[%lu] bytes read: %s\n", 
        bytes, buff);
        memset(buff, 0, 4096);
    }
}

static void 
onEvent(bufferevent *buffevent, short events, void *data)
{
    printf("events[0x%x] occures!\n", events);
    if (events & BEV_EVENT_EOF) {
        printf("Connection closed.\n");
        event_base_loopbreak((event_base*)data);
    } else if (events & BEV_EVENT_ERROR) {
        printf("Got an error on the connection: %s\n",
        strerror(errno));
        event_base_loopbreak((event_base*)data);
    }
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