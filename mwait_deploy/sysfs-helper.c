#include "sysfs-helper.h"

#include <linux/kernel.h>
#include <asm/page.h>

extern int measurement_count;

ssize_t format_array_into_buffer(u64 *array, char *buf)
{
	int bytes_written = 0;
	int i = 0;

	while (bytes_written < PAGE_SIZE && i < measurement_count)
	{
		bytes_written += scnprintf(buf + bytes_written, PAGE_SIZE - bytes_written, "%llu\n", array[i]);
		++i;
	}
	return bytes_written;
}
