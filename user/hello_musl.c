#include <stdio.h>
#include <unistd.h>
int main(int argc, char **argv) {
    printf("hello from musl argc=%d argv0=%s\n", argc, argv[0]);
    write(1, "write ok\n", 9);
    return 0;
}
