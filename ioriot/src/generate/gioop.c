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

#include "gioop.h"

typedef status_e (*gioop_handler_f)(gwriter_s *w, gtask_s *t, generate_s *g);

typedef struct {
    const char *op;
    gioop_handler_f handler;
} gioop_entry_s;

#define _GIOOP_ENTRY_COUNT(entries) \
    (sizeof(entries) / sizeof((entries)[0]))

static status_e
_gioop_ignore(gwriter_s *w, gtask_s *t, generate_s *g)
{
    (void)w;
    (void)t;
    (void)g;

    return SUCCESS;
}

static gioop_handler_f
_gioop_find_handler(const gioop_entry_s *entries, const size_t num_entries,
                    const char *op)
{
    if (op == NULL)
        return NULL;

    for (size_t i = 0; i < num_entries; ++i) {
        if (Eq(entries[i].op, op))
            return entries[i].handler;
    }

    return NULL;
}

static int
_gioop_creat_flags(const gtask_s *t)
{
    if (t->flags != -1)
        return t->flags;

    // creat(2) behaves like open(..., O_CREAT|O_WRONLY|O_TRUNC, mode).
    return O_CREAT | O_WRONLY | O_TRUNC;
}

static const char*
_gioop_label(const gtask_s *t, const char *fallback)
{
    if (t != NULL && t->op != NULL)
        return t->op;

    return fallback;
}

static const gioop_entry_s _GIOOP_PREOPEN[] = {
    {"open", gioop_open},
    {"openat", gioop_openat},
    {"creat", gioop_creat},
};

static const gioop_entry_s _GIOOP_DISPATCH[] = {
    {"close", gioop_close},
    {"stat", gioop_stat},
    {"statfs", gioop_statfs},
    {"statfs64", gioop_statfs64},
    {"fstat", gioop_fstat},
    {"fstatat", gioop_fstatat},
    {"fstatfs", gioop_fstatfs},
    {"fstatfs64", gioop_fstatfs64},
    {"rename", gioop_rename},
    {"renameat", gioop_renameat},
    {"renameat2", gioop_renameat2},
    {"read", gioop_read},
    {"readv", gioop_readv},
    {"readahead", gioop_readahead},
    {"readdir", gioop_readdir},
    {"readlink", gioop_readlink},
    {"readlinkat", gioop_readlinkat},
    {"write", gioop_write},
    {"writev", gioop_writev},
    {"lseek", gioop_lseek},
    {"llseek", gioop_llseek},
    {"getdents", gioop_getdents},
    {"mkdir", gioop_mkdir},
    {"rmdir", gioop_rmdir},
    {"mkdirat", gioop_mkdirat},
    {"unlink", gioop_unlink},
    {"unlinkat", gioop_unlinkat},
    {"lstat", gioop_lstat},
    {"fsync", gioop_fsync},
    {"fdatasync", gioop_fdatasync},
    {"sync", gioop_sync},
    {"syncfs", gioop_syncfs},
    {"sync_file_range", gioop_sync_file_range},
    {"fcntl", gioop_fcntl},
    {"mmap2", _gioop_ignore},
    {"munmap", _gioop_ignore},
    {"mremap", _gioop_ignore},
    {"msync", _gioop_ignore},
    {"chmod", gioop_chmod},
    {"fchmodat", gioop_chmod},
    {"fchmod", gioop_fchmod},
    {"chown", gioop_chown},
    {"chown16", gioop_chown},
    {"lchown", gioop_lchown},
    {"lchown16", gioop_lchown},
    {"fchown", gioop_fchown},
    {"fchown16", gioop_fchown},
    {"fchownat", gioop_chown},
    {"exit_group", gioop_exit_group},
};

void gioop_test(void)
{
    gtask_s task = {0};
    const size_t preopen_count = _GIOOP_ENTRY_COUNT(_GIOOP_PREOPEN);
    const size_t dispatch_count = _GIOOP_ENTRY_COUNT(_GIOOP_DISPATCH);

    assert(_gioop_find_handler(_GIOOP_PREOPEN, preopen_count, "open") ==
           gioop_open);
    assert(_gioop_find_handler(_GIOOP_PREOPEN, preopen_count, "creat") ==
           gioop_creat);
    assert(_gioop_find_handler(_GIOOP_DISPATCH, dispatch_count, "close") ==
           gioop_close);
    assert(_gioop_find_handler(_GIOOP_DISPATCH, dispatch_count, "msync") ==
           _gioop_ignore);
    assert(_gioop_find_handler(_GIOOP_DISPATCH, dispatch_count, NULL) == NULL);
    assert(_gioop_find_handler(_GIOOP_DISPATCH, dispatch_count,
                               "definitely_unknown") == NULL);

    task.flags = -1;
    assert(_gioop_creat_flags(&task) == (O_CREAT | O_WRONLY | O_TRUNC));
    task.flags = O_RDONLY;
    assert(_gioop_creat_flags(&task) == O_RDONLY);
    task.op = "lchown16";
    assert(Eq(_gioop_label(&task, "fallback"), "lchown16"));
    assert(Eq(_gioop_label(NULL, "fallback"), "fallback"));
}

