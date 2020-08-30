#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <event2/event.h>
#include <event2/http.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>
#include <event2/thread.h>


char uriRoot[512];

static const struct tableEntry{
		const char *extension;
		const char *contentType;
} contentTypeTbl[] = {
		{ "txt", "text/plain" },
		{ "c", "text/plain" },
		{ "h", "text/plain" },
		{ "html", "text/html" },
		{ "htm", "text/htm" },
		{ "css", "text/css" },
		{ "gif", "image/gif" },
		{ "jpg", "image/jpeg" },
		{ "png", "image/png" },
		{ "pdf", "application/pdf" },
		{ "ps", "application/postcript" },
		{ NULL, NULL},
};

#define UNKNOWN_CONTENT_TYPE "application/misc"
struct options {
		int port;
		int iocp;
		int verbose;

		int unlink;
		const char *unixSock;
		const char *docRoot;
};


static const char *guessContentType(const char *);
static void onRequest(evhttp_request *, void *);
static void onSend(evhttp_request *, void *);
static void onTerm(int, short, void *);
static void usage(FILE *, const char *, int);
static options parseOpts(int, char **);
static int displayDetail(evhttp_bound_socket*);

int 
main(int argc, char **argv)
{
    event_config    *cfg    = nullptr;
    event_base      *base   = nullptr;
    evhttp          *http   = nullptr;
    evhttp_bound_socket *handle = nullptr;
    evconnlistener  *lstner = nullptr;
    event           *evTerm = nullptr;
    int ret                 = 0;

    auto o = parseOpts(argc, argv);

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        ret = 1;
        goto err;
    }

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (o.verbose || getenv("EVENT_DEBUG_LOGGING_ALL"))
        event_enable_debug_logging(EVENT_DBG_ALL);

    cfg = event_config_new();
    assert(cfg);

    base = event_base_new_with_config(cfg);
    assert(base);

    event_config_free(cfg);

    http = evhttp_new(base);
    assert(http);

    evhttp_set_cb(http, "/dump", onRequest, NULL);
    evhttp_set_gencb(http, onSend, &o);

    if (o.unixSock) {
        sockaddr_un addr;

        if (o.unlink && (unlink(o.unixSock) && errno != ENOENT)) {
            perror(o.unixSock);
            ret = 1;
            goto err;
        }

        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path, o.unixSock);

        lstner = evconnlistener_new_bind(base, NULL, NULL, LEV_OPT_CLOSE_ON_FREE,
                -1, (sockaddr*)&addr, sizeof(addr));
        assert(lstner);

        handle = evhttp_bind_listener(http, lstner);
        assert(handle);

    } else {
        handle = evhttp_bind_socket_with_handle(http, "0.0.0.0", o.port);
        assert(handle);

    };

    assert(!displayDetail(handle));

    evTerm = evsignal_new(base, SIGINT, onTerm, base);
    assert(evTerm);

    event_base_dispatch(base);

err:
    if (cfg) event_config_free(cfg);
    if (http) evhttp_free(http);
    if (evTerm) event_free(evTerm);
    if (base) event_base_free(base);

    return ret;
}



static const char *
guessContentType(const char *path)
{

		auto lastPeriod = strchr(path, '.');
		if (!lastPeriod || strchr(lastPeriod, '/')) {
				return UNKNOWN_CONTENT_TYPE;
		}

		auto extension = lastPeriod + 1;
		for (auto &entry : contentTypeTbl) {
				if (!evutil_ascii_strcasecmp(entry.extension, extension)) 
						return extension;
		}

		return UNKNOWN_CONTENT_TYPE;
}



static void 
onRequest(evhttp_request *req, void *arg)
{
    (void)arg;
	const char *cmdType;
	switch (evhttp_request_get_command(req)) {
			case EVHTTP_REQ_GET: cmdType = "GET"; break;
			case EVHTTP_REQ_POST: cmdType = "POST"; break;
			case EVHTTP_REQ_HEAD: cmdType = "HEAD"; break;
			case EVHTTP_REQ_PUT: cmdType = "PUT"; break;
			case EVHTTP_REQ_DELETE: cmdType = "DELETE"; break;
			case EVHTTP_REQ_OPTIONS: cmdType = "OPTIONS"; break;
            case EVHTTP_REQ_TRACE: cmdType = "TRACE"; break;
            case EVHTTP_REQ_CONNECT: cmdType = "CONNECT"; break;
            case EVHTTP_REQ_PATCH: cmdType = "PATCH"; break;
            default: cmdType = "unknown"; break;
    }

    printf("Received a %s request for %s\nHeaders:\n", cmdType, evhttp_request_get_uri(req));


    auto headers = evhttp_request_get_input_headers(req);
    for (auto &header = headers->tqh_first; header; 
            header = header->next.tqe_next) 
        printf("  %s: %s\n", header->key, header->value);

    auto buf = evhttp_request_get_input_buffer(req);
    printf("Input data: <<<\n");

    while (evbuffer_get_length(buf)) {
        char data[128];
        int n = evbuffer_remove(buf, data, sizeof(data));
        if (n > 0)  fwrite(data, 1, n, stdout);
    }

    printf(">>>\n");

    evhttp_send_reply(req, 200, "OK", NULL);
}

