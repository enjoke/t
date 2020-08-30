#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <memory>
#include <functional>
#include <string>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <event2/event.h>
#include <event2/dns.h>
#include <event2/dns_struct.h>
#include <event2/util.h>

using evBase    = std::shared_ptr<event_base>;
using dnsBase   = std::shared_ptr<evdns_base>;
using sock      = std::shared_ptr<evutil_socket_t>;

static int verbose = 0;
const int PORT = 10053;
evutil_socket_t fd = -1;
struct options {
    int reverse;
    int use_getaddrinfo;
    int servtest;
    char *resolv_conf;
    char *ns; 

    options() = default;
    ~options() = default;
    options(options &&rhs) : reverse(rhs.reverse),
    use_getaddrinfo(rhs.use_getaddrinfo), servtest(rhs.servtest),
    resolv_conf(rhs.resolv_conf), ns(rhs.ns) {
        rhs.resolv_conf = nullptr;
        rhs.ns = nullptr;
    }
};
static void usage(const char *program);
static options resolvOpt(int argc, char **argv);
static int setupSrv(evBase base);
static void dnsCallback(int result, char type, int count, int ttl,
    void *addrs, void *ori);
static void addrCallback(int err, evutil_addrinfo *, void *);
static void dnsSrvCallback(evdns_server_request *req, void *data);
int main(int argc, char **argv)
{
    if (argc < 2)
        usage(argv[0]);

    options opt = resolvOpt(argc, argv);

    auto base = evBase(event_base_new(), event_base_free);
    auto dns = dnsBase(evdns_base_new(base.get(), EVDNS_BASE_DISABLE_WHEN_INACTIVE),
        bind(evdns_base_free, std::placeholders::_1, 1));

    evdns_set_log_fn([](int warn, const char *msg) {
        if (!warn && !verbose)  return;
        fprintf(stderr, "%s: %s\n", warn ? "WARN" : "INFO", msg);
    });

    if (opt.servtest && 
        setupSrv(base)) {
        goto __release__;
    }

    if (optind < argc) {
        int res = opt.ns ? 
            evdns_base_nameserver_ip_add(dns.get(), opt.ns) :
            evdns_base_resolv_conf_parse(dns.get(),
                DNS_OPTION_NAMESERVERS, opt.resolv_conf);
        if (res) {
            fprintf(stderr, "Couldn't configure nameservers\n");
            goto __release__;
        }
    }

    printf("EVUTIL_AI_CANONNAME: %d\n", EVUTIL_AI_CANONNAME);
    for (; optind < argc; ++optind) {
        if (opt.reverse) {
            in_addr addr;
            if (evutil_inet_pton(AF_INET, argv[optind], &addr) != 1) {
                fprintf(stderr, "SKipping non-IP %s\n", argv[optind]);
                continue;
            }
            fprintf(stderr, "resolving %s...\n", argv[optind]);
            evdns_base_resolve_reverse(dns.get(), &addr, 0, dnsCallback, (void*)argv[optind]);
        } else if (opt.use_getaddrinfo) {
            evutil_addrinfo hints   = {
                .ai_flags           = EVUTIL_AI_CANONNAME, 
                .ai_family          = PF_UNSPEC,
                .ai_protocol        = IPPROTO_TCP,
            };

            fprintf(stderr, "resolving (fwd) %s...\n", argv[optind]);
            evdns_getaddrinfo(dns.get(), argv[optind], NULL, &hints,
                addrCallback, (void*)argv[optind]);   
        } else {
            fprintf(stderr, "resolving (fwd) %s...\n", argv[optind]);
            evdns_base_resolve_ipv4(dns.get(), argv[optind], 0, dnsCallback, 
                (void*)argv[optind]);
        }
    }

    fflush(stdout);
    event_base_dispatch(base.get());
__release__:
    if (fd != -1) evutil_closesocket(fd);

    return 0;
}

static void usage(const char *program)
{
    
    fprintf(stderr, "Usage: %s [-x] [-v] [-c resolv.conf] [-s ns] hostname\n",
        program);
    fprintf(stderr, "%s [-T]\n", program);
    exit(EXIT_FAILURE);
}
static options resolvOpt(int argc, char **argv)
{
    options opts;
    int opt;
    while ((opt = getopt(argc, argv, "xvc:Ts:g")) != -1) {
        switch (opt) {
            case 'x':   opts.reverse = 1;break;
            case 'v':   ++verbose; break;
            case 'g':   opts.use_getaddrinfo = 1; break;
            case 'T':   opts.servtest = 1;break;
            case 'c':   opts.resolv_conf = optarg;break;
            case 's':   opts.ns = optarg;break;
            default :
                fprintf(stderr,"Unknown options %c\n", opt);
                usage(argv[0]);
        }
    }

    return opts;
}

