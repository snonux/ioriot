#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

static void
die(const char *msg)
{
    perror(msg);
    exit(1);
}

static void
must(bool ok, const char *msg)
{
    if (!ok)
        die(msg);
}

static void
must_eq(long rc, const char *msg)
{
    if (rc == -1)
        die(msg);
}

static void
write_pid_file(const char *path)
{
    FILE *fp = fopen(path, "w");

    must(fp != NULL, "fopen pid file");
    fprintf(fp, "%ld\n", (long)getpid());
    fclose(fp);
}

static void
wait_for_go(const char *path)
{
    while (access(path, F_OK) != 0)
        usleep(10000);
}

static void
write_full(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t written = write(fd, buf, len);

        must_eq(written, "write");
        buf += written;
        len -= (size_t)written;
    }
}

int
main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s PID_FILE GO_FILE BASE_DIR USER\n", argv[0]);
        return 2;
    }

    const char *pid_file = argv[1];
    const char *go_file = argv[2];
    const char *base = argv[3];
    const char *user = argv[4];

    struct passwd *pw = getpwnam(user);

    must(pw != NULL, "getpwnam");

    write_pid_file(pid_file);
    wait_for_go(go_file);

    char path_open[1024];
    char path_renamed[1024];
    char path_creat[1024];
    char path_link[1024];
    char path_emptydir[1024];

    snprintf(path_open, sizeof(path_open), "%s/open.txt", base);
    snprintf(path_renamed, sizeof(path_renamed), "%s/open-renamed.txt", base);
    snprintf(path_creat, sizeof(path_creat), "%s/creat.txt", base);
    snprintf(path_link, sizeof(path_link), "%s/link-open.txt", base);
    snprintf(path_emptydir, sizeof(path_emptydir), "%s/emptydir", base);

    must_eq(mkdir(base, 0777), "mkdir base");

    int basefd = open(base, O_RDONLY | O_DIRECTORY);

    must_eq(basefd, "open base dir");
    must_eq(mkdirat(basefd, "dirA", 0777), "mkdirat dirA");
    must_eq(mkdirat(basefd, "emptydir", 0777), "mkdirat emptydir");

    int dirfd = openat(basefd, "dirA", O_RDONLY | O_DIRECTORY);

    must_eq(dirfd, "openat dirA");
    must_eq(mkdirat(dirfd, "nested", 0777), "mkdirat nested");

    int fd_open = open(path_open, O_CREAT | O_RDWR | O_TRUNC, 0644);

    must_eq(fd_open, "open open.txt");
    write_full(fd_open, "alpha", 5);
    must_eq(fsync(fd_open), "fsync open.txt");
    must_eq(fdatasync(fd_open), "fdatasync open.txt");
    must_eq(lseek(fd_open, 0, SEEK_SET), "lseek open.txt");

    char read_buf[64];

    must_eq(read(fd_open, read_buf, 5), "read open.txt");

    struct iovec write_iov[2] = {
        {.iov_base = "beta", .iov_len = 4},
        {.iov_base = "gamma", .iov_len = 5},
    };

    must_eq(writev(fd_open, write_iov, 2), "writev open.txt");
    must_eq(lseek(fd_open, 0, SEEK_SET), "lseek open.txt again");

    char readv_a[5] = {0};
    char readv_b[10] = {0};
    struct iovec read_iov[2] = {
        {.iov_base = readv_a, .iov_len = sizeof(readv_a) - 1},
        {.iov_base = readv_b, .iov_len = sizeof(readv_b) - 1},
    };

    must_eq(readv(fd_open, read_iov, 2), "readv open.txt");

    struct stat st;

    must_eq(fstat(fd_open, &st), "fstat open.txt");
    must_eq(stat(path_open, &st), "stat open.txt");
    must_eq(fstatat(basefd, "open.txt", &st, 0), "fstatat open.txt");

    struct statfs sfs;

    must_eq(statfs(base, &sfs), "statfs base");
    must_eq(fstatfs(fd_open, &sfs), "fstatfs open.txt");

    int flags = fcntl(fd_open, F_GETFL);

    must_eq(flags, "fcntl getfl");
    must_eq(fcntl(fd_open, F_SETFL, flags | O_APPEND), "fcntl setfl");

