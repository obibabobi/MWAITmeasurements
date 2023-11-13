#ifndef SYSFS_HELPER_H
#define SYSFS_HELPER_H

#include <linux/types.h>

ssize_t format_array_into_buffer(u64 *array, char *buf);

#endif