static int 
setupSrv(evBase base)
{
    fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        perror("socket");
        return -1;
    }

    evutil_make_socket_nonblocking(fd);
    sockaddr_in sin = {
        .sin_family = AF_INET,
        .sin_port   = htons(PORT),
        .sin_addr = {
            .s_addr = INADDR_ANY,
        },
    };

    if (bind(fd, (sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return -1;
    }

    evdns_add_server_port_with_base(base.get(), fd, 0, dnsSrvCallback, NULL);
    return 0;
}

static void dnsCallback(int result, char type, int count, int ttl,
    void *addrs, void *ori)
{
    char *n = (char*)ori;
    auto printIp = [](ev_uint32_t addr)->char*{
                static char buf[32];
                auto a = ntohl(addr);
                evutil_snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                                    (int)(ev_uint8_t)((a >> 24) & 0xff),
                                    (int)(ev_uint8_t)((a >> 16) & 0xff),
                                    (int)(ev_uint8_t)((a >>  8) & 0xff),
                                    (int)(ev_uint8_t)((a      ) & 0xfff)
                                    );
                return buf;
            };
    for (int i=0; i < count; ++i) {
        if (type == DNS_IPv4_A) {
            ev_uint32_t *p = (ev_uint32_t *)addrs;
            printf("%s: %s\n", n, printIp(p[i]));
        } else if (type == DNS_PTR) {
            printf("%s: %s\n", n, ((char**)addrs)[i]);
        }
    }

    if (!count) {
        printf("%s: No answer (%d)\n", n, result);
    }
    fflush(stdout);
}

static void 
dnsSrvCallback(evdns_server_request *req, void *data)
{
    (void)data;

    int reply;
    for (int i = 0; i < req->nquestions; ++i) {
        ev_uint32_t ans = htonl(0xc0a80b0bUL);
        if (req->questions[i]->type == EVDNS_TYPE_A && 
            req->questions[i]->dns_question_class == EVDNS_CLASS_INET) {
            printf(" -- replying for %s (A)\n", req->questions[i]->name);
            reply = evdns_server_request_add_a_reply(req, req->questions[i]->name, 
                1, &ans, 10);
            if (reply < 0) {
                printf("eeep, didn't work.\n");
            }     
        } else if (req->questions[i]->type == EVDNS_TYPE_PTR &&
            req->questions[i]->dns_question_class == EVDNS_CLASS_INET) {
            printf(" -- replying for %s (PTR)\n", req->questions[i]->name);
            reply = evdns_server_request_add_ptr_reply(req, NULL, req->questions[i]->name,
                "foo.bar.example.com", 10);
            if (reply < 0) {
                printf("ugh, no luck.\n");
            }   
        } else {
            printf(" -- skipping %s [%d %d]\n", req->questions[i]->name, 
            req->questions[i]->type, req->questions[i]->dns_question_class);
        }
    }

    reply = evdns_server_request_respond(req, 0);
    if (reply < 0) {
        printf("eeek, couldn't send reply\n");
    }
}

static void addrCallback(int err, evutil_addrinfo *addr, void *data)
{
    const char *name = (const char*)data;
    if (err) {
        printf("%s: %s\n", name, evutil_gai_strerror(err));
    }

    if (addr && addr->ai_canonname) {
        printf("%s ==> %s\n", name, addr->ai_canonname);
    }
   
    auto first = addr;
    for (int i=0; addr; addr = addr->ai_next, ++i) {
        char buf[128];
        if (addr->ai_family == PF_INET) {
            sockaddr_in *sin = (sockaddr_in*) addr->ai_addr;
            evutil_inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
            printf("[%d] %s: %s\n", i, name, buf);
        } else {
            sockaddr_in6 *sin = (sockaddr_in6*) addr->ai_addr;
            evutil_inet_ntop(AF_INET6, &sin->sin6_addr, buf, sizeof(buf)); 
        }
        printf("[%d] %s: %s\n", i, name, buf);
    }
    
    if (first) {
        evutil_freeaddrinfo(first);
    }
}