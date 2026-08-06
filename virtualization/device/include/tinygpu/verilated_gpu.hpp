#ifndef TINYGPU_VERILATED_GPU_HPP
#define TINYGPU_VERILATED_GPU_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "tinygpu_hw.h"

class VerilatedContext;
class Vgpu;

namespace tinygpu {

class VerilatedGpu {
public:
    VerilatedGpu();
    ~VerilatedGpu();

    VerilatedGpu(const VerilatedGpu&) = delete;
    VerilatedGpu& operator=(const VerilatedGpu&) = delete;

    void reset();
    void cycle();
    void set_thread_count(std::uint8_t count);
    void start();
    bool run_until_done(std::uint64_t max_cycles);

    bool busy() const noexcept;
    bool done() const noexcept;
    std::uint64_t cycles() const noexcept;

    void load_program(const std::vector<std::uint16_t>& instructions);
    void load_program_bytes(const std::uint8_t* source, std::size_t length,
                            std::size_t offset = 0);
    void load_data(const std::uint8_t* source, std::size_t length,
                   std::size_t offset = 0);
    void copy_data(std::uint8_t* destination, std::size_t length,
                   std::size_t offset = 0) const;

    std::array<std::uint8_t, TINYGPU_PROGRAM_MEMORY_SIZE>& program_memory();
    std::array<std::uint8_t, TINYGPU_DATA_MEMORY_SIZE>& data_memory();

private:
    void service_program_memory();
    void service_data_memory();
    static void check_range(std::size_t offset, std::size_t length,
                            std::size_t capacity, const char* memory_name);

    std::unique_ptr<VerilatedContext> context_;
    std::unique_ptr<Vgpu> dut_;
    std::array<std::uint8_t, TINYGPU_PROGRAM_MEMORY_SIZE> program_memory_{};
    std::array<std::uint8_t, TINYGPU_DATA_MEMORY_SIZE> data_memory_{};
    std::uint64_t cycles_ = 0;
    bool busy_ = false;
    bool done_ = false;
};

} // namespace tinygpu

#endif