#ifdef SYS_readahead
    syscall(SYS_readahead, fd_open, 0, 8);
#endif

#ifdef SYS_sync_file_range
    syscall(SYS_sync_file_range, fd_open, 0, 8, 0);
#endif

#if defined(SYS_llseek)
    long long llseek_result = 0;
    syscall(SYS_llseek, fd_open, 0UL, 0UL, &llseek_result, SEEK_END);
#elif defined(SYS__llseek)
    long long llseek_result = 0;
    syscall(SYS__llseek, fd_open, 0UL, 0UL, &llseek_result, SEEK_END);
#endif

    must_eq(syncfs(fd_open), "syncfs");
    sync();

    int fd_openat = openat(dirfd, "openat.txt", O_CREAT | O_RDWR | O_TRUNC,
                           0640);

    must_eq(fd_openat, "openat openat.txt");
    write_full(fd_openat, "openat-data", 11);
    must_eq(lseek(fd_openat, 0, SEEK_SET), "lseek openat.txt");
    must_eq(read(fd_openat, read_buf, 4), "read openat.txt");

    int fd_creat = creat(path_creat, 0644);

    must_eq(fd_creat, "creat creat.txt");
    write_full(fd_creat, "creat", 5);
    must_eq(close(fd_creat), "close creat.txt");

    int fd_nested = openat(dirfd, "nested", O_RDONLY | O_DIRECTORY);

    must_eq(fd_nested, "open nested dir");

    int fd_nested_file = openat(fd_nested, "head.bin",
                                O_CREAT | O_RDWR | O_TRUNC, 0644);

    must_eq(fd_nested_file, "open nested file");
    write_full(fd_nested_file, "head-data", 9);
    must_eq(close(fd_nested_file), "close nested file");

#ifdef SYS_getdents
    char dents_buf[4096];
    syscall(SYS_getdents, dirfd, dents_buf, sizeof(dents_buf));
#endif

    must_eq(symlink(path_open, path_link), "symlink");
    must_eq(lstat(path_link, &st), "lstat link");
    must_eq(readlink(path_link, read_buf, sizeof(read_buf)), "readlink");
    must_eq(readlinkat(basefd, "link-open.txt", read_buf, sizeof(read_buf)),
            "readlinkat");

    must_eq(chmod(path_open, 0640), "chmod");
    must_eq(fchmod(fd_open, 0600), "fchmod");
    must_eq(fchmodat(basefd, "open.txt", 0644, 0), "fchmodat");

    must_eq(chown(path_open, pw->pw_uid, pw->pw_gid), "chown");
    must_eq(fchown(fd_open, pw->pw_uid, pw->pw_gid), "fchown");
    must_eq(fchownat(basefd, "open.txt", pw->pw_uid, pw->pw_gid, 0),
            "fchownat");
    must_eq(lchown(path_link, pw->pw_uid, pw->pw_gid), "lchown");

    must_eq(rename(path_open, path_renamed), "rename");
    must_eq(renameat(dirfd, "openat.txt", dirfd, "openat-renamed.txt"),
            "renameat");

#ifdef SYS_renameat2
    must_eq(syscall(SYS_renameat2, basefd, "creat.txt", basefd,
                    "creat-renamed.txt", 0), "renameat2");
#endif

    must_eq(unlink(path_renamed), "unlink");
    must_eq(unlinkat(dirfd, "openat-renamed.txt", 0), "unlinkat");
    must_eq(unlinkat(basefd, "creat-renamed.txt", 0), "unlinkat creat");

    must_eq(renameat(fd_nested, "head.bin", dirfd, "head-renamed.bin"),
            "renameat nested");

    must_eq(close(fd_nested), "close nested dir");
    must_eq(rmdir(path_emptydir), "rmdir emptydir");

    must_eq(close(fd_openat), "close openat");
    must_eq(close(fd_open), "close open");
    must_eq(close(dirfd), "close dirA");
    must_eq(close(basefd), "close base");

    return 0;
}
