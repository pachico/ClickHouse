#include <Common/FakeTime.h>

#if defined(__linux__) && defined(__x86_64__)

#include <cassert>
#include <unistd.h>
#include <sys/syscall.h>


static int64_t offset = 0;


/// We cannot include stdlib.h and similar to avoid clashes with overridden function names.

extern "C" char * getenv(const char *);

namespace DB
{
    FakeTime::FakeTime()
    {
        const char * env = getenv("FAKE_TIME_OFFSET");
        if (!env)
            return;

        /// Manual and dirty parsing of the env variable to avoid including more headers.

        bool negative = false;
        while (*env)
        {
            if (*env == '-')
            {
                negative = true;
            }
            else
            {
                offset *= 10;
                offset += *env - '0';
            }
            ++env;
        }
        if (negative)
            offset = -offset;
    }

    bool FakeTime::isEffective() const
    {
        return offset != 0;
    }
}


/// The constants and structure layout correspond to the ABI of Linux Kernel and libc on amd64.

extern "C"
{

#define CLOCK_REALTIME 0
#define AT_FDCWD -100
#define AT_EMPTY_PATH 0x1000
#define AT_SYMLINK_NOFOLLOW 0x100

using time_t = int64_t;

struct timespec
{
    time_t tv_sec;
    long tv_nsec;
};

struct timeval
{
    time_t tv_sec;
    long tv_usec;
};

int __clock_gettime(int32_t clk_id, struct timespec * tp);

int clock_gettime(int32_t clk_id, struct timespec * tp)
{
    int res = __clock_gettime(clk_id, tp);
    if (0 == res)
        tp->tv_sec += offset;
    return res;
}

int gettimeofday(struct timeval * tv, void *)
{
    timespec tp;
    int res = __clock_gettime(CLOCK_REALTIME, &tp);
    if (0 == res)
    {
        tv->tv_sec = tp.tv_sec + offset;
        tv->tv_usec = tp.tv_nsec / 1000;
    }
    return res;
}

time_t time(time_t * tloc)
{
    timespec tp;
    int res = __clock_gettime(CLOCK_REALTIME, &tp);
    assert(0 == res);
    time_t t = tp.tv_sec + offset;
    if (tloc)
        *tloc = t;
    return t;
}

struct stat
{
    char pad[72];

    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
};

static int fstatat(int fd, const char * pathname, struct stat * statbuf, int flags)
{
    int res = syscall(__NR_newfstatat, fd, pathname, statbuf, flags);
    if (0 == res)
    {
        statbuf->st_atim.tv_sec += offset;
        statbuf->st_mtim.tv_sec += offset;
        statbuf->st_ctim.tv_sec += offset;
    }
    return res;
}

int stat(const char * pathname, struct stat * statbuf)
{
    return fstatat(AT_FDCWD, pathname, statbuf, 0);
}

int fstat(int fd, struct stat * statbuf)
{
    return fstatat(fd, "", statbuf, AT_EMPTY_PATH);
}

int lstat(const char * pathname, struct stat * statbuf)
{
    return fstatat(AT_FDCWD, pathname, statbuf, AT_SYMLINK_NOFOLLOW);
}

int stat64(const char * pathname, struct stat * statbuf)
{
    return stat(pathname, statbuf);
}

int fstat64(int fd, struct stat * statbuf)
{
    return fstat(fd, statbuf);
}

int fstatat64(int fd, const char * pathname, struct stat * statbuf, int flags)
{
    return fstatat(fd, pathname, statbuf, flags);
}

int lstat64(const char * pathname, struct stat * statbuf)
{
    return lstat(pathname, statbuf);
}

}

#else

namespace DB
{
    FakeTime::FakeTime()
    {
    }

    bool FakeTime::isEffective() const
    {
        return false;
    }
}

#endif