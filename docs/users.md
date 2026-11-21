# Multi-user subsystem

Each task carries real, effective and saved UID/GID plus up to 16 supplementary
groups. Fork and thread creation inherit these credentials; exec preserves them.
The initial shell runs as root.

## User records

Users live under `/config/users/<name>/user`, not `/etc`. A record contains:

```text
name=alice
uid=1000
gid=1000
home=/home/alice
shell=/system/bin/bash
```

Home, state, log and cache directories are separated under `/home`,
`/state/users`, `/log/users` and `/cache/users`. User profiles use
`/profiles/users/<name>/bin`.

## Commands

The native `userctl.elf` multicall program is exposed as:

- `adduser <name> [uid]` — create a user record and lifecycle-separated directories.
- `su <name> [-c <command>]` — run Bash as another user; without `-c`, start an
  interactive shell.
- `login <name> [-c <command>]` — same user-session entry path as `su`.
- `id` — show real UID/GID and supplementary groups with names resolved from
  `/config/users`.
- `whoami` — print the current user name.

`adduser`, `su` and `login` require effective UID 0. This initial local-user model
has no password authentication or privilege-escalation path for normal users.
It is not a replacement for a hardened login daemon.

## Permissions and persistence

VFS nodes store UID, GID and mode bits. Path traversal, open, create, unlink,
rename, truncate and exec check the current effective identity and group membership.
Root bypasses mode checks, while read-only/noexec/append-only policies remain
separate. Only root or the owner may chmod; chown is root-only. Signal delivery to
another process also checks user identity.

JFS uses a `JFS_CHOWN` record and emits ownership after newly created objects, so
non-root ownership is replayed after reboot. Native and Linux user syscalls share
the same kernel credential implementation, including the 32-bit compatibility
syscall numbers.

## Test

```sh
EXTRA_CFLAGS=-DAUTO_MULTI bash build-arch.sh x86_64
qemu-system-x86_64 -m 128M -display none -serial stdio -no-reboot \
  -drive file=build/x86_64/80m.img,format=raw,if=ide
```

`user/users-test.sh` creates `testuser`, switches identity, checks home writes,
ownership, chmod and denied access to `/home/root`, and prints
`MULTIUSER_TEST_OK` when all checks pass.
