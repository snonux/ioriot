# Agent Notes

## Project Overview

I/O Riot captures Linux file I/O on one machine and replays the same workload
on another machine so storage and filesystem changes can be measured against a
real application pattern instead of a synthetic benchmark.

The normal execution path is:

1. capture live I/O into a `.capture` log through SystemTap probes
2. generate a `.replay` workload and initialize the replay tree
3. replay the workload with configurable process and thread parallelism
4. inspect replay stats and host-level storage metrics

## Architecture

- `/opt/ioriot/bin/ioriot` is built from `ioriot/src/main.c` and dispatches the
  capture, generate, init, and replay modes.
- `systemtap/src/ioriot.stp` is the source probe set for capture. The sibling
  files `systemtap/src/targetedioriot.stp` and `systemtap/src/javaioriot.stp`
  are generated variants built from it by `systemtap/Makefile`.
- `ioriot/src/generate/` parses `.capture` lines, maps real PIDs/FDs into the
  virtual replay model, and writes `.replay` operations.
- `ioriot/src/init/` creates the file and directory skeleton needed before
  replay starts.
- `ioriot/src/replay/` owns the runtime worker/process/thread engine that
  executes replay tasks against the initialized tree.
- `ioriot/src/datas/`, `ioriot/src/vfd.*`, and `ioriot/src/vsize.*` implement
  the custom maps, buffers, and virtual file-size/offset tracking shared by the
  generator and replay engine.

## Syscall Matrix End-to-End Test

Use this procedure when you need a reproducible end-to-end test that exercises
the supported capture/generate/replay syscall surface with a controlled
workload.

### Preconditions

- Run from the repository root: `/home/paul/git/ioriot`.
- Use a base directory under `/home`, not `/tmp`. This project filters some
  file systems during replay generation, and `/tmp` may be `tmpfs`.
- Keep the helper base directory and test name short. The checked-in syscall
  helpers use fixed-size `readlink()` buffers, so overly long absolute paths
  can truncate the captured symlink target and turn a valid replay check into a
  false failure.
- The helper below is intentionally dirfd-heavy so it catches path-resolution
  bugs in `*at` syscalls as well as normal file I/O.
- The main helper exercises the native x86_64 syscall surface. Use the compat32
  helper below when you need the legacy 32-bit ABI names such as `readdir`,
  `llseek`, `statfs64`, and the `*chown16` family.

### 1. Rebuild, test, and install

```bash
make clean all test
sudo make install
```

### 2. Prepare a unique run directory and compile the helper

```bash
stamp=$(date +%Y%m%d_%H%M%S)
run_root=/tmp/ioriot-syscall-e2e-$stamp
user_name=${SUDO_USER:-$USER}
base_dir=/home/$user_name/ioriot-syscall-matrix-src-$stamp
helper_bin=$run_root/syscall_matrix

mkdir -p "$run_root"
cc -Wall -Wextra -O2 -o "$helper_bin" scripts/syscall_matrix.c

pid_file=$run_root/syscall_matrix.pid
go_file=$run_root/syscall_matrix.go
capture_file=$run_root/syscall_matrix.capture
replay_file=$run_root/syscall_matrix.replay
stats_file=$run_root/syscall_matrix.stats
strace_file=$run_root/replay.strace
test_name=e2e_syscall_matrix_$stamp
```

### 3. Start the helper in a paused state

Run this in terminal A:

```bash
sudo "$helper_bin" "$pid_file" "$go_file" "$base_dir" "$user_name"
```

The helper writes its real PID into `"$pid_file"` and then waits until
`"$go_file"` exists.

### 4. Attach targeted capture before releasing the helper

Run this in terminal B after `"$pid_file"` exists:

```bash
helper_pid=$(cat "$pid_file")
sudo /opt/ioriot/bin/ioriot -c "$capture_file" -x "$helper_pid"
```

Wait until `staprun` reports that `targetedioriot.ko` has been inserted.
Then give the module a short settling delay before releasing the helper, for
example `sleep 2`. On this host, releasing the workload immediately after the
inserted message can miss the first burst of syscalls and produce a nearly
empty `.capture`.

### 5. Release the helper and stop capture after it exits

Back in terminal A, release the workload:

```bash
touch "$go_file"
```

When the helper exits, stop capture in terminal B with `Ctrl+C`.

### 6. Generate the replay file

