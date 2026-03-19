// Copyright 2018 Mimecast Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "rioop.h"

#include "../vfd.h"
#include "rworker.h"

typedef void (*rioop_handler_f)(rprocess_s *p, rthread_s *t, rtask_s *task);

typedef struct {
    int op;
    rioop_handler_f handler;
} rioop_entry_s;

#define _RIOOP_ENTRY_COUNT(entries) \
    (sizeof(entries) / sizeof((entries)[0]))

// Printing error messages
#define _Error(...) \
  fprintf(stderr, "%s:%d ERROR: ", __FILE__, __LINE__); \
  fprintf(stderr, __VA_ARGS__); \
  fprintf(stderr, "\nlineno:%ld path:%s\n", task->lineno, vfd->path); \
  fflush(stdout); \
  fflush(stderr); \
  exit(ERROR);

#define _Errno(...) \
  fprintf(stderr, "%s:%d ERROR: %s (%d). ", __FILE__, __LINE__, \
      strerror(errno), errno); \
  fprintf(stderr, __VA_ARGS__); \
  fprintf(stderr, "\nlineno:%ld path:%s\n", task->lineno, vfd->path); \
  fflush(stdout); \
  fflush(stderr); \
  exit(ERROR);

#define _Init_arg(num) int arg = atoi(task->toks[num])
#define _Init_cmd(num) int cmd = atoi(task->toks[num])
#define _Init_fd(num) long fd = atol(task->toks[num])
#define _Init_flags(num) int flags = atoi(task->toks[num])
//#define _Init_mode(num) int mode = atoi(task->toks[num])
#define _Init_offset(num) long offset = atol(task->toks[num])
#define _Init_op(num) int op = atoi(task->toks[num])
#define _Init_path2(num) char *path2 = task->toks[num]
#define _Init_path(num) char *path = task->toks[num]
#define _Init_rc(num) int rc = atoi(task->toks[num])
#define _Init_whence(num) long whence = atol(task->toks[num])

#define _Init_bytes(num) \
    int bytes = atoi(task->toks[num]); \
    if (bytes <= 0) return

#define _Init_virtfd \
    vfd_s *vfd = amap_get(p->fds_map, fd); \
    if (vfd == NULL) return

static struct passwd*
_rioop_get_pwd(const char *user, struct passwd *pwd, char *buf,
               const size_t buf_size)
{
    struct passwd *result = NULL;

    if (user == NULL)
        return NULL;

    if (getpwnam_r(user, pwd, buf, buf_size, &result) != 0)
        return NULL;

    return result;
}

static void
_rioop_noop(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    (void)p;
    (void)t;
    (void)task;
}

static void
_rioop_open_default(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    rioop_open(p, t, task, -1);
}

static void
_rioop_open_creat(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    rioop_open(p, t, task, O_CREAT|O_WRONLY|O_TRUNC);
}

static rioop_handler_f
_rioop_find_handler(const rioop_entry_s *entries, const size_t num_entries,
                    const int op)
{
    for (size_t i = 0; i < num_entries; ++i) {
        if (entries[i].op == op)
            return entries[i].handler;
    }

    return NULL;
}

static const rioop_entry_s _RIOOP_HANDLERS[] = {
    {FSTAT, rioop_fstat},
    {FSTATFS, _rioop_noop},
    {FSTATFS64, _rioop_noop},
    {FSTAT_AT, rioop_stat},
    {LSTAT, rioop_stat},
    {STAT, rioop_stat},
    {STATFS, _rioop_noop},
    {STATFS64, _rioop_noop},
    {READ, rioop_read},
    {READV, rioop_read},
    {READAHEAD, _rioop_noop},
    {READLINK, _rioop_noop},
    {READLINK_AT, _rioop_noop},
    {WRITE, rioop_write},
    {WRITEV, rioop_write},
    {OPEN, _rioop_open_default},
    {OPEN_AT, _rioop_open_default},
    {CREAT, _rioop_open_creat},
    {MKDIR, rioop_mkdir},
    {MKDIR_AT, rioop_mkdir},
    {RENAME, rioop_rename},
    {RENAME_AT, rioop_rename},
    {RENAME_AT2, rioop_rename},
    {CLOSE, rioop_close},
    {UNLINK, rioop_unlink},
    {UNLINK_AT, rioop_unlink},
    {RMDIR, rioop_rmdir},
    {FSYNC, rioop_fsync},
    {FDATASYNC, rioop_fdatasync},
    {SYNC, _rioop_noop},
    {SYNCFS, _rioop_noop},
    {SYNC_FILE_RANGE, _rioop_noop},
    {FCNTL, rioop_fcntl},
    {GETDENTS, rioop_getdents},
    {LSEEK, rioop_lseek},
    {LLSEEK, rioop_lseek},
    {CHMOD, rioop_chmod},
    {FCHMOD, rioop_fchmod},
    {CHOWN, rioop_chown},
    {FCHOWN, rioop_fchown},
    {FCHOWNAT, rioop_fchown},
    {LCHOWN, rioop_lchown},
    {META_EXIT_GROUP, _rioop_noop},
    {META_TIMELINE, _rioop_noop},
};

