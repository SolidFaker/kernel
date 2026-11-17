# Filesystem

## Boot image

`tools/mkfs.c` writes the read-only `myfs` image into `build/kernel.img`:

- header + entries at sector 10
- files laid out sector-aligned
- imported at boot into a writable ramfs by `fs/vfs.c`

## ramfs

`fs/ramfs.c` implements an in-memory tree with directories and files:

- child/sibling directory entries, recursive path lookup
- per-node data buffer with growth/truncate
- `create/unlink/rename/truncate` callbacks exposed through `struct fs_node`
- file and directory nodes imported from myfs
- `/dev` is an empty directory

`fs/ofd.c` implements shared open file descriptions used by `dup`/`fork`; descriptors
track their absolute path so write/truncate operations can be journaled.

## Persistent journal (JFS)

`fs/jfs.c` adds a write-ahead log on the ATA disk:

- journal area starts at LBA 8192, 8 MiB
- records: `CREATE_FILE`, `CREATE_DIR`, `WRITE`, `TRUNCATE`, `UNLINK`, `RMDIR`,
  `RENAME`
- each record has path/path2, offset, length, sequence number and CRC32
- commit protocol: invalidate header → write data sectors → write committed header →
  `FLUSH CACHE`
- at boot: import myfs into ramfs, then replay valid records in order
- journal writes are hooked into open/write/truncate/mkdir/rmdir/unlink/rename

Persistence test (same `build/80m.img` across two boots):

```sh
mkdir /docs
mkdir /docs/sub
echo hi > /docs/a
# reboot
ls /docs       # a sub
cat /docs/a    # hi
```

Limitations:

- append-only; no checkpoint/compaction yet
- once 8 MiB is full, further journaled writes return `ENOSPC`
- `make all` recreates `build/80m.img`; persistence is across boots of the same image,
  not across rebuilds