static void 
onSend(evhttp_request *req, void *arg)
{
    options *o = static_cast<options*>(arg);
    int fd;
    char *wholePath = nullptr;
    evbuffer *buf = nullptr;
    size_t len = 0;
    if (evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
        onRequest(req, arg);
        return;
    }
    auto uri = evhttp_request_get_uri(req);
    printf("Got a GET request for .%s>\n", uri);
    auto decode = evhttp_uri_parse(uri);
    if (!decode) {
        printf("It's not a good URI. Sending BADREQUEST\n");
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        return;
    }
    auto path = evhttp_uri_get_path(decode);
    if (!path) path = "/";
    auto decodePath = evhttp_uridecode(path, 0, NULL);
    if (decodePath == NULL) 
        goto err;
    if (strstr(decodePath, ".."))
        goto err;
    len = strlen(decodePath) + strlen(o->docRoot) + 2;
    wholePath  =  new char[len];
    if (!wholePath) {
        perror("new");
        goto err;
    }
    evutil_snprintf(wholePath, len, "%s/%s", o->docRoot, decodePath);
    struct stat st;
    if (stat(wholePath, &st) < 0 )
        goto err;
    buf = evbuffer_new();
    if (S_ISDIR(st.st_mode)) {
       const char *trailingSlash = "";
       dirent *ent;
       if (!strlen(path) || path[strlen(path)-1] != '/')
           trailingSlash = "/";
       auto d = opendir(wholePath);
       if (!d)  
           goto err;
       evbuffer_add_printf(buf, 
               "<!DOCTYPE html>\n"
               "<html>\n <head>\n"
               " <meta charset='utf-8'>\n"
               "  <title>%s</title>\n"
               "  <base href='%s%s'>\n"
               "</head>\n"
               "<body>\n"
               "<h1>%s</h1>\n"
               "<ul>\n",
               decodePath, 
               path, 
               trailingSlash,
               decodePath);
       while ((ent = readdir(d))){
           auto name = ent->d_name;
           evbuffer_add_printf(buf,
                   "    <li><a href=\"%s\">%s</a>\n",
                   name, name);
       }
       evbuffer_add_printf(buf, "</url></body></html>\n");
       closedir(d);
       evhttp_add_header(evhttp_request_get_output_headers(req),
               "Content-Type", "text/html");
    } else {
        auto type = guessContentType(decodePath);
        fd = open(wholePath, O_RDONLY);
        if (fd < 0) {
            perror("open");
            goto err;
        }
        if (fstat(fd, &st) < 0) {
            perror("fstat");
            goto err;
        }
        evhttp_add_header(evhttp_request_get_output_headers(req),
                "Content-Type", type);
        evbuffer_add_file(buf, fd, 0, st.st_size);
    }
    goto done;
err:
    evhttp_send_error(req, HTTP_NOTFOUND, NULL);
    if (fd >= 0) close(fd);
done:
    if (decode) evhttp_uri_free(decode);
    if (decodePath) delete[] decodePath;
    if (wholePath) delete[] wholePath;
    if (buf)    evbuffer_free(buf);
}


static void 
usage(FILE *handle, const char *progName, int exitCode)
{
    fprintf(handle,
            "Usage: %s <docroot>\n"
            " -p        - port\n"
            " -U        - bind to unix socket\n"
            " -u        - unlink unix socket before bind\n"
            " -I        - IOCP\n"
            " -v        - verbosity, enables libevent debug logging too\n",
            progName);
    exit(exitCode);
}

static options 
parseOpts(int c, char **v)
{
    options o;
    int opt;

    while((opt = getopt(c, v, "hp:U:uIv")) != -1) {
        switch (opt) {
            case 'p': o.port=atoi(optarg); break;
            case 'U': o.unixSock =optarg; break;
            case 'u': o.unlink = 1; break;
            case 'I': o.iocp = 1; break;
            case 'v': ++o.verbose; break;
            case 'h': usage(stdout, v[0], 0); break;
            default: 
                {
                    fprintf(stderr, "Unknown option %c\n", opt);
                    usage(stderr, v[0], -1);
                    break;
                }
        }
    }

    if (optind >= c || (c - optind) > 1) {
        usage(stdout, v[0], 1);
    }

    o.docRoot = v[optind];
    return o;
}
                    
static void 
onTerm(int sigNo, short event, void *arg)
{
    (void) event;
    event_base_loopbreak(static_cast<event_base*>(arg));
    fprintf(stderr, "Got %i, Terminating...\n", sigNo);
}
static int 
displayDetail(evhttp_bound_socket *handle)
{
    assert(handle);

    sockaddr_storage addr;
    ev_socklen_t socklen = sizeof(addr);
    int port;
    void *sinAddr;
    auto fd = evhttp_bound_socket_get_fd(handle);
    memset(&addr, 0, socklen);

    if (getsockname(fd, (sockaddr*)&addr, &socklen)) {
        perror("getsockname");
        return 1;
    }

    if (addr.ss_family == AF_INET) {
        port = ntohs(((sockaddr_in*)&addr)->sin_port);
        sinAddr = &((sockaddr_in*)&addr)->sin_addr;
    } else if (addr.ss_family == AF_INET6) {
        port = ntohs(((sockaddr_in6*)&addr)->sin6_port);
        sinAddr = &((sockaddr_in6*)&addr)->sin6_addr;
    } else if (addr.ss_family == AF_UNIX) {
        printf("Listening on <%s> \n", ((struct sockaddr_un*)&addr)->sun_path);
        return 0;
    } else {
        fprintf(stderr, "Weird address family %d\n", addr.ss_family);
        return 1;
    }

    char buf[128];
    auto strAddr = evutil_inet_ntop(addr.ss_family, sinAddr, buf, sizeof(buf));
    if (strAddr) {
        printf("Listening on %s:%d\n", strAddr, port);
        evutil_snprintf(uriRoot, sizeof(uriRoot), "http://%s:%d", strAddr, port);
    } else {
        fprintf(stderr, "evutil_inet_ntop failed!\n");
        return 1;
    }
    return 0;
}