```bash
sudo /opt/ioriot/bin/ioriot \
  -c "$capture_file" \
  -r "$replay_file" \
  -u "$user_name" \
  -n "$test_name" \
  -w /home/$user_name/.ioriot-wd
```

### 7. Sanity-check capture and replay contents

List the captured syscall names:

```bash
python - "$capture_file" <<'PY'
import re
import sys
ops=set()
for line in open(sys.argv[1]):
    m=re.search(r';:,o=([^;]+);:,', line)
    if m:
        ops.add(m.group(1))
for op in sorted(ops):
    print(op)
PY
```

List the replay operation names:

```bash
python - "$replay_file" <<'PY'
import re
import sys
ops=set()
for line in open(sys.argv[1]):
    m=re.search(r'\|([a-z0-9_]+)@[0-9]+\|\s*$', line)
    if m:
        ops.add(m.group(1))
for op in sorted(ops):
    print(op)
PY
```

Verify the replay file contains expected workload paths:

```bash
rg -n 'head-renamed|openat|creat|link-open|emptydir' "$replay_file"
```

Architecture note:

- On modern x86_64 systems, some operations may surface as modern entry points
  rather than legacy ones. For example, `open` may appear as `openat`, and
  `stat` / `lstat` may appear as `newfstatat` in `strace`.
- `readdir` replay typically appears as `getdents64` in `strace`, and legacy
  compat syscalls may replay through the closest modern libc entry point.

### 8. Initialize the replay tree

```bash
sudo /opt/ioriot/bin/ioriot \
  -i "$replay_file" \
  -w /home/$user_name/.ioriot-wd
```

### 9. Replay single-threaded under strace

```bash
sudo strace -f -o "$strace_file" \
  /opt/ioriot/bin/ioriot \
  -D \
  -r "$replay_file" \
  -p 1 \
  -t 1 \
  -S "$stats_file"
```

### 10. Verify replay activity and resulting state

Check stats:

```bash
cat "$stats_file"
```

Inspect replay syscalls touching the target tree:

```bash
rg -n "/home/\\.ioriot/$test_name/" "$strace_file"
```

Useful follow-up checks:

```bash
find "/home/.ioriot/$test_name" -maxdepth 6 \
  \( -name 'head-renamed.bin' -o -name 'open*.txt' -o -name 'creat*.txt' \
     -o -name 'link-open.txt' -o -name 'emptydir' \) | sort
```

```bash
python - "$test_name" "$user_name" "$stamp" <<'PY'
from pathlib import Path
import sys

test_name, user_name, stamp = sys.argv[1:]
root = Path(f"/home/.ioriot/{test_name}/{user_name}")
base = root / f"ioriot-syscall-matrix-src-{stamp}"

paths = [
    base / "open-renamed.txt",
    base / "emptydir",
    base / "dirA" / "openat-renamed.txt",
]
for p in paths:
    print(f"{p}: exists={p.exists()} is_dir={p.is_dir()}")
PY
```

### Expected coverage

The helper in [scripts/syscall_matrix.c](/home/paul/git/ioriot/scripts/syscall_matrix.c)
attempts to exercise these operations:

- `creat`, `openat`, `close`
- `read`, `readv`, `write`, `writev`
- `fstat`, `fstatat`, `statfs`, `fstatfs`
- `fcntl`, `readahead`, `sync`, `syncfs`, `sync_file_range`
- `getdents`, `lseek`, `llseek` when available
- `mkdir`, `mkdirat`, `rename`, `renameat`, `renameat2`
- `unlink`, `unlinkat`, `rmdir`
- `readlink`, `readlinkat`
- `chmod`, `fchmod`, `fchmodat`
- `chown`, `fchown`, `fchownat`, `lchown`

If the generated replay only contains the header and `#INIT`, or the replay
paths escape the intended test tree, investigate capture tokenization and
dirfd-relative path handling first.

## Legacy Compat Syscalls

The normal helper above exercises the current x86_64 syscall surface. For
legacy compat names that only show up through the 32-bit ABI on this host,
use [scripts/syscall_compat32.c](/home/paul/git/ioriot/scripts/syscall_compat32.c)
and compile it with `cc -m32`.

- It reuses the same `PID_FILE GO_FILE BASE_DIR USER` contract as the main
  matrix helper, so the capture/generate/init/replay procedure above applies
  unchanged.
- It covers `readdir`, `llseek`, `statfs64`, `fstatfs64`, `chown16`,
  `lchown16`, and `fchown16`.
- Run it with a user whose uid/gid fit into the legacy 16-bit chown ABI.
