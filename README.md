# A-MINI-KERNEL

## make

```
make all     # 交叉工具链默认 i686-elf-*，可用 CC=/AS=/LD= 覆盖
make test    # 用 QEMU 启动 80m.img（带 e1000 用户态网络）
```

macOS 安装工具链：`brew install i686-elf-gcc qemu`

## musl + toybox

- 默认 shell 是 **toybox sh**（静态链接 musl 1.2.5，i386）
- 内核新增 Linux i386 syscall ABI 兼容层（OSABI=0xff）：`set_thread_area`/TLS、
  `mmap2`、`statx`、`getdents64`、`openat`、`fork`/`clone`、musl 信号 restorer 等
- 构建依赖（可用变量覆盖）：
  - `MUSL_DIR=/tmp/musl-install`（i386 musl 静态库 + 头文件）
  - `TOYBOX_BIN=/tmp/toybox-master/toybox`（toybox multicall，启用 sh/ls/cat/echo/pwd 等）
- `user/hello_musl.elf` 是 musl hello world；`user/toyrun.c` 可手动启动 toybox
- toybox 当前编入的 applet：`sh echo pwd ls cat true false mkdir rmdir rm touch
  sleep env printf uname test`；其余可通过 `TOYBOX_BIN` 重新生成

## terminal

启动时 16 位 loader 通过 VBE 设置 1280x1024x32 线性 framebuffer，内核把它映射到
`0xD0000000`，由 `drivers/fbcon.c` 渲染一个图形终端：

- 圆角窗口、标题栏、红绿灯按钮和底部状态栏（tty / 尺寸 / 运行时间）
- 76x56 字符网格，8x8 字体 2x 放大，16 色调色板（Catppuccin 风格）
- 解析 ANSI SGR：shell 提示符、帮助标题、错误和退出状态都有颜色
- 软件光标闪烁；`Alt+1..4` 切换 tty，每个 tty 有独立的 76x56 字符缓冲区
- 文本写入仍然通过 `NR_write`，图形终端对用户程序透明

## graphics

`drivers/graphic.c` 提供逻辑 320x200x256 画布：像素、直线、矩形、圆、8x8 文本
和 `GFX_RGB()` 3-3-2 调色板。它会以 4x 缩放到 framebuffer 上，退出时重绘终端。
用户态通过 `NR_gfx` 系统调用（`include/gfx.h`）访问：

```
gfx            # 或 run gfx.elf，按任意键返回终端
```

## posix

POSIX 兼容层按阶段推进，阶段 1+2+3+4+5+6+7+8 已实现：

- 共享 `open file description`（dup/dup2/fcntl 共享 offset/append），fork 继承 fd，
  exit 关闭 fd
- `pipe`（阻塞读写、EOF/EPIPE）、`dup`/`dup2`、`fcntl`
- `getppid`、`isatty`、`uname`、`clock_gettime`、`gettimeofday`
- `chdir`/`getcwd`、路径解析、`stat`、`access`、`waitpid(WNOHANG)`
- 可写 ramfs：启动时把只读 myfs 导入内存，支持 `O_CREAT`/`O_EXCL`/`O_TRUNC`/
  `O_APPEND`，`mkdir`/`rmdir`/`unlink`/`rename`/`truncate`/`ftruncate`
- `mmap`/`munmap`/`mprotect`/`msync`/`madvise`：匿名映射和文件私有映射，
  映射页会随 fork 复制、随进程退出回收
- `termios`/`ioctl`：`TCGETS`/`TCSETS`、`TIOCGWINSZ`、`FIOCLEX`/`FIONCLEX`，
  tty 保存每终端 termios 和 76x56 窗口尺寸
- 信号：`kill`/`raise`/`sigaction`/`sigprocmask`/`sigreturn`/`sigpending`/
  `alarm`/`pause`，用户态 handler + restorer，SIG_IGN、阻塞掩码、默认终止动作
- 进程组/作业控制：`setpgid`/`getpgid`/`getpgrp`/`setsid`、
  `tcgetpgrp`/`tcsetpgrp`，退出时向父进程发送 SIGCHLD