void rioop_test(void)
{
    const size_t handler_count = _RIOOP_ENTRY_COUNT(_RIOOP_HANDLERS);

    assert(_rioop_find_handler(_RIOOP_HANDLERS, handler_count, OPEN) ==
           _rioop_open_default);
    assert(_rioop_find_handler(_RIOOP_HANDLERS, handler_count, CREAT) ==
           _rioop_open_creat);
    assert(_rioop_find_handler(_RIOOP_HANDLERS, handler_count, READAHEAD) ==
           _rioop_noop);
    assert(_rioop_find_handler(_RIOOP_HANDLERS, handler_count, -1) == NULL);
}

void rioop_run(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_op(2);
    rioop_handler_f handler = _rioop_find_handler(_RIOOP_HANDLERS,
                                                  _RIOOP_ENTRY_COUNT(
                                                      _RIOOP_HANDLERS),
                                                  op);

    if (handler == NULL) {
        Error("op(%d) not implemented", op);
    }

    handler(p, t, task);
}

void rioop_stat(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_path(3);
    struct stat buf;
    stat(path, &buf);
}

void rioop_fstat(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_fd(3);
    _Init_virtfd;
    struct stat buf;
    fstat(vfd->fd, &buf);
}

void rioop_rename(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_path(3);
    _Init_path2(4);
    rename(path, path2);
}

void rioop_read(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_fd(3);
    _Init_bytes(4);
    _Init_virtfd;

    char *buf = Calloc(bytes+1, char);
    read(vfd->fd, buf, bytes);
    free(buf);
}

void rioop_write(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_fd(3);
    _Init_bytes(4);
    _Init_virtfd;

    char *buf = Calloc(bytes+1, char);
    sprintf(buf, "%ld", task->lineno);
    Fill_with_stuff(buf, bytes);
    if (vfd->fd == 0) {
        Debug("%d %d %ld", vfd->fd, vfd->debug, task->lineno);
        _Error("ERROR");
    }
    write(vfd->fd, buf, bytes);
    free(buf);
}

void rioop_open(rprocess_s *p, rthread_s *t, rtask_s *task, int flags_)
{
    _Init_fd(3);
    _Init_path(4);
    _Init_flags(6);

    // Special case as this is creat() now
    if (flags_ != -1)
        flags = flags_;

    bool directory = Has(flags, O_DIRECTORY);

    if (fd > 0) {
        if (directory) {
            // We can not open a directory via open() otherwise!
            flags &= (O_RDONLY & ~(O_RDWR|O_WRONLY|O_CREAT));
        } else {
            // We don't want to open the file in read only mode.
            // SystemTap could have skipped syscalls to fcntl or open
            flags &= ~O_RDONLY;
        }
        //    flags |= O_DIRECT|O_SYNC;
        flags &= ~O_EXCL;
    }

    int ret = open(path, flags, S_IRWXU|S_IRWXG|S_IRWXO);

    if (fd < 0 && ret > 0) {
        close(ret);
#ifdef THREAD_DEBUG
        fprintf(t->rthread_fd, "TRACE OPEN|open+close|%s|\n", path);
        fflush(t->rthread_fd);
#endif
    }

    if (fd > 0 && ret > 0) {
        rworker_s *w = t->worker;
        vfd_s *vfd = vfd_new(ret, fd, path);
        pthread_mutex_lock(&w->fds_map_mutex);
        amap_set(p->fds_map, fd, vfd);
        pthread_mutex_unlock(&w->fds_map_mutex);

#ifdef THREAD_DEBUG
        fprintf(t->rthread_fd, "TRACE OPEN|open|%s|\n", path);
        fflush(t->rthread_fd);
#endif
    }
}

