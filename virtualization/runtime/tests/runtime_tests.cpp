#include "tinygpu/runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using Runtime = tinygpu::runtime::tinygpu;

struct KernelArgs {
    Runtime::device_ptr input_a;
    Runtime::device_ptr input_b;
    Runtime::device_ptr output;
};

static_assert(sizeof(KernelArgs) == 3);
static_assert(offsetof(KernelArgs, input_a) == 0);
static_assert(offsetof(KernelArgs, input_b) == 1);
static_assert(offsetof(KernelArgs, output) == 2);

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
    }
    return condition;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "usage: tinygpu-runtime-tests <program.bin>\n";
        return EXIT_FAILURE;
    }
    Runtime gpu(sizeof(KernelArgs));
    const std::array<std::uint8_t, 8> a = {0, 1, 2, 3, 4, 5, 6, 7};
    const std::array<std::uint8_t, 8> b = {7, 6, 5, 4, 3, 2, 1, 0};
    std::array<std::uint8_t, 8> output{};

    const auto a_ptr = gpu.allocate_input(a.data(), a.size());
    const auto b_ptr = gpu.allocate_input(b.data(), b.size());
    const auto output_ptr = gpu.allocate_output(output.size());
    gpu.set_kernel(KernelArgs{a_ptr, b_ptr, output_ptr});
    gpu.set_output(output.data(), output_ptr, output.size());
    gpu.set_launch_params(8, 1000);
    gpu.load_binary(argv[1]);

    return expect(a_ptr == 3, "first input must follow kernel") &&
                   expect(b_ptr == 11, "second input must follow first input") &&
                   expect(output_ptr == 19, "output must follow second input") &&
                   expect(gpu.allocated_data_size() == 27, "allocated data size") &&
                   expect(gpu.program_size() == 36, "matadd program size") &&
                   expect(gpu.data_image()[0] == 3 && gpu.data_image()[1] == 11 &&
                              gpu.data_image()[2] == 19,
                          "kernel bytes at data-memory start")
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
