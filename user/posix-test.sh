#!/system/bin/sh
fails=0
check()
{
    if [ "$1" -eq 0 ]; then
        echo "PASS $2"
    else
        echo "FAIL $2"
        fails=1
    fi
}
if [ -f /state/system/reboot-test/phase ]; then
    echo ROOTFS_REBOOT_SECOND_BEGIN
    v=$(cat /state/system/reboot-test/phase)
    [ "$v" = ready ]
    check $? state-persistent
    v=$(cat /config/system/reboot-test)
    [ "$v" = persistent ]
    check $? config-persistent
    v=$(cat /log/system/reboot-test)
    [ "$v" = boot ]
    check $? log-persistent
    [ ! -e /tmp/reboot-persist ] && [ ! -e /run/reboot-persist ]
    check $? tmp-run-volatile
    v=$(readlink /profiles/users/root/bin/reboot-shell)
    [ "$v" = /system/bin/bash ]
    check $? symlink-persistent
    if [ "$fails" -eq 0 ]; then
        echo POSIX_TEST_OK
    else
        echo POSIX_TEST_FAIL
    fi
    exit
fi
echo POSIX_TEST_BEGIN
ls > /tmp/ls-current
check $? ls-current-directory
rm /tmp/ls-current
mkdir /tmp/posix-test
check $? mkdir
echo hello > /tmp/posix-test/a
check $? write
v=$(cat /tmp/posix-test/a)
echo "cat output=$v"
[ "$v" = hello ]
check $? cat
v=$(cat /tmp/posix-test/a | wc -c)
echo "pipe output=$v"
[ "$v" -eq 6 ]
check $? pipe
cp /tmp/posix-test/a /tmp/posix-test/b
check $? cp
mv /tmp/posix-test/b /tmp/posix-test/c
check $? mv
printf 'b\na\n' > /tmp/posix-test/s
v=$(sort /tmp/posix-test/s)
echo "sort output=$v"
[ "$v" = "a
b" ]
check $? sort
chmod 600 /tmp/posix-test/c
check $? chmod
v=$(stat -c %a /tmp/posix-test/c)
[ "$v" = 600 ]
check $? stat
mkdir /tmp/posix-test/d
touch /tmp/posix-test/d/x
check $? touch
[ -f /tmp/posix-test/d/x ]
check $? test-file
rm /tmp/posix-test/d/x
check $? rm
rmdir /tmp/posix-test/d
check $? rmdir
rm /tmp/posix-test/a /tmp/posix-test/c /tmp/posix-test/s
check $? rm-multiple
rmdir /tmp/posix-test
check $? rmdir-tmp
postest.elf
check $? native-posix
libctest.elf
check $? native-libc
[ ! -e /bin ] && [ ! -e /sbin ] && [ ! -e /etc ] && [ ! -e /usr ] &&
    [ ! -e /var ] && [ ! -e /root ] && [ ! -e /mnt ] && [ ! -e /media ]
check $? no-legacy-paths
v=$(readlink /system)
[ "$v" = /store/system-profiles/0 ]
check $? system-link
v=$(readlink /system/bin/bash)
case "$v" in /store/*) true;; *) false;; esac
check $? store-package-link
v=$(stat -c %a /home/root)
[ "$v" = 700 ]
check $? root-home-mode
printf '#!/system/bin/sh\necho SHOULD_NOT_RUN\n' > /tmp/noexec-test
chmod 755 /tmp/noexec-test
if /tmp/noexec-test; then false; else true; fi
check $? tmp-noexec
if touch /store/forbidden; then false; else true; fi
check $? store-readonly
printf 'one\n' > /log/system/posix-test
printf 'two\n' >> /log/system/posix-test
v=$(cat /log/system/posix-test)
[ "$v" = "one
two" ]
check $? log-append-only
if [ "$fails" -eq 0 ]; then
    mkdir /state/system/reboot-test
    check $? reboot-state-dir
    echo ready > /state/system/reboot-test/phase
    echo persistent > /config/system/reboot-test
    echo temporary > /tmp/reboot-persist
    echo runtime > /run/reboot-persist
    ln -s /system/bin/bash /profiles/users/root/bin/reboot-shell
    check $? reboot-symlink-create
    printf 'boot\n' > /log/system/reboot-test
    check $? reboot-log-create
fi
if [ "$fails" -eq 0 ]; then
    echo POSIX_TEST_OK
else
    echo POSIX_TEST_FAIL
fi
