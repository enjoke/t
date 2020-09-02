#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <tuple>

#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#define MAX_OUTPUT (512*1024)
sockaddr_storage local, remote;
int lenLocal, lenRemote, useSSL, useWapper;
SSL_CTX *ssl_ctx;
event_base *base;
struct options {
    int     useSSL;
    int     useWapper;
    char   *localAddr;
    char   *remoteAddr;
    explicit options() = default;
    ~options() = default;

    options(options&& rhs) :
        useSSL(rhs.useSSL), useWapper(rhs.useWapper),
        localAddr(rhs.localAddr), remoteAddr(rhs.remoteAddr){
        rhs.useSSL = 0;
        rhs.useWapper = 0;
        localAddr = nullptr;
        remoteAddr = nullptr;
    }

    options& operator=(options&& rhs) {

        useSSL = rhs.useSSL;
        useWapper = rhs.useWapper;
        localAddr = rhs.localAddr;
        remoteAddr = rhs.remoteAddr;
        rhs.useSSL = 0;
        rhs.useWapper = 0;
        localAddr = nullptr;
        remoteAddr = nullptr;
        return *this;
    }
};

static void usage(char *);
static options getOpt(int , char **);
static void onRead(bufferevent *, void *);
static void onWrite(bufferevent *, void *);
static void onClose(bufferevent *, void *);
static void onEvent(bufferevent *, short, void *);
static void onAccept(evconnlistener *, evutil_socket_t, sockaddr *, int, void *);


int main(int argc, char **argv)
{
    if (argc < 3)   usage(argv[0]);

    auto opt = getOpt(argc, argv);

    ssl_ctx = nullptr;

    int lenLocal = sizeof(local), lenRemote = sizeof(remote);
    memset(&local, 0, lenLocal);
    memset(&remote, 0, lenRemote);

    if (!strchr(opt.localAddr, '.') && !strchr(opt.localAddr, ':')) {
        int port = atoi(opt.localAddr);
        fprintf(stderr, "%s: %d\n", opt.localAddr, port);
        assert(port >= 1 && port <= 65535);
        sockaddr_in *sin = (sockaddr_in*)&local;
        sin->sin_family = AF_INET;
        sin->sin_addr.s_addr = htonl(0x7f000001);
        sin->sin_port  = htons(port);
        lenLocal = sizeof(sockaddr_in);
    }
    else if (evutil_parse_sockaddr_port(opt.localAddr, 
            (sockaddr*)&local, &lenLocal) < 0) {
        
        usage(argv[0]);
    }

    if (evutil_parse_sockaddr_port(opt.remoteAddr, (sockaddr*)&remote, &lenRemote) < 0) {
        usage(argv[0]);
    }

    if (opt.useSSL) {
        useSSL = 1;
        int r = RAND_poll();
        assert(r != 0);
        ssl_ctx = SSL_CTX_new(TLS_method());
    }
    useWapper = opt.useWapper;
    base = event_base_new();
    assert(base);

    auto listener = evconnlistener_new_bind(base, onAccept, NULL, LEV_OPT_CLOSE_ON_FREE|LEV_OPT_CLOSE_ON_EXEC|LEV_OPT_REUSEABLE,
        -1, (sockaddr*)&local, lenLocal);
    assert(listener);

    event_base_dispatch(base);

    evconnlistener_free(listener);
    event_base_free(base);

    return 0;
}
static void 
usage(char *argv)
{
    fprintf(stderr, "Usage:\n"
        "%s [-s] [-W] <-l listen-addr> <-r remote-addr>\n", argv);
    exit(EXIT_FAILURE);
} 
static options 
getOpt(int argc, char **argv)
{
    int opt;
    options o;
    while ((opt = getopt(argc, argv, "sWl:r:")) != -1) {
        switch (opt) {
            case 's': o.useSSL = 1; break;
            case 'W': o.useWapper = 1; break;
            case 'l': o.localAddr = optarg; break;
            case 'r': o.remoteAddr = optarg; break;
            default: {
                fprintf(stderr, "Unknown option: %c\n", opt);
                usage(argv[0]);
            }
        }
    }
    
    fprintf(stderr, "%s: %s\n", o.localAddr, o.remoteAddr);
    return o;
}

