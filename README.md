# I/O Riot

## Overview

<img src=doc/ioriot_small.png align=right />

...is an I/O benchmarking tool for Linux systems which captures file-I/O
operations on one host and replays the same workload on another host or test
environment.

I/O Riot is normally operated in 5 steps:

1. Capture: Record all I/O operations over a given period of time to a capture log.
2. Generate: Transform the `.capture` log into a `.replay` file with replay-local paths and metadata.
3. Initialize: Create the directory, file, and symlink skeleton required for replay.
4. Replay: Optionally drop caches and replay the recorded workload with configurable worker and thread parallelism.
5. Analyze and repeat: Inspect replay stats and storage behavior, then rerun with different hardware, file-system, or tuning choices.

Examples of OS and hardware settings and adjustments:

* Change of system parameters (file system mount options, file system caching, file system type, file system creation flags).
* Replay the I/O at different speed(s).
* Replay the I/O with modified pattern(s) (e.g. remove reads from the replay journal).
* Replay the I/O on different types of hardware.

The file system fragmentation (depending on the file system type and utilisation) might affect I/O performance as well. Therefore, replaying the I/O will not give the exact same result as on a production system. But it provides a pretty good way to determine I/O bottlenecks. As a rule of thumb file system fragmentation will not be an issue, unless the file system begins to fill up. Modern file systems (such as Ext4) will slowly start to suffer from fragmentation and slow down then.

## Benefits

In contrast to traditional I/O benchmarking tools, I/O Riot reproduces real production I/O, and does not rely on a pre-defined set of I/O operations.

Also, I/O Riot only requires a server machine for capturing and another server machine for replaying. A traditional load test environment would usually be a distributed system which can consist of many components and machines. Such a distributed system can become quite complex which makes it difficult to isolate possible I/O bottlenecks. For example in order to trigger I/O events a client application would usually have to call a remote server application. The remote server application itself would query a database and the database would trigger the actual I/O operations in Linux. Furthermore, it is not easy to switch forth and back between hardware and OS settings. For example without a backup and restore procedure a database would most likely be corrupt after reformatting the data partitions with a different file system type.

The benefits of I/O Riot are:

* It is easy to determine whether a new hardware type is suitable for an already existing application.
* It is easy to change OS and hardware for performance tests and optimizations.
* Findings can be applied to production machines in order to optimize OS configuration and to save hardware costs.
* Benchmarks are based on production I/O patterns and not on artificial I/O patterns.
* Log files can be modified to see whether a change in the application behavior would improve I/O performance (without actually touching the application code)
* Log files could be generated synthetically in order to find out how a new application would perform (even if there isn't any code for the new application yet)
* It identifies possible flaws in the applications (e.g. Java programs which produce I/O operations on the server machines). Findings can be reported to the corresponding developers so that changes can be introduced to improve the applications I/O performance.
* It captures I/O in Linux Kernel space (very efficient, no system slowdowns even under heavy I/O load)
* It replays I/O via a tool developed in C with as little overhead as possible.

# Send in patches

Patches of any kind (bug fixes, new features...) are welcome! I/O Riot is new software and not everything might be perfect yet. Also, I/O Riot is used for a very specific use case at Mimecast. It may need tuning or extension for your use case. It will grow and mature over time.

This is also potentially a great tool just for analysing (not replaying) the I/O, therefore it would be a great opportunity to add more features related to that (e.g. more stats, filters, etc.).

Future work will also include file hole support and I/O support for memory mapped files.

# How to install I/O Riot

I/O Riot depends on SystemTap and a compatible version of the Linux Kernel. To get started have a read through the [installation guide](doc/markdown/installation.md).

Once the prerequisites are in place, the normal build and install flow is:

```bash
make clean all test
sudo make install
```

# How to use I/O Riot

Check out the [I/O Riot usage guide](doc/markdown/usage.md) for a full usage workflow demonstration.

The current CLI workflow is:

```bash
# Capture all supported file I/O, or target a specific PID with -x
sudo /opt/ioriot/bin/ioriot -c io.capture [-x PID]

# Generate a replay file from the capture log
sudo /opt/ioriot/bin/ioriot -c io.capture -r io.replay -u "$USER" -n test1 \
  -w /home/"$USER"/.ioriot-wd

# Initialize the replay tree
sudo /opt/ioriot/bin/ioriot -i io.replay -w /home/"$USER"/.ioriot-wd

# Replay the workload
sudo /opt/ioriot/bin/ioriot -r io.replay -S stats.txt
```

Notes:

* Targeted capture (`-x PID`) automatically uses `targetedioriot.ko` unless `-m` overrides the module name.
* Replay data is created under per-test directories such as `/home/.ioriot/<name>/...`.
* Captured symlink targets are rewritten to replay-local relative paths, and replay refuses to create symlinks that would resolve outside the corresponding replay tree.

# Appendix

## Supported file systems

Replay path rewriting currently recognizes `ext2`, `ext4`, `xfs`, `zfs`, and
`btrfs` mount points.

## Supported syscalls

The generator and replay engine currently support these operations end to end
on the native syscall surface:

```text
open, openat, creat
close
lseek
fcntl
read, readv, readahead
write, writev
stat, lstat, fstat, fstatat
statfs, fstatfs
readdir, getdents
readlink, readlinkat
rename, renameat, renameat2
unlink, unlinkat, rmdir
mkdir, mkdirat
fsync, fdatasync, sync, syncfs, sync_file_range
chmod, fchmod, fchmodat
chown, lchown, fchown, fchownat
```

Additional legacy compat syscall names are supported when they are captured via
the 32-bit ABI on a compatible host:

```text
llseek
statfs64, fstatfs64
chown16, lchown16, fchown16
```

The capture layer also records some mmap-related operations, but they are still
ignored during replay generation:

```text
mmap2, mremap, munmap, msync
```

## Source code documentation

The documentation of the source code can be generated via the Doxygen Framework. To install doxygen run ``sudo yum install doxygen`` and to generate the documentation run ``make doxygen`` in the top level source directory.  Once done, the resulting documentation can be found in the ``doc/html`` subfolder of the project. It is worthwhile to start from ``ioriot/src/main.c`` and read your way through. Functions are generally documented in the header files. Exceptions are static functions which don't have any separate declarations.

More
====

* [How to contribute](CONTRIBUTING.md)
* [Code of conduct](CODE_OF_CONDUCT.md)
* [License](LICENSE)

Credits
=======

* I/O Riot was created by **Paul Buetow** *<pbuetow@mimecast.com>*

* Thank you to **Vlad-Marian Marian** for creating the I/O Riot logo.
