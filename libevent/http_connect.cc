#include <unistd.h>
#include <limits.h>

#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/buffer.h>

#include <cstdio>
#include <cstdlib>


using namespace std;
#define URL_MAX 4096


#define VERIFY(cond) do {\
    if (!(cond)) {  \
        fprintf(stderr, "[%s] error!\n", #cond);\
        exit(EXIT_FAILURE); \
    }   \
} while (0)

event_base *base        = nullptr;
evhttp_connection *conn = nullptr;
evhttp_uri *location    = nullptr;

static evhttp_uri *uriParse(const char *);
static void onConnect(evhttp_request *, void *);
static void onGet(evhttp_request *, void *);

int main(int argc, char **argv)
{

    if (argc != 3) {
        fprintf(stderr, "Usage: %s proxy url\n", argv[0]);
        exit(EXIT_FAILURE);
    }


    char hostAndPort[URL_MAX];
    evhttp_request *req = nullptr;
    auto proxy      = uriParse(argv[1]);
    location        = uriParse(argv[2]);
  
    VERIFY(base = event_base_new());
    VERIFY(conn = evhttp_connection_base_new(base, NULL, evhttp_uri_get_host(proxy),
        evhttp_uri_get_port(proxy)));
    VERIFY(req = evhttp_request_new(onConnect, NULL));

    
    VERIFY(evhttp_uri_join(location, hostAndPort, sizeof(hostAndPort)));
    printf("evhttp_uri_join result: [%s]\n", hostAndPort);

    auto host_ = evhttp_uri_get_host(location);
    int port_ = 0;
    VERIFY(host_ != NULL);
    VERIFY((port_ = evhttp_uri_get_port((const evhttp_uri*)location)) > 0);
    evutil_snprintf(hostAndPort, sizeof(hostAndPort), "%s:%d",
        host_, port_);
    

    evhttp_add_header(req->output_headers, "Connection", "keep-alive");
    evhttp_add_header(req->output_headers, "Proxy-Connection", "keep-alive");
    evhttp_add_header(req->output_headers, "Host", hostAndPort);

    evhttp_make_request(conn, req, EVHTTP_REQ_CONNECT, hostAndPort);

    event_base_dispatch(base);

    evhttp_connection_free(conn);
    event_base_free(base);

    evhttp_uri_free(proxy);
    evhttp_uri_free(location);

    return 0;
}


static evhttp_uri *uriParse(const char *strUri)
{
    evhttp_uri *uri;

    VERIFY(uri = evhttp_uri_parse(strUri));
    VERIFY(evhttp_uri_get_host(uri));
    VERIFY(evhttp_uri_get_port(uri) > 0);

    return uri;
}

static void onConnect(evhttp_request *req, void *arg)
{
    VERIFY(req);

    auto r = evhttp_request_new(onGet, NULL);
    evhttp_add_header(req->output_headers, "Connection", "close");
    evhttp_add_header(req->output_headers, "Host", evhttp_uri_get_host(location));
    
    char buff[URL_MAX];
    VERIFY(evhttp_uri_join(location, buff, sizeof(buff)));
    auto path = evhttp_uri_parse(buff);
    evhttp_uri_set_scheme(path, NULL);
    evhttp_uri_set_userinfo(path, NULL);
    evhttp_uri_set_host(path, NULL);
    evhttp_uri_set_port(path, -1);

    VERIFY(evhttp_uri_join(path, buff, sizeof(buff)));
    
    VERIFY(!evhttp_make_request(conn, r, EVHTTP_REQ_GET, buff));
}
static void 
onGet(evhttp_request *req, void *arg)
{
    VERIFY(req);
    auto buff = evhttp_request_get_input_buffer(req);
    auto len = evbuffer_get_length(buff);
    fwrite(evbuffer_pullup(buff, len), len, 1, stdout);
    evbuffer_drain(buff, len);
}