status_e gioop_run(gwriter_s *w, gtask_s *t)
{
    status_e ret = SUCCESS;
    gioop_handler_f handler = NULL;

    // There was already an error in the parser (parser.c) processing this
    // task! Don't process it futher.
    if (t->ret != SUCCESS) {
        Cleanup(t->ret);
    }

    generate_s *g = w->generate;

    // Get the virtual process data object from the virtual PID space and store
    // a pointer to it at t->gprocess
    generate_gprocess_by_realpid(g, t);

    // One of the open syscalls may openes a file handle succesfully
    handler = _gioop_find_handler(_GIOOP_PREOPEN,
                                  _GIOOP_ENTRY_COUNT(_GIOOP_PREOPEN),
                                  t->op);
    if (handler != NULL) {
        Cleanup(handler(w, t, g));
    }

    // Get the virtual file descriptor of a given real fd and store a pointer
    // to it to t->vfd.
    if (t->has_fd) {
        ret = gprocess_vfd_by_realfd(t->gprocess, t);
        Cleanup_unless(SUCCESS, ret);
    }

    handler = _gioop_find_handler(_GIOOP_DISPATCH,
                                  _GIOOP_ENTRY_COUNT(_GIOOP_DISPATCH),
                                  t->op);
    if (handler != NULL) {
        Cleanup(handler(w, t, g));
    }

    Cleanup(ERROR);

cleanup:

#ifdef LOG_FILTERED
    if (ret != SUCCESS)
        t->filtered_where = __FILE__;
#endif

    t->ret = ret;
    return ret;
}

status_e gioop_open(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd || t->path == NULL || t->flags == -1) {
        return ERROR;
    }

    gprocess_create_vfd_by_realfd(t->gprocess, t, g);
    generate_vsize_by_path(g, t, NULL);

    Gioop_write(OPEN, "%ld|%s|%d|%d|open",
                t->mapped_fd, t->path, t->mode, t->flags);

    if (t->fd > 0)
        vsize_open(t->vsize, t->vfd, t->path, t->flags);

    return SUCCESS;
}

status_e gioop_openat(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd || t->path == NULL || t->flags == -1) {
        return ERROR;
    }

    gprocess_create_vfd_by_realfd(t->gprocess, t, g);
    generate_vsize_by_path(g, t, NULL);
    Gioop_write(OPEN_AT, "%ld|%s|%d|%d|openat",
                t->mapped_fd,t->path, t->mode, t->flags);
    if (t->fd > 0)
        vsize_open(t->vsize, t->vfd, t->path, t->flags);

    return SUCCESS;
}

status_e gioop_creat(gwriter_s *w, gtask_s *t, generate_s *g)
{
    int flags = _gioop_creat_flags(t);

    if (!t->has_fd || t->path == NULL || t->mode == -1) {
        return ERROR;
    }

    gprocess_create_vfd_by_realfd(t->gprocess, t, g);
    generate_vsize_by_path(g, t, NULL);

    Gioop_write(CREAT, "%ld|%s|%d|%d|creat",
                t->mapped_fd, t->path, t->mode, flags);
    if (t->fd > 0)
        vsize_open(t->vsize, t->vfd, t->path, flags);

    return SUCCESS;
}


status_e gioop_close(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(CLOSE, "%ld|%d|close", t->mapped_fd, t->status);

    if (t->status == 0)
        vsize_close(t->vsize, t->vfd);

    hmap_remove_l(t->gprocess->fd_map, t->fd);
    hmap_remove_l(t->gprocess->vfd_map, t->mapped_fd);

    if (!(rbuffer_insert(g->vfd_buffer, t->vfd)))
        vfd_destroy(t->vfd);

    return SUCCESS;
}