- 错误码使用负 errno（`include/errno.h`）
- AF_UNIX 流式 socket：`socket`/`socketpair`/`bind`/`listen`/`accept`/`connect`、
  `shutdown`、`getsockname`/`getpeername`、`setsockopt`/`getsockopt`
- AF_INET 回环（127.0.0.1）：TCP 风格 `bind`/`listen`/`accept`/`connect` 和
  UDP `sendto`/`recvfrom`，源端口临时分配；libc 提供 `htons`/`htonl`、
  `inet_addr`/`inet_ntoa`、`gethostbyname("localhost")`
- `poll`/`select` 就绪等待（tty、pipe、socket、普通文件），`O_NONBLOCK` 非阻塞读写
- pthreads：`clone` 式 `thread_create`/`thread_exit`/`thread_wait`，共享 mm/fd，
  mm 引用计数；libc 提供 `pthread_create/join/exit/self`、mutex、condvar、
  futex wait/wake
- 环境变量：`execve` 真正传递 `envp`，crt0 设置 `environ`，libc 支持
  `getenv/setenv/unsetenv/putenv`
- 真实网卡：QEMU e1000（PCI + MMIO + DMA ring），ARP/IPv4/ICMP echo，
  guest `10.0.2.15/24`、gateway `10.0.2.2`
- 真实 UDP：AF_INET `SOCK_DGRAM` 的 `sendto`/`recvfrom` 走 e1000，UDP 包分发到
  绑定的 socket；`net` 命令向主机别名 `10.0.2.2:9999` 做 UDP echo 自测
- 真实 TCP 客户端：三次握手、seq/ack、PSH 数据、ACK、FIN，`connect`/`send`/`recv`/
  `shutdown` 走 e1000；`net` 同时做 `10.0.2.2:9998` TCP echo 自测
- 轻量 libc（`user/libc/`）：crt0、系统调用封装、errno、malloc/calloc/realloc/free、
  string.h（含 strtok/strspn/strpbrk）、stdio.h、dirent.h、time.h、signal.h、
  termios.h、pthread.h、ctype.h、stdint.h、limits.h、assert.h、sys/resource.h、
  sys/socket.h、poll.h、sys/select.h、qsort/bsearch/strtol/rand/sysconf 等；
  标准 C 程序用 `crt0.o + libc.a` 链接

```
posix          # POSIX 自测（63 项）
libc           # 标准 C/libc 演示（stdio、malloc、dirent、signal、socket/poll/select、
               # AF_UNIX、AF_INET 回环 TCP/UDP、pthreads、环境变量、qsort）
ping           # 通过 e1000 向 10.0.2.2 发 ICMP echo
net            # 真实 UDP+TCP echo 测试
```

`net` 需要主机上的 echo 服务：

```
python3 -u /tmp/echo_servers.py   # UDP 9999 + TCP 9998
```

限制：TCP 为最小客户端（无重传/SACK/乱序重组），无 DNS 解析器、IPv6、socket 服务端
监听真实网络、实时信号（sigqueue）和 locale。

## windows

Git Bash 下构建（工具链约定放在 `I:/tools`：i686-elf 交叉编译器、mingw64 宿主 gcc）：

```
bash build-win.sh
qemu-system-i386 -m 64 -drive file=80m.img,format=raw,if=ide -nic user,model=e1000 -no-reboot
```

- 交叉工具链：`I:/tools/i686-elf/bin/i686-elf-gcc.exe`（可用 `CROSS=`/`HOSTCC=` 覆盖）
- 无头调试：`build-win.sh` 的 CFLAGS 加 `-DDEBUG_E9` 重编译，printk 输出会镜像到 E9 调试端口：
  `qemu-system-i386 -display none -chardev stdio,id=e9 -device isa-debugcon,iobase=0xe9,chardev=e9 -drive file=80m.img,format=raw,if=ide`

## snapshot

### time slice task running
![pthrT0.png](https://s1.ax1x.com/2018/01/14/pthrT0.png)
### memory map
![pthykV.png](https://s1.ax1x.com/2018/01/14/pthykV.png)
### read disk file
![pthDwq.png](https://s1.ax1x.com/2018/01/14/pthDwq.png)
### systemcall fork()
![pthBmn.png](https://s1.ax1x.com/2018/01/14/pthBmn.png)
