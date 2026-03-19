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

#include "utils.h"

#include <sys/resource.h>
#include <sys/time.h>

static rlim_t _limit_target(const rlim_t requested, const rlim_t hard_limit)
{
    if (hard_limit == RLIM_INFINITY || requested <= hard_limit)
        return requested;

    return hard_limit;
}

static void _set_limit_or_cap(const int resource, const char *label,
                              const rlim_t requested)
{
    struct rlimit rl;

    if (0 != getrlimit(resource, &rl)) {
        Errno("Could not read %s limits", label);
    }

    rlim_t target = _limit_target(requested, rl.rlim_max);
    if (target != requested) {
        Warn("Requested %s of '%llu' exceeds hard limit '%llu', using '%llu'",
             label,
             (unsigned long long) requested,
             (unsigned long long) rl.rlim_max,
             (unsigned long long) target);
    }

    rl.rlim_cur = rl.rlim_max = target;
    if (0 != setrlimit(resource, &rl)) {
        Errno("Could not set %s to '%llu'!",
              label, (unsigned long long) target)
    }
}

static struct passwd *_lookup_user_or_null(const char *user)
{
    if (user == NULL)
        return NULL;

    return getpwnam(user);
}

void* notnull(void *p, char *file, int line, int count)
{
    if (p == NULL) {
        Errno("%s:%d count:%d Could not allocate memory", file, line, count);
    }
    return p;
}


FILE* fnotnull(FILE *fd, const char *path, char *file, int line)
{
    if (fd == NULL) {
        Errno("%s:%d Could not open file '%s'", file, line, path);
    }
    return fd;
}

void* mmapok(void *p, char *file, int line)
{
    if (p == MAP_FAILED) {
        Errno("%s:%d: Mmap failed", file, line);
    }
    return p;
}

