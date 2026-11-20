// Standard C/POSIX demo built against the in-tree libc.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

static int fails;
static volatile int sig_got;

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

static volatile int counter;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static volatile int cond_ready;

static void *worker(void *arg)
{
    unsigned int n = (unsigned int)(unsigned long)arg;
    unsigned int i;
    for (i = 0; i < n; i++) {
        pthread_mutex_lock(&g_mutex);
        counter++;
        pthread_mutex_unlock(&g_mutex);
    }
    return (void *)(unsigned long)n;
}

static void *cond_worker(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_mutex);
    cond_ready = 1;
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_mutex);
    return (void *)7;
}

static void check(int ok, const char *name)
{
    if (ok) {
        printf("PASS %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        fails++;
    }
}

static void on_signal(int sig)
{
    sig_got = sig;
}

int main(int argc, char **argv)
{
    char buf[160];
    void *a, *b;
    FILE *f;
    struct stat st;
    DIR *d;
    struct dirent *de;
    int seen_readme = 0;
    int st2 = 0;
    int p[2];
    char c;
    pid_t pid;

    printf("\x1b[1;36mlibc demo\x1b[0m argc=%d argv0=%s\n", argc,
           argv ? argv[0] : "(null)");
    check(argc >= 1 && argv && strcmp(argv[0], "libctest.elf") == 0,
          "argc/argv");
    if (argc > 1 && strcmp(argv[1], "child") == 0) {
        const char *v = getenv("FOO");
        printf("child env FOO=%s\n", v ? v : "(null)");
        return (v && strcmp(v, "bar") == 0) ? 0 : 1;
    }

    snprintf(buf, sizeof(buf), "%d %u %x %s %c %05d %.3s",
             -42, 42u, 0xBEEFu, "str", 'Z', 7, "abcdef");
    check(strcmp(buf, "-42 42 beef str Z 00007 abc") == 0, "printf formats");
    check(strcmp(strerror(ENOENT), "No such file or directory") == 0,
          "strerror");

    check(strlen("hello") == 5 && strcmp(strcpy(buf, "abc"), "abc") == 0 &&
          strchr("abc", 'b') != 0 && strstr("abcdef", "cd") != 0,
          "string.h");

    a = malloc(100);
    b = calloc(16, 4);
    check(a && b, "malloc/calloc");
    if (a) {
        memset(a, 0xAA, 100);
    }
    b = realloc(b, 256);
    check(a && b && ((unsigned char *)a)[99] == 0xAA, "memset/realloc");
    free(a);
    free(b);

    f = fopen("/libcfile", "w");
    check(f != 0, "fopen write");
    if (f) {
        fprintf(f, "hello libc %d\n", 123);
        fclose(f);
    }
    f = fopen("/libcfile", "r");
    check(f != 0, "fopen read");
    if (f) {
        fgets(buf, sizeof(buf), f);
        fclose(f);
    }
    check(strcmp(buf, "hello libc 123\n") == 0, "fgets");
    check(stat("/libcfile", &st) == 0 && st.st_size == 15, "stat");
    check(unlink("/libcfile") == 0, "unlink");

    d = opendir("/system/share/doc");
    if (d) {
        while ((de = readdir(d)) != 0) {
            if (strcmp(de->d_name, "README") == 0) {
                seen_readme = 1;
            }
        }
        closedir(d);
    }
    check(seen_readme, "opendir/readdir");

    check(time(0) > 0, "time");
    {
        struct timespec ts;
        check(clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "clock_gettime");
    }

    check(pipe(p) == 0, "pipe");
    pid = fork();
    if (pid == 0) {
        close(p[0]);
        write(p[1], "x", 1);
        _exit(0);
    }
    close(p[1]);
    check(read(p[0], &c, 1) == 1 && c == 'x', "pipe read");
    close(p[0]);
    waitpid(pid, &st2, 0);
    check(st2 == 0, "fork/waitpid");

    check(signal(SIGUSR1, on_signal) != (sighandler_t)-1, "signal");
    sig_got = 0;
    raise(SIGUSR1);
    check(sig_got == SIGUSR1, "signal handler");

    {
        int sv[2];
        struct pollfd pfd;
        check(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
        write(sv[0], "hi", 2);
        pfd.fd = sv[1];
        pfd.events = POLLIN;
        pfd.revents = 0;
        check(poll(&pfd, 1, 100) == 1 && (pfd.revents & POLLIN),
              "poll socket");
        memset(buf, 0, sizeof(buf));
        check(read(sv[1], buf, 2) == 2 && strcmp(buf, "hi") == 0,
              "socket read");
        close(sv[0]);
        close(sv[1]);
    }

    {
        int sv[2];
        fd_set rf;
        struct timeval tv;
        check(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
              "socketpair select");
        write(sv[0], "z", 1);
        FD_ZERO(&rf);
        FD_SET(sv[1], &rf);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        check(select(sv[1] + 1, &rf, 0, 0, &tv) >= 1 &&
              FD_ISSET(sv[1], &rf), "select socket");
        close(sv[0]);
        close(sv[1]);
    }

    {
        int lfd, afd, cfd;
        struct sockaddr_un sa, got;
        socklen_t glen = sizeof(got);
        char cbuf[8];

        memset(&sa, 0, sizeof(sa));
        sa.sun_family = AF_UNIX;
        strcpy(sa.sun_path, "/libcsock");
        lfd = socket(AF_UNIX, SOCK_STREAM, 0);
        check(lfd >= 0, "socket");
        check(bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) == 0, "bind");
        check(listen(lfd, 4) == 0, "listen");

        pid = fork();
        if (pid == 0) {
            cfd = socket(AF_UNIX, SOCK_STREAM, 0);
            connect(cfd, (struct sockaddr *)&sa, sizeof(sa));
            write(cfd, "hello", 5);
            close(cfd);
            _exit(0);
        }
        afd = accept(lfd, 0, 0);
        check(afd >= 0, "accept");
        memset(cbuf, 0, sizeof(cbuf));
        if (afd >= 0) {
            read(afd, cbuf, 5);
        }
        waitpid(pid, &st2, 0);
        check(strcmp(cbuf, "hello") == 0, "local socket data");
        check(getsockname(afd, (struct sockaddr *)&got, &glen) == 0 &&
              strcmp(got.sun_path, "/libcsock") == 0, "getsockname");
        if (afd >= 0) {
            close(afd);
        }
        close(lfd);
    }

    {
        int pp[2];
        char nc;
        pipe(pp);
        fcntl(pp[0], F_SETFL, O_NONBLOCK);
        check(read(pp[0], &nc, 1) < 0 && errno == EAGAIN,
              "O_NONBLOCK read");
        close(pp[0]);
        close(pp[1]);
    }

    {
        int lfd, afd, cfd;
        struct sockaddr_in sa, ga, pa;
        socklen_t gl, pl;
        char ib[8];

        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(8080);
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        lfd = socket(AF_INET, SOCK_STREAM, 0);
        check(lfd >= 0, "inet socket");
        check(bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) == 0,
              "inet bind");
        check(listen(lfd, 4) == 0, "inet listen");

        pid = fork();
        if (pid == 0) {
            cfd = socket(AF_INET, SOCK_STREAM, 0);
            connect(cfd, (struct sockaddr *)&sa, sizeof(sa));
            write(cfd, "inet", 4);
            close(cfd);
            _exit(0);
        }
        afd = accept(lfd, 0, 0);
        check(afd >= 0, "inet accept");
        gl = sizeof(ga);
        pl = sizeof(pa);
        check(getsockname(afd, (struct sockaddr *)&ga, &gl) == 0 &&
              ntohs(ga.sin_port) == 8080 &&
              ntohl(ga.sin_addr.s_addr) == INADDR_LOOPBACK,
              "inet getsockname");
        check(getpeername(afd, (struct sockaddr *)&pa, &pl) == 0 &&
              ntohs(pa.sin_port) != 0 &&
              ntohl(pa.sin_addr.s_addr) == INADDR_LOOPBACK,
              "inet getpeername");
        memset(ib, 0, sizeof(ib));
        if (afd >= 0) {
            read(afd, ib, 4);
        }
        waitpid(pid, &st2, 0);
        check(strcmp(ib, "inet") == 0, "inet loopback data");
        if (afd >= 0) {
            close(afd);
        }
        close(lfd);
    }

    {
        int u1, u2;
        struct sockaddr_in a, src;
        socklen_t sl;
        char ub[8];
        struct pollfd pfd;

        u1 = socket(AF_INET, SOCK_DGRAM, 0);
        u2 = socket(AF_INET, SOCK_DGRAM, 0);
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons(9090);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(u1 >= 0 && u2 >= 0, "udp socket");
        check(bind(u1, (struct sockaddr *)&a, sizeof(a)) == 0, "udp bind");
        check(sendto(u2, "udp", 3, 0, (struct sockaddr *)&a,
                     sizeof(a)) == 3, "udp sendto");
        pfd.fd = u1;
        pfd.events = POLLIN;
        pfd.revents = 0;
        check(poll(&pfd, 1, 100) == 1 && (pfd.revents & POLLIN),
              "udp poll");
        memset(ub, 0, sizeof(ub));
        sl = sizeof(src);
        check(recvfrom(u1, ub, sizeof(ub), 0, (struct sockaddr *)&src,
                       &sl) == 3 && strcmp(ub, "udp") == 0 &&
              ntohs(src.sin_port) != 0, "udp recvfrom");
        close(u1);
        close(u2);
    }

    {
        struct in_addr ina;
        ina.s_addr = inet_addr("127.0.0.1");
        check(ntohl(ina.s_addr) == INADDR_LOOPBACK &&
              strcmp(inet_ntoa(ina), "127.0.0.1") == 0, "inet helpers");
    }

    {
        int nums[6] = { 5, 1, 4, 2, 6, 3 };
        char *end = 0;
        check(getenv("P7_NO_SUCH") == 0, "getenv missing");
        check(setenv("FOO", "bar", 1) == 0 &&
              getenv("FOO") && strcmp(getenv("FOO"), "bar") == 0,
              "setenv/getenv");
        check(unsetenv("FOO") == 0 && getenv("FOO") == 0, "unsetenv");
        qsort(nums, 6, sizeof(int), cmp_int);
        check(nums[0] == 1 && nums[5] == 6, "qsort");
        check(strtol("0x2a", &end, 0) == 42 && end && *end == 0,
              "strtol");
        srand(7);
        check(rand() >= 0, "rand");
    }

    {
        char *args[3];
        char *envs[2];
        args[0] = (char *)"libctest.elf";
        args[1] = (char *)"child";
        args[2] = 0;
        envs[0] = (char *)"FOO=bar";
        envs[1] = 0;
        pid = fork();
        if (pid == 0) {
            execve("/system/bin/libctest.elf", args, envs);
            _exit(127);
        }
        waitpid(pid, &st2, 0);
        check(st2 == 0, "execve envp");
    }

    {
        pthread_t th[4];
        void *rv = 0;
        int i;
        counter = 0;
        for (i = 0; i < 4; i++) {
            check(pthread_create(&th[i], 0, worker,
                                 (void *)(unsigned long)200) == 0,
                  "pthread_create");
        }
        for (i = 0; i < 4; i++) {
            pthread_join(th[i], &rv);
        }
        check(counter == 800, "pthread mutex");
        check((unsigned long)rv == 200, "pthread join value");
        check(pthread_equal(pthread_self(), pthread_self()) &&
              !pthread_equal(pthread_self(), th[0]), "pthread_self");
    }

    {
        pthread_t th;
        cond_ready = 0;
        check(pthread_create(&th, 0, cond_worker, 0) == 0,
              "pthread cond create");
        pthread_mutex_lock(&g_mutex);
        while (!cond_ready) {
            pthread_cond_wait(&g_cond, &g_mutex);
        }
        pthread_mutex_unlock(&g_mutex);
        {
            void *rv = 0;
            pthread_join(th, &rv);
            check(cond_ready == 1 && (unsigned long)rv == 7,
                  "pthread condvar");
        }
    }

    if (fails == 0) {
        printf("\x1b[32mlibc demo ok\x1b[0m\n");
    } else {
        printf("\x1b[31mlibc demo failed (%d)\x1b[0m\n", fails);
    }
    return fails ? 1 : 0;
}