status_e gioop_stat(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(STAT, "%s|%d|stat", t->path, t->status);

    if (t->status == 0)
        vsize_stat(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_statfs(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(STATFS, "%s|%d|statfs", t->path, t->status);

    if (t->status == 0)
        vsize_stat(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_statfs64(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(STATFS64, "%s|%d|statfs64", t->path, t->status);

    if (t->status == 0)
        vsize_stat(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_fstat(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(FSTAT, "%ld|%d|fstat", t->mapped_fd, t->status);

    return SUCCESS;
}

status_e gioop_fstatat(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(FSTAT_AT, "%s|%d|fstatat", t->path, t->status);

    if (t->status == 0)
        vsize_stat(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_fstatfs(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(FSTATFS, "%ld|%d|fstatfs", t->mapped_fd, t->status);

    return SUCCESS;
}

status_e gioop_fstatfs64(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(FSTATFS64, "%ld|%d|fstatfs64", t->mapped_fd, t->status);

    return SUCCESS;
}

status_e gioop_rename(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL || t->path2 == NULL ) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(RENAME, "%s|%s|%d|rename", t->path, t->path2, t->status);

    if (t->status == 0) {
        t->vsize2 = generate_vsize_by_path(g, NULL, t->path2);
        vsize_rename(t->vsize, t->vsize2, t->path, t->path2);
    }

    return SUCCESS;
}

status_e gioop_renameat(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL || t->path2 == NULL ) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(RENAME_AT, "%s|%s|%d|renameat", t->path, t->path2, t->status);

    if (t->status == 0) {
        t->vsize2 = generate_vsize_by_path(g, NULL, t->path2);
        vsize_rename(t->vsize, t->vsize2, t->path, t->path2);
    }

    return SUCCESS;
}
status_e gioop_renameat2(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL || t->path2 == NULL ) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(RENAME_AT2, "%s|%s|%d|renameat2",
                t->path, t->path2, t->status);

    if (t->status == 0) {
        t->vsize2 = generate_vsize_by_path(g, NULL, t->path2);
        vsize_rename(t->vsize, t->vsize2, t->path, t->path2);
    }

    return SUCCESS;
}

status_e gioop_read(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(READ, "%ld|%ld|read", t->mapped_fd, t->bytes);

    if (t->bytes > 0)
        vsize_read(t->vsize, t->vfd, t->vfd->path, t->bytes);

    return SUCCESS;
}

status_e gioop_readv(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(READ, "%ld|%ld|readv", t->mapped_fd, t->bytes);

    if (t->bytes > 0)
        vsize_read(t->vsize, t->vfd, t->vfd->path, t->bytes);

    return SUCCESS;
}

status_e gioop_readahead(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(READAHEAD, "%ld|%ld|%ld|readahead",
                t->mapped_fd, t->offset, t->count);

    return SUCCESS;
}

status_e gioop_readdir(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(READDIR, "%ld|%d|readdir", t->mapped_fd, t->status);

    return SUCCESS;
}

status_e gioop_readlink(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(READLINK, "%s|%d|readlink", t->path, t->status);

    if (t->status == 0)
        vsize_stat(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_readlinkat(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(READLINK_AT, "%s|%d|readlinkat", t->path, t->status);

    if (t->status == 0)
        vsize_stat(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_write(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(WRITE, "%ld|%ld|write", t->mapped_fd, t->bytes);

    if (t->bytes > 0)
        vsize_write(t->vsize, t->vfd, t->path, t->bytes);

    return SUCCESS;
}

status_e gioop_writev(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(WRITEV, "%ld|%ld|writev", t->mapped_fd, t->bytes);

    if (t->bytes > 0)
        vsize_write(t->vsize, t->vfd, t->path, t->bytes);

    return SUCCESS;
}

status_e gioop_lseek(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(LSEEK, "%ld|%ld|%ld|%ld|lseek",
                t->mapped_fd, t->offset, t->whence, t->bytes);

    if (t->bytes >= 0)
        vsize_seek(t->vsize, t->vfd, t->bytes);

    return SUCCESS;
}

status_e gioop_llseek(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(LLSEEK, "%ld|%ld|%ld|%ld|llseek",
                t->mapped_fd, t->offset, t->whence, t->bytes);

    if (t->bytes >= 0)
        vsize_seek(t->vsize, t->vfd, t->bytes);

    return SUCCESS;
}

status_e gioop_getdents(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(GETDENTS, "%ld|%ld|%ld|getdents",
                t->mapped_fd, t->count, t->bytes);

    return SUCCESS;
}

status_e gioop_mkdir(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(MKDIR, "%s|%d|%d|mkdir", t->path, t->mode, t->status);

    if (t->status == 0)
        vsize_mkdir(t->vsize, t->path);

    return SUCCESS;
}
status_e gioop_rmdir(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(RMDIR, "%s|%d|rmdir", t->path, t->status);

    if (t->status == 0)
        vsize_rmdir(t->vsize, t->path);

    return SUCCESS;
}
status_e gioop_mkdirat(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(MKDIR_AT, "%s|%d|%d|mkdirat", t->path, t->mode, t->status);

    if (t->status == 0)
        vsize_mkdir(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_unlink(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(UNLINK, "%s|%d|unlink", t->path, t->status);

    if (t->status == 0)
        vsize_unlink(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_unlinkat(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(UNLINK_AT, "%s|%d|unlinkat", t->path, t->status);

    if (t->status == 0)
        vsize_unlink(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_lstat(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(LSTAT, "%s|%d|lstat", t->path, t->status);

    if (t->status == 0)
        vsize_stat(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_fsync(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(FSYNC, "%ld|%d|fsync", t->mapped_fd, t->status);

    return SUCCESS;
}

status_e gioop_fdatasync(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(FDATASYNC, "%ld|%d|fdatasync", t->mapped_fd, t->status);

    return SUCCESS;
}

status_e gioop_sync(gwriter_s *w, gtask_s *t, generate_s *g)
{
    Gioop_write(SYNC, "%d|sync", t->status);

    return SUCCESS;
}

status_e gioop_syncfs(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(SYNCFS, "%ld|%d|syncfs", t->mapped_fd, t->status);

    return SUCCESS;
}

status_e gioop_sync_file_range(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(SYNC_FILE_RANGE, "%ld|%ld|%ld|%d|sync_file_range",
                t->mapped_fd, t->offset, t->bytes, t->status);

    return SUCCESS;
}

status_e gioop_fcntl(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    switch (t->F) {
    case F_GETFD:
    case F_GETFL:
    case F_SETFD:
    case F_SETFL:
        break;
    default:
        return ERROR;
        break;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(FCNTL, "%ld|%d|%d|%d|fcntl",
                t->mapped_fd, t->F, t->G, t->status);

    return SUCCESS;
}

status_e gioop_chmod(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    Gioop_write(CHMOD, "%s|%d|%d|chmod", t->path, t->mode, t->status);

    if (t->status == 0)
        vsize_stat(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_fchmod(gwriter_s *w, gtask_s *t, generate_s *g)
{
    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(FCHMOD, "%ld|%d|%d|fchmod", t->mapped_fd, t->mode, t->status);

    return SUCCESS;
}

status_e gioop_chown(gwriter_s *w, gtask_s *t, generate_s *g)
{
    const char *label = _gioop_label(t, "chown");

    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    // Hmm, maybe rename t->offset, because here it is used for the user UID
    Gioop_write(CHOWN, "%s|%ld|%d|%d|%s",
                t->path, t->offset, t->G, t->status, label);

    if (t->status == 0)
        vsize_stat(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_fchown(gwriter_s *w, gtask_s *t, generate_s *g)
{
    const char *label = _gioop_label(t, "fchown");

    if (!t->has_fd) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, t->vfd->path);
    // Hmm, maybe rename t->offset, because here it is used for the user UID
    Gioop_write(FCHOWN, "%ld|%ld|%d|%d|%s",
                t->mapped_fd, t->offset, t->G, t->status, label);

    return SUCCESS;
}

status_e gioop_lchown(gwriter_s *w, gtask_s *t, generate_s *g)
{
    const char *label = _gioop_label(t, "lchown");

    if (t->path == NULL) {
        return ERROR;
    }

    generate_vsize_by_path(g, t, NULL);
    // Hmm, maybe rename t->offset, because here it is used for the user UID
    Gioop_write(LCHOWN, "%s|%ld|%d|%d|%s",
                t->path, t->offset, t->G, t->status, label);

    if (t->status == 0)
        vsize_stat(t->vsize, t->path);

    return SUCCESS;
}

status_e gioop_exit_group(gwriter_s *w, gtask_s *t, generate_s *g)
{
    // It means that the process and all its threads terminate.
    // Therefore close all file handles of that process!
    hmap_run_cb2(t->gprocess->vfd_map, gioop_close_all_vfd_cb, t);

    // Remove virtual process from pid map and destroy it
    amap_unset(g->pid_map, t->pid);
    gprocess_destroy(t->gprocess);

    return SUCCESS;
}

void gioop_close_all_vfd_cb(void *data, void *data2)
{
    gtask_s *t = data2;
    t->vfd = data;
    generate_s *g = t->generate;

    generate_vsize_by_path(g, t, t->vfd->path);
    Gioop_write(CLOSE, "%ld|%d|close on exit_group", t->vfd->mapped_fd, 0);
    vsize_close(t->vsize, t->vfd);
}
