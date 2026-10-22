/* Only the unsupported perf-counter path uses timerfd. */
#ifndef SWEET_TIMERFD_H
#define SWEET_TIMERFD_H
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#define TFD_CLOEXEC O_CLOEXEC
#define TFD_NONBLOCK O_NONBLOCK
struct itimerspec { struct timespec it_interval, it_value; };
static inline int timerfd_create(clockid_t clock, int flags)
{ (void)clock; (void)flags; errno = ENOSYS; return -1; }
static inline int timerfd_settime(int fd, int flags, const struct itimerspec *value, struct itimerspec *old)
{ (void)fd; (void)flags; (void)value; (void)old; errno = ENOSYS; return -1; }
#endif
