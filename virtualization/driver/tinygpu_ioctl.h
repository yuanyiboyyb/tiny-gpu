#ifndef TINYGPU_IOCTL_H
#define TINYGPU_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define TINYGPU_IOCTL_MAGIC 'T'

struct tinygpu_submit {
	__u64 program_ptr;
	__u32 program_size;
	__u32 thread_count;

	__u64 input_ptr;
	__u32 input_size;
	__u32 input_offset;

	__u64 output_ptr;
	__u32 output_size;
	__u32 output_offset;

	__u32 timeout_ms;
	__u32 reserved;
};

#define TINYGPU_IOCTL_SUBMIT \
	_IOWR(TINYGPU_IOCTL_MAGIC, 0x00, struct tinygpu_submit)

#endif
