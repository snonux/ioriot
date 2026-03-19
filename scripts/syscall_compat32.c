#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include <asm/unistd_32.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <unistd.h>

#if !defined(__i386__)
#error "Compile this helper with -m32 so it exercises the compat syscall ABI."
#endif

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
    must(pw->pw_uid <= USHRT_MAX, "uid fits 16-bit compat ABI");
    must(pw->pw_gid <= USHRT_MAX, "gid fits 16-bit compat ABI");

    write_pid_file(pid_file);
    wait_for_go(go_file);

    char path_file[1024];
    char path_link[1024];

    snprintf(path_file, sizeof(path_file), "%s/compat-open.txt", base);
    snprintf(path_link, sizeof(path_link), "%s/compat-link.txt", base);

    must_eq(mkdir(base, 0777), "mkdir compat base");

    int dirfd = open(base, O_RDONLY | O_DIRECTORY);

    must_eq(dirfd, "open compat base");

    int fd = open(path_file, O_CREAT | O_RDWR | O_TRUNC, 0644);

    must_eq(fd, "open compat-open.txt");
    write_full(fd, "compat-data", 11);
    must_eq(lseek(fd, 0, SEEK_SET), "lseek compat-open.txt");
    must_eq(symlink(path_file, path_link), "symlink compat-link.txt");

    char dir_buf[4096];
    long long llseek_result = 0;
    struct statfs64 sfs;

    must_eq(syscall(__NR_readdir, dirfd, dir_buf, sizeof(dir_buf)),
            "compat readdir");
    must_eq(syscall(__NR__llseek, fd, 0UL, 0UL, &llseek_result, SEEK_END),
            "compat _llseek");
    must_eq(syscall(__NR_statfs64, base, sizeof(sfs), &sfs),
            "compat statfs64");
    must_eq(syscall(__NR_fstatfs64, fd, sizeof(sfs), &sfs),
            "compat fstatfs64");
    must_eq(syscall(__NR_chown, path_file, pw->pw_uid, pw->pw_gid),
            "compat chown16");
    must_eq(syscall(__NR_lchown, path_link, pw->pw_uid, pw->pw_gid),
            "compat lchown16");
    must_eq(syscall(__NR_fchown, fd, pw->pw_uid, pw->pw_gid),
            "compat fchown16");

    must_eq(close(fd), "close compat-open.txt");
    must_eq(close(dirfd), "close compat base");

    return 0;
}
