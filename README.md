# A-MINI-KERNEL

## make

```
make all
```

## windows

Git Bash 下构建并启动（工具链约定放在 `I:/tools`：i686-elf 交叉编译器、mingw64 宿主 gcc、bochs）：

```
bash build-win.sh                                     # 编译并生成可启动的 80m.img
I:/tools/bochs/bochs.exe -q -f bochsrc.windows        # 启动
```

- 交叉工具链：`I:/tools/i686-elf/bin/i686-elf-gcc.exe`（可用 `CROSS=`/`HOSTCC=` 覆盖）
- bochs 强杀进程后会残留 `80m.img.lock`，删除后再启动，否则报 `image locked`
- 无头调试：`build-win.sh` 的 CFLAGS 加 `-DDEBUG_E9` 重编译，printk 输出会镜像到 bochs 控制台（`bochsrc.windows` 已开启 `port_e9_hack`）

## snapshot

### time slice task running
![pthrT0.png](https://s1.ax1x.com/2018/01/14/pthrT0.png)
### memory map
![pthykV.png](https://s1.ax1x.com/2018/01/14/pthykV.png)
### read disk file
![pthDwq.png](https://s1.ax1x.com/2018/01/14/pthDwq.png)
### systemcall fork()
![pthBmn.png](https://s1.ax1x.com/2018/01/14/pthBmn.png)
