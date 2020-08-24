#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>


int main(int argc, char **argv)
{
    sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(9999),
        .sin_addr = {
            .s_addr = htonl(INADDR_ANY),
        },
    };

    char sndBuff[1024], rcvBuff[1024];

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1) {
        perror("socket");
        exit(-1);
    }

    time_t now = time(NULL);
    if (argc > 1)
    {
        if(::bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            exit(-1);
        }

        printf("UDP Server runing at [9999]...\n");

        sockaddr_in cli;
        socklen_t len = sizeof(cli);
        int n = recvfrom(sock, rcvBuff, sizeof(rcvBuff), 0, (sockaddr*)&cli, &len);
        if (n < 0) {
            perror("recvfrom");
            exit(-1);
        }

        printf("recv [%s] from client!\n", rcvBuff);

        sprintf(sndBuff, "Hi! %s", ctime(&now));
        n = sendto(sock, sndBuff, strlen(sndBuff), 0, (sockaddr*)&cli, len);
        if (n < 0) {
            perror("sendto");
            exit(-1);
        }
    } else {
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        sprintf(sndBuff, "Hello! It's %s now!", ctime(&now));
        int n = sendto(sock, sndBuff, strlen(sndBuff), 0, (sockaddr*)&addr, sizeof(addr));
        if (n < 0) {
            perror("sendto");
            exit(-1);
        }

        n = recvfrom(sock, rcvBuff, sizeof(rcvBuff), 0, NULL, NULL);
        if (n < 0) {
            perror("recvfrom");
            exit(-1);
        }

        printf("recv [%s] from Server!\n", rcvBuff);
    }
    return 0;
}