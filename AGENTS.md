# Agent Notes

## Syscall Matrix End-to-End Test

Use this procedure when you need a reproducible end-to-end test that exercises
the supported capture/generate/replay syscall surface with a controlled
workload.

### Preconditions

- Run from the repository root: `/home/paul/git/ioriot`.
- Use a base directory under `/home`, not `/tmp`. This project filters some
  file systems during replay generation, and `/tmp` may be `tmpfs`.
- The helper below is intentionally dirfd-heavy so it catches path-resolution
  bugs in `*at` syscalls as well as normal file I/O.
- `readdir` is intentionally excluded. Generation handles it, but replay does
  not currently have a `READDIR` implementation.

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
- Some replay entries are intentionally no-ops today: `statfs`, `fstatfs`,
  `readahead`, `readlink`, `readlinkat`, `sync`, `syncfs`, and
  `sync_file_range`.

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
