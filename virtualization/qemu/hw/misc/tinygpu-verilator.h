#ifndef HW_MISC_TINYGPU_VERILATOR_H
#define HW_MISC_TINYGPU_VERILATOR_H

#include <stddef.h>
#include <stdint.h>

#define TINYGPU_VERILATOR_ERROR_SIZE 128

#ifdef __cplusplus
extern "C" {
#endif

int tinygpu_verilator_run(const uint8_t *program, size_t program_length,
                          const uint8_t *input, size_t input_length,
                          size_t input_offset, uint8_t *output,
                          size_t output_length, size_t output_offset,
                          uint32_t thread_count, char *error,
                          size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
