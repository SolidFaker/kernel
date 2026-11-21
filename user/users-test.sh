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
v=$(whoami)
echo "root whoami=$v"
[ "$v" = root ]
check $? root-whoami
adduser testuser 1001
check $? adduser
v=$(su testuser -c 'whoami')
echo "user whoami=$v"
[ "$v" = testuser ]
check $? user-whoami
v=$(su testuser -c 'id')
echo "user id=$v"
case "$v" in
    uid=1001\(testuser\)*) true;;
    *) false;;
esac
check $? user-id
su testuser -c 'echo user-data > /home/testuser/data'
check $? user-write-home
v=$(su testuser -c 'cat /home/testuser/data')
echo "user data=$v"
[ "$v" = user-data ]
check $? user-read-home
v=$(su testuser -c 'stat -c %u:%g /home/testuser/data')
echo "file owner=$v"
[ "$v" = 1001:1001 ]
check $? file-owner
su testuser -c 'chmod 600 /home/testuser/data'
check $? owner-chmod
if su testuser -c 'chmod 600 /config/users/root/user'; then false; else true; fi
check $? chmod-denied
if su testuser -c 'echo denied > /home/root/denied'; then false; else true; fi
check $? root-write-denied
[ ! -e /home/root/denied ]
check $? root-write-absent
su testuser -c 'mkdir /tmp/testuser-tmp; echo ok > /tmp/testuser-tmp/file'
check $? user-tmp-write
if [ "$fails" -eq 0 ]; then
    echo MULTIUSER_TEST_OK
else
    echo MULTIUSER_TEST_FAIL
fi
exit