char* strtok2_r(char *str, char *delim, char **saveptr)
{
    int len = strlen(delim);

    if (str == NULL) {
        if (*saveptr == NULL)
            return NULL;
        str = *saveptr;
    }

    while (str[0] == '\n' || str[0] == '\r')
        str++;

    if (str[0] == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    char *next = strstr(str, delim);
    if (next) {
        next[0] = '\0';
        for (int i = 0; i < len; ++i)
            next++;
        while (next[0] == '\n' || next[0] == '\r')
            next++;
        *saveptr = next;
        return str;
    }

    int str_len = strlen(str);
    while (str_len > 0 &&
           (str[str_len-1] == '\n' || str[str_len-1] == '\r')) {
        str[str_len-1] = '\0';
        str_len--;
    }

    if (str[0] == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    *saveptr = NULL;
    return str;
}

void chreplace(char *str, char replace, char with)
{
    for (int i = 0; ; ++i) {
        if (str[i] == '\0')
            break;
        if (str[i] == replace)
            str[i] = with;
    }
}

void strunquote(char *str)
{
    int len = strlen(str);

    if (str[0] == '"') {
        if (str[len-1] == '"')
            str[len-1] = '\0';
        for (int i = 1; i < len; ++i)
            str[i-1] = str[i];
    }
}

void set_limits_drop_root(const char *user)
{
    if (getuid() == 0) {
        _set_limit_or_cap(RLIMIT_NOFILE, "RLIMIT_NOFILE", SET_RLIMIT_NOFILE);
        _set_limit_or_cap(RLIMIT_NPROC, "RLIMIT_NPROC", SET_RLIMIT_NPROC);

        Error_if(user == NULL, "No user specified while dropping privileges");

        if (!Eq("root", user)) {
            Put("Dropping root privileges to user '%s'", user);
            struct passwd *pw = _lookup_user_or_null(user);
            Error_if(pw == NULL, "Unable to resolve user '%s'", user);

            /* process is running as root, drop privileges */
            if (setgid(pw->pw_gid) != 0) {
                Errno("Unable to drop group privileges!");
            }
            if (setuid(pw->pw_uid) != 0) {
                Errno("Unable to drop user privileges!");
            }
        }
    }

    /*
       getrlimit(RLIMIT_NOFILE, &rl);
       Put("Max open files: '%lld'", (long long) rl.rlim_cur);
       getrlimit(RLIMIT_NPROC, &rl);
       Put("Max open processes : '%lld'", (long long) rl.rlim_cur);
       */
}

void get_loadavg_s(char *readbuf)
{
    FILE *fp = Fopen("/proc/loadavg", "r");
    fgets(readbuf, 128, fp);
    char *pos = strchr(readbuf, ' ');
    pos[0] = '\0';
    fclose(fp);
}

double get_loadavg()
{
    // Not thread safe, but multi processing safe
    static char buf[128];
    get_loadavg_s(buf);

    return atof(buf);
}

bool is_number(char *str)
{
    for (int i = 0; ; ++i) {
        if (str[i] == '\0')
            return true;
        if (isdigit(str[i]) == 0 && str[i] != '-')
            return false;
    }

    return true;
}

void start_pthread(pthread_t *thread, void*(*cb)(void*), void *data)
{
    int rc = pthread_create(thread, NULL, cb, data);

    switch (rc) {
    case 0:
        break;
    case EAGAIN:
        Error("Out of resources while creating pthread (%d)", rc);
        break;
    case EINVAL:
        Error("Ivalid settings while creating pthread (%d)", rc);
        break;
    case EPERM:
        Error("No permissions to configure pthread (%d)", rc);
    default:
        Error("Unknown error while creating pthread (%d)", rc);
        break;
    }
}

void utils_test(void)
{
    char tokens_without_trailer[] = "t=1;:,c=2";
    char *saveptr = NULL;
    assert(Eq("t=1", strtok2_r(tokens_without_trailer, ";:,", &saveptr)));
    assert(Eq("c=2", strtok2_r(NULL, ";:,", &saveptr)));
    assert(NULL == strtok2_r(NULL, ";:,", &saveptr));

    char tokens_with_trailer[] = "t=1;:,c=2;:,";
    saveptr = NULL;
    assert(Eq("t=1", strtok2_r(tokens_with_trailer, ";:,", &saveptr)));
    assert(Eq("c=2", strtok2_r(NULL, ";:,", &saveptr)));
    assert(NULL == strtok2_r(NULL, ";:,", &saveptr));

    char tokens_without_trailer_newline[] = "t=1;:,c=2\n";
    saveptr = NULL;
    assert(Eq("t=1", strtok2_r(tokens_without_trailer_newline, ";:,",
                               &saveptr)));
    assert(Eq("c=2", strtok2_r(NULL, ";:,", &saveptr)));
    assert(NULL == strtok2_r(NULL, ";:,", &saveptr));

    char tokens_with_trailer_newline[] = "t=1;:,c=2;:,\n";
    saveptr = NULL;
    assert(Eq("t=1", strtok2_r(tokens_with_trailer_newline, ";:,",
                               &saveptr)));
    assert(Eq("c=2", strtok2_r(NULL, ";:,", &saveptr)));
    assert(NULL == strtok2_r(NULL, ";:,", &saveptr));

    assert(_limit_target(100, 50) == 50);
    assert(_limit_target(100, RLIM_INFINITY) == 100);
    assert(NULL == _lookup_user_or_null("ioriot-definitely-missing-user"));

    if (getuid() == 0) {
        struct rlimit rl;
        getrlimit(RLIMIT_NOFILE, &rl);
        rlim_t expected_nofile = _limit_target(SET_RLIMIT_NOFILE, rl.rlim_max);
        getrlimit(RLIMIT_NPROC, &rl);
        rlim_t expected_nproc = _limit_target(SET_RLIMIT_NPROC, rl.rlim_max);

        set_limits_drop_root("nobody");

        getrlimit(RLIMIT_NOFILE, &rl);
        assert(rl.rlim_cur == expected_nofile);
        assert(rl.rlim_max == expected_nofile);

        getrlimit(RLIMIT_NPROC, &rl);
        assert(rl.rlim_cur == expected_nproc);
        assert(rl.rlim_max == expected_nproc);
    }
}
