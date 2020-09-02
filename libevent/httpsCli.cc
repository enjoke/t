#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/http.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

static int ignoreCert = 0;

static void onRequestDone(evhttp_request*, void*);
static void usage(char *);

static void 
onRequestDone(evhttp_request *req, void *arg)
{
	char buffer[256];
	int nread;
	if (!req || !evhttp_request_get_reponse_code(req)) {
		bufferevent *bev = (bufferevent*) arg;
		unsigned long oslerr;
		int printErr = 0;
		int errCode = EVUTIL_SOCKET_ERROR();
		while ((oslerr = bufferevent_get_openssl_error(bev))) {
			ERR_error_string_n(oslerr, buffer, sizeof(buffer));
			fprintf(stderr, "%s\n", buffer);
			printErr = 1;
		}

		if (!printErr) 
			fprintf(stderr, "socket error = %s (%d)\n", evutil_socket_error_to_string(errCode), errCode);
		return;
	}

	fprintf(stdout, "Response line: %d %s\n",
	evhttp_request_get_response_code(req),
	evhttp_request_get_response_code_line(req));
		
	while ((nread = evbuffer_remove(evhttp_request_get_input_buffer(req), buffer, sizeof(buffer)) > 0) {
			fwrite(buffer, nread, 1, stdout);
	}
}

static void 
usage(char *prog)
{
	fprintf(stderr,"Usage: %s -url <https-url> [-data data-file.bin] [-ignore-cert] [-retries num] [-timeout sec] [-crt crt]\n",
			prog);
	fprintf(stderr "Example: %s -url https://ip.appspot.com/\n", prog);
	exit(EXIT_FAILURE);
}


