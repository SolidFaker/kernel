# Filesystem

## Boot image

`tools/mkfs.c` writes the read-only `myfs` image into `build/kernel.img`:

- header + up to 128 entries at sector 10
- files laid out sector-aligned
- imported at boot into a writable ramfs by `fs/vfs.c`, then placed into `/boot` or
  content-addressed packages under `/store`; no executable files remain at `/`
- Coreutils entry points are small shebang text files in the Coreutils package;
  `/system` and `/profiles` expose them through symbolic links

## ramfs

`fs/ramfs.c` implements an in-memory tree with directories and files:

- child/sibling directory entries, recursive path lookup
- per-node data buffer with growth/truncate
- `create/unlink/rename/truncate` callbacks exposed through `struct fs_node`
- file and directory nodes imported from myfs
- `/dev` is an empty directory

`fs/ofd.c` implements shared open file descriptions used by `dup`/`fork`; descriptors
track their absolute path so write/truncate operations can be journaled.

## Log-structured filesystem (JFS2)

`fs/jfs.c` turns the writable tree into a persistent, disk-backed
log-structured filesystem.

### Layout

- superblock at LBA 8192
- two 8 MiB journal regions (region A and B); records are appended to the active
  region
- records: `CREATE_FILE`, `CREATE_DIR`, `WRITE`, `TRUNCATE`, `UNLINK`, `RMDIR`,
  `RENAME`, `CHMOD`, `SYMLINK`
- `/tmp` and `/run` are excluded from records and checkpoints; `/dev`, `/proc` and
  `/sys` are regenerated as kernel interfaces
- each record stores path/path2, offset, length, sequence number and CRC32
- `CREATE_*` and `CHMOD` records carry permission bits; checkpoints preserve modes
- commit protocol: invalidate header → write data sectors → committed header →
  `FLUSH CACHE`

### Boot and recovery

1. If the superblock has no valid snapshot, import the read-only myfs image into
   ramfs (boot/module partition)
2. If a snapshot exists, start from a fresh root tree and rebuild persistent content
   only from the journal
3. Scan the active region up to the checkpointed head, validate CRCs and replay
   records in order
4. When the region is over 75% full (or on the first boot), checkpoint the whole
   tree into the inactive region and switch the superblock atomically

After the first checkpoint the journal alone is authoritative for mutable areas:
adding, modifying, renaming, deleting files and creating symbolic links persist across
reboots. `/store` remains read-only after replay. The 16-bit loader still uses the raw
myfs boot area for `kernel.elf`; that is a boot partition, not the root filesystem.

### Persistence test

```sh
mkdir /state/system/demo
echo hi > /state/system/demo/a
echo enabled > /config/system/demo
# reboot
cat /state/system/demo/a  # hi
cat /config/system/demo   # enabled
```

### Limitations

- one checkpoint must fit in an 8 MiB region; very large trees can hit `ENOSPC`
- superblock is updated per write (durable but slower than a batched journal)
- fsync/fdatasync are simple disk flushes; there is no separate batched commit protocol
