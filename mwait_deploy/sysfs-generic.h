#ifndef SYSFS_GENERIC_H
#define SYSFS_GENERIC_H

#include "sysfs-helper.h"

#include <linux/sysfs.h>

#define output_to_sysfs(attribute_name)                                             \
	({                                                                          \
		if (strcmp(attr->name, #attribute_name) == 0)                       \
			return format_array_into_buffer(stat->attribute_name, buf); \
	})

#define create_attribute(prefix, attribute_name)                          \
	static struct attribute prefix##_##attribute_name##_attribute = { \
	    .name = #attribute_name,                                      \
	    .mode = 0444};

#endif