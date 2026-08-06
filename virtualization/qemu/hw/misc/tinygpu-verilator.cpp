#include "qemu/osdep.h"

#include "tinygpu-verilator.h"

#include <exception>

#include "tinygpu/verilated_gpu.hpp"

extern "C" int tinygpu_verilator_run(
    const uint8_t *program, size_t program_length,
    const uint8_t *input, size_t input_length, size_t input_offset,
    uint8_t *output, size_t output_length, size_t output_offset,
    uint32_t thread_count, char *error, size_t error_size)
{
    constexpr uint64_t max_cycles = 10000000;

    try {
        tinygpu::VerilatedGpu gpu;

        gpu.load_program_bytes(program, program_length);
        gpu.load_data(input, input_length, input_offset);
        gpu.set_thread_count(static_cast<uint8_t>(thread_count));
        gpu.start();
        if (!gpu.run_until_done(max_cycles)) {
            snprintf(error, error_size,
                     "Verilator model timed out after %" PRIu64 " cycles",
                     max_cycles);
            return -1;
        }
        gpu.copy_data(output, output_length, output_offset);
        return 0;
    } catch (const std::exception &exception) {
        snprintf(error, error_size, "Verilator model failed: %s",
                 exception.what());
        return -1;
    }
}
