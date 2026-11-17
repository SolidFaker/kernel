// Real network UDP echo test: guest 10.0.2.15 -> host alias 10.0.2.2:9999.
// Start a host UDP echo server on 127.0.0.1:9999 before running this.

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>

static int udp_test(void)
{
    int fd;
    struct sockaddr_in to, from;
    socklen_t fl;
    struct pollfd pfd;
    char buf[64];
    const char *msg = "hello-udp";
    int n;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        printf("FAIL socket: %d\n", errno);
        return 1;
    }
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(9999);
    to.sin_addr.s_addr = htonl(0x0A000202);   /* 10.0.2.2 */

    if (sendto(fd, msg, 9, 0, (struct sockaddr *)&to, sizeof(to)) != 9) {
        printf("FAIL sendto: %d\n", errno);
        close(fd);
        return 1;
    }
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, 1500) != 1) {
        printf("FAIL poll timeout (host UDP echo on 127.0.0.1:9999?)\n");
        close(fd);
        return 1;
    }
    fl = sizeof(from);
    n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
    if (n == 9 && memcmp(buf, msg, 9) == 0) {
        printf("\x1b[32mPASS\x1b[0m udp echo from %s:%u\n",
               inet_ntoa(from.sin_addr), ntohs(from.sin_port));
        close(fd);
        return 0;
    }
    printf("FAIL recvfrom n=%d\n", n);
    close(fd);
    return 1;
}

static int tcp_test(void)
{
    int fd;
    struct sockaddr_in to;
    struct pollfd pfd;
    char buf[64];
    int n;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("FAIL tcp socket: %d\n", errno);
        return 1;
    }
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(9998);
    to.sin_addr.s_addr = htonl(0x0A000202);
    if (connect(fd, (struct sockaddr *)&to, sizeof(to)) < 0) {
        printf("FAIL tcp connect: %d (host TCP echo on 127.0.0.1:9998?)\n",
               errno);
        close(fd);
        return 1;
    }
    if (send(fd, "hello-tcp", 9, 0) != 9) {
        printf("FAIL tcp send: %d\n", errno);
        close(fd);
        return 1;
    }
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, 1500) != 1) {
        printf("FAIL tcp poll timeout\n");
        close(fd);
        return 1;
    }
    n = recv(fd, buf, sizeof(buf), 0);
    if (n == 9 && memcmp(buf, "hello-tcp", 9) == 0) {
        printf("\x1b[32mPASS\x1b[0m tcp echo\n");
        shutdown(fd, SHUT_WR);
        close(fd);
        return 0;
    }
    printf("FAIL tcp recv n=%d\n", n);
    close(fd);
    return 1;
}

int main(void)
{
    int a = udp_test();
    int b = tcp_test();
    return (a == 0 && b == 0) ? 0 : 1;
}

