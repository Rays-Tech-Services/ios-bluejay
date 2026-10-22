/* Panfrost perf counters are unsupported by the iOS bridge. */
#ifndef SWEET_EVENTFD_H
#define SWEET_EVENTFD_H
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
typedef uint64_t eventfd_t;
#define EFD_CLOEXEC O_CLOEXEC
#define EFD_NONBLOCK O_NONBLOCK
static inline int eventfd(unsigned value, int flags)
{ (void)value; (void)flags; errno = ENOSYS; return -1; }
static inline int eventfd_read(int fd, eventfd_t *value)
{ (void)fd; (void)value; errno = ENOSYS; return -1; }
static inline int eventfd_write(int fd, eventfd_t value)
{ (void)fd; (void)value; errno = ENOSYS; return -1; }
#endif