void rioop_close(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_fd(3);
    rworker_s *w = t->worker;
    // Lookup lifetime still relies on the per-FD ownership invariant.
    pthread_mutex_lock(&w->fds_map_mutex);
    vfd_s *vfd = amap_get(p->fds_map, fd);
    if (vfd != NULL)
        amap_unset(p->fds_map, fd);
    pthread_mutex_unlock(&w->fds_map_mutex);
    if (vfd == NULL)
        return;

    if (vfd->dirfd) {
        closedir(vfd->dirfd);
#ifdef THREAD_DEBUG
        fprintf(t->rthread_fd, "TRACE OPEN|closedir|%s|\n", vfd->path);
        fflush(t->rthread_fd);
#endif
    } else {
        close(vfd->fd);
#ifdef THREAD_DEBUG
        fprintf(t->rthread_fd, "TRACE OPEN|close|%s|\n", vfd->path);
        fflush(t->rthread_fd);
#endif
    }
    vfd_destroy(vfd);
}

void rioop_getdents(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_fd(3);
    _Init_virtfd;

    // getdents expects a dirfd
    DIR *dirfd = fdopendir(vfd->fd);
    if (dirfd) {
        vfd->dirfd = dirfd;
        readdir(dirfd);
#ifdef THREAD_DEBUG
        fprintf(t->rthread_fd, "TRACE OPEN|fdopendir|%s|\n", vfd->path);
        fflush(t->rthread_fd);
#endif
    }
}

void rioop_mkdir(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_path(3);
    mkdir(path, S_IRWXU|S_IRWXG|S_IRWXO);
}

void rioop_unlink(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_path(3);
    unlink(path);
}

void rioop_rmdir(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_path(3);
    rmdir(path);
}

void rioop_lseek(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_fd(3);
    _Init_bytes(6);
    _Init_virtfd;
    lseek(vfd->fd, bytes, SEEK_SET);
}

void rioop_fsync(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_fd(3);
    _Init_virtfd;
    fsync(vfd->fd);
}

void rioop_fdatasync(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_fd(3);
    _Init_virtfd;
    fdatasync(vfd->fd);
}

void rioop_fcntl(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_fd(3);
    _Init_cmd(4);
    _Init_arg(5);
    _Init_virtfd;

    switch (cmd) {
    case F_GETFD:
    case F_GETFL:
        fcntl(vfd->fd, cmd);
        break;
    case F_SETFD:
    case F_SETFL:
        fcntl(vfd->fd, cmd, arg);
        break;
    default:
        break;
    }
}

void rioop_chmod(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_path(3);
    chmod(path, S_IRWXU|S_IRWXG|S_IRWXO);
}

void rioop_fchmod(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_fd(3);
    _Init_virtfd;
    fchmod(vfd->fd, S_IRWXU|S_IRWXG|S_IRWXO);
}

void rioop_chown(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_path(3);
    rworker_s *w = t->worker;
    options_s *opts = w->opts;
    char pwd_buf[MAX_LINE_LEN];
    struct passwd pwd;
    struct passwd *pwd_result = _rioop_get_pwd(opts->user, &pwd, pwd_buf,
                                               sizeof(pwd_buf));
    if (pwd_result == NULL)
        return;
    chown(path, pwd_result->pw_uid, -1);
}

void rioop_fchown(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_fd(3);
    _Init_virtfd;
    rworker_s *w = t->worker;
    options_s *opts = w->opts;
    char pwd_buf[MAX_LINE_LEN];
    struct passwd pwd;
    struct passwd *pwd_result = _rioop_get_pwd(opts->user, &pwd, pwd_buf,
                                               sizeof(pwd_buf));
    if (pwd_result == NULL)
        return;
    fchown(vfd->fd, pwd_result->pw_uid, -1);
}

void rioop_lchown(rprocess_s *p, rthread_s *t, rtask_s *task)
{
    _Init_path(3);
    rworker_s *w = t->worker;
    options_s *opts = w->opts;
    char pwd_buf[MAX_LINE_LEN];
    struct passwd pwd;
    struct passwd *pwd_result = _rioop_get_pwd(opts->user, &pwd, pwd_buf,
                                               sizeof(pwd_buf));
    if (pwd_result == NULL)
        return;
    lchown(path, pwd_result->pw_uid, -1);
}
