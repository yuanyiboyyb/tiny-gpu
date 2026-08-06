// KernelArgs occupies data-memory addresses 0, 1 and 2.
// Each member is an 8-bit pointer into TinyGPU data memory.
global KernelArgs {
    u8 *input_a;
    u8 *input_b;
    u8 *output;
};

u8 main() {
    u8 index = blockIdx * blockDim + threadIdx;
    KernelArgs.output[index] =
        KernelArgs.input_a[index] + KernelArgs.input_b[index];
    return 0;
}