static void 
onRead(bufferevent *evBuff, void *arg)
{

    bufferevent *pair = (bufferevent*)arg;
    auto src = bufferevent_get_input(evBuff);
    auto len = evbuffer_get_length(src);
    if (!pair) {
        evbuffer_drain(src, len);
        return;
    }

    auto dst = bufferevent_get_output(pair);
    evbuffer_add_buffer(dst, src);

    if (evbuffer_get_length(dst) >= MAX_OUTPUT) {
        bufferevent_setcb(pair, onRead, onWrite, onEvent, evBuff);
        bufferevent_setwatermark(pair, EV_WRITE, MAX_OUTPUT, MAX_OUTPUT);
        bufferevent_disable(evBuff, EV_READ);
    }
}

static void 
onWrite(bufferevent *evBuff, void *arg)
{
    bufferevent *pair = (bufferevent *)arg;
    bufferevent_setcb(evBuff, onRead, NULL, onEvent, pair);
    bufferevent_setwatermark(evBuff, EV_WRITE, 0, 0);
    if(pair) bufferevent_enable(pair, EV_READ);
}

static void 
onClose(bufferevent *evBuff, void *)
{
    auto buff = bufferevent_get_output(evBuff);
    if (!evbuffer_get_length(buff)) bufferevent_free(evBuff);

}
static void 
onEvent(bufferevent *evBuff, short what, void *arg)
{
    bufferevent *pair = (bufferevent*)arg;

    if (what & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        if (what & BEV_EVENT_ERROR) {
            unsigned long err;
            while((err = (bufferevent_get_openssl_error(evBuff)))) {
                auto msg = ERR_reason_error_string(err);
                auto lib = ERR_lib_error_string(err);
                auto func = ERR_func_error_string(err);
                fprintf(stderr, "%s in %s %s \n", msg, lib, func);
                if (errno) perror("connection error");
            }
        }

        if (pair) {
            onRead(evBuff, arg);

            if (evbuffer_get_length(bufferevent_get_output(
                pair
            ))) {
                bufferevent_setcb(pair, NULL, onClose, onEvent, NULL);
                bufferevent_disable(pair, EV_READ);
            } else bufferevent_free(pair);
        }

        bufferevent_free(evBuff);
    }


}
static void 
onAccept(evconnlistener *ctx, evutil_socket_t sock, sockaddr *addr, int len, void *arg)
{

    auto in = bufferevent_socket_new(base, sock, BEV_OPT_CLOSE_ON_FREE |
        BEV_OPT_DEFER_CALLBACKS);

    bufferevent *out;
    if (!useSSL || useWapper) {
        out = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE |
        BEV_OPT_DEFER_CALLBACKS);
    } else {
        SSL *ssl = SSL_new(ssl_ctx);
        out = bufferevent_openssl_socket_new(base, -1, ssl, BUFFEREVENT_SSL_CONNECTING,
		    BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);
    }

    assert(in && out);

    if (bufferevent_socket_connect(out, (sockaddr*)&remote, lenRemote) < 0) {
        perror("bufferevent_socket_connect");
        bufferevent_free(in);
        bufferevent_free(out);
        return;
    }

    if (useSSL && useWapper) {
        SSL *ssl = SSL_new(ssl_ctx);
        auto bOut = bufferevent_openssl_filter_new(base, out, ssl, BUFFEREVENT_SSL_CONNECTING,
            BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
        perror("bufferevent_openssl_filter_new");
        bufferevent_free(in);
        bufferevent_free(out);
        return;
    }

    bufferevent_setcb(in, onRead, NULL, onEvent, out);
    bufferevent_setcb(out, onRead, NULL, onEvent, in);
    bufferevent_enable(in, EV_READ | EV_WRITE);
    bufferevent_enable(out, EV_READ | EV_WRITE);
}