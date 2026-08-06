#include "tinygpu/runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

using Gpu = tinygpu::runtime::tinygpu;

struct KernelArgs {
    Gpu::device_ptr input_a;
    Gpu::device_ptr input_b;
    Gpu::device_ptr output;
};

static_assert(sizeof(KernelArgs) == 3);
static_assert(offsetof(KernelArgs, input_a) == 0);
static_assert(offsetof(KernelArgs, input_b) == 1);
static_assert(offsetof(KernelArgs, output) == 2);

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: tinygpu-runtime-matadd <matadd.bin> [/dev/tinygpuN]\n";
        return 1;
    }

    const std::string device = argc == 3 ? argv[2] : "/dev/tinygpu0";
    std::array<std::uint8_t, 8> input_a{0, 1, 2, 3, 4, 5, 6, 7};
    std::array<std::uint8_t, 8> input_b{0, 1, 2, 3, 4, 5, 6, 7};
    std::array<std::uint8_t, 8> output{};

    try {
        Gpu gpu(sizeof(KernelArgs), device);
        const auto input_a_ptr =
            gpu.allocate_input(input_a.data(), input_a.size());
        const auto input_b_ptr =
            gpu.allocate_input(input_b.data(), input_b.size());
        const auto output_ptr = gpu.allocate_output(output.size());

        gpu.set_kernel(KernelArgs{input_a_ptr, input_b_ptr, output_ptr});
        gpu.set_output(output.data(), output_ptr, output.size());
        gpu.load_binary(argv[1]);
        gpu.set_launch_params(8, 1000);
        gpu.run();

        for (const auto value : output) {
            std::cout << static_cast<unsigned>(value) << ' ';
        }
        std::cout << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
