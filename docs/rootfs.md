# Root filesystem

The root tree separates immutable code, visibility, configuration, persistent state,
logs, caches, temporary data and user files. Legacy `/bin`, `/sbin`, `/lib`, `/usr`,
`/etc`, `/var`, `/opt`, `/root`, `/mnt`, `/media` and `/srv` are not created.

```text
/boot /efi /system -> /store/system-profiles/0 /store /profiles
/config /state /log /cache /home /run /tmp /mounts /dev /proc /sys
```

## Code and visibility

- Every imported user-space executable is stored under
  `/store/<crc32>-<name>-<version>/`; `/boot` remains the firmware/loader boundary.
- Bash and Coreutils package binaries live in package `bin/` directories. Coreutils
  command scripts use `#!/system/bin/coreutils` and are executable files in the same
  package.
- `/store/system-profiles/0` contains `bin`, `lib`, `include` and `share` links and
  directories. `/system` links to that generation.
- `/profiles/users/root/bin` contains a parallel link forest. The initial environment
  uses `PATH=/system/bin:/profiles/users/root/bin`, `HOME=/home/root` and
  `SHELL=/system/bin/bash`.
- `/system/bin/sh` links to Bash. Native programs remain available through `/system/bin`.

CRC32 is currently used only for content-addressed names and accidental-corruption
checks; it is not a cryptographic signature or dm-verity replacement.

## Writable areas

- `/config/system` and `/config/users/root` hold configuration.
- `/state/system` and `/state/users/root` hold persistent mutable state.
- `/log/system` and `/log/users/root` hold append-only logs. Existing logs cannot be
  truncated, unlinked or renamed through the normal filesystem operations.
- `/cache` is disposable persistent storage and can be cleared by policy.
- `/home/root` is mode `0700`; user projects and documents belong there.
- `/run` and `/tmp` are mode `01777`. Their contents are excluded from JFS snapshots and
  journal records, so they behave as volatile tmpfs areas even though they share ramfs.
- `/mounts` is the common mount-point area.

The VFS rejects execution from `/config`, `/state`, `/log`, `/cache`, `/run` and `/tmp`.
This check applies to scripts as well as ELF files. `/store` is recursively marked
read-only after boot/replay; attempts to create or modify package content return an
error even when reached through a symbolic link.

`/dev`, `/proc` and `/sys` are also excluded from persistence. Device and kernel-provided
files are expected to be regenerated at boot.

## Symlinks and persistence

Ramfs stores link targets as file data. The VFS resolver follows absolute and relative
links, handles `.` and `..`, detects loops, and can leave the final link unresolved for
`readlink`. JFS has a `JFS_SYMLINK` record and includes links in checkpoints, so profile
links survive reboots.

The regression script `user/posix-test.sh` checks the new tree, package links, root home
mode, noexec, read-only store and append-only logs. Its second-boot branch checks that
configuration, state, logs and a newly created profile link persist while `/tmp` and
`/run` disappear.

## Current boundaries

- `/proc` and `/sys` are currently mount-point directories, not complete procfs/sysfs
  implementations. The same applies to device-node provisioning under `/dev`.
- Mode bits and owners exist, but capabilities, polkit and ACLs are not implemented.
- No package manager or privileged store writer is provided yet. A generation can be
  selected through `/system`, but online package installation and boot-image replacement
  still need their own tools.
- `/home` currently relies on mode bits; the explicit noexec policy is limited to the
  areas listed above.
- Log rotation requires a future privileged log daemon; normal append-only writers cannot
  rotate logs themselves.
