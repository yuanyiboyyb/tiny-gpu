#include "tinygpu/verilated_gpu.hpp"

#include <algorithm>
#include <stdexcept>

#include "Vgpu.h"
#include "verilated.h"

namespace tinygpu {

VerilatedGpu::VerilatedGpu()
    : context_(std::make_unique<VerilatedContext>()),
      dut_(std::make_unique<Vgpu>(context_.get()))
{
    reset();
}

VerilatedGpu::~VerilatedGpu()
{
    dut_->final();
}

void VerilatedGpu::cycle()
{
    dut_->clk = 0;
    dut_->program_mem_read_ready = 0;
    dut_->data_mem_read_ready = 0;
    dut_->data_mem_write_ready = 0;
    dut_->eval();

    service_program_memory();
    service_data_memory();
    dut_->eval();

    dut_->clk = 1;
    dut_->eval();
    context_->timeInc(1);
    ++cycles_;

    if (dut_->done) {
        done_ = true;
        busy_ = false;
    }
}

void VerilatedGpu::reset()
{
    dut_->start = 0;
    dut_->device_control_write_enable = 0;
    dut_->device_control_data = 0;
    dut_->program_mem_read_ready = 0;
    dut_->data_mem_read_ready = 0;
    dut_->data_mem_write_ready = 0;
    dut_->reset = 1;

    cycle();
    cycle();

    dut_->reset = 0;
    cycle();
    busy_ = false;
    done_ = false;
}

void VerilatedGpu::set_thread_count(std::uint8_t count)
{
    if (busy_)
        throw std::logic_error("cannot change thread count while GPU is busy");

    dut_->device_control_data = count;
    dut_->device_control_write_enable = 1;
    cycle();
    dut_->device_control_write_enable = 0;
}

void VerilatedGpu::start()
{
    if (busy_)
        throw std::logic_error("GPU is already running");

    done_ = false;
    busy_ = true;
    dut_->start = 1;
    cycle();
}

bool VerilatedGpu::run_until_done(std::uint64_t max_cycles)
{
    for (std::uint64_t cycle_index = 0;
         cycle_index < max_cycles && !done_; ++cycle_index) {
        cycle();
    }

    if (!done_)
        return false;

    dut_->start = 0;
    cycle();
    return true;
}

bool VerilatedGpu::busy() const noexcept
{
    return busy_;
}

bool VerilatedGpu::done() const noexcept
{
    return done_;
}

std::uint64_t VerilatedGpu::cycles() const noexcept
{
    return cycles_;
}

void VerilatedGpu::load_program(
    const std::vector<std::uint16_t>& instructions)
{
    check_range(0, instructions.size() * 2, program_memory_.size(),
                "program memory");

    program_memory_.fill(0);
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const auto instruction = instructions[index];
        program_memory_[index * 2] =
            static_cast<std::uint8_t>(instruction & 0xff);
        program_memory_[index * 2 + 1] =
            static_cast<std::uint8_t>(instruction >> 8);
    }
}

void VerilatedGpu::load_program_bytes(const std::uint8_t* source,
                                      std::size_t length,
                                      std::size_t offset)
{
    check_range(offset, length, program_memory_.size(), "program memory");
    std::copy_n(source, length, program_memory_.begin() + offset);
}

void VerilatedGpu::load_data(const std::uint8_t* source,
                             std::size_t length,
                             std::size_t offset)
{
    check_range(offset, length, data_memory_.size(), "data memory");
    std::copy_n(source, length, data_memory_.begin() + offset);
}

void VerilatedGpu::copy_data(std::uint8_t* destination,
                             std::size_t length,
                             std::size_t offset) const
{
    check_range(offset, length, data_memory_.size(), "data memory");
    std::copy_n(data_memory_.begin() + offset, length, destination);
}

std::array<std::uint8_t, TINYGPU_PROGRAM_MEMORY_SIZE>&
VerilatedGpu::program_memory()
{
    return program_memory_;
}

std::array<std::uint8_t, TINYGPU_DATA_MEMORY_SIZE>&
VerilatedGpu::data_memory()
{
    return data_memory_;
}

void VerilatedGpu::service_program_memory()
{
    if ((dut_->program_mem_read_valid & 1U) == 0)
        return;

    const auto address =
        static_cast<std::size_t>(dut_->program_mem_read_address[0]);
    if (address + 1 >= program_memory_.size())
        throw std::out_of_range("GPU program-memory read crosses boundary");

    dut_->program_mem_read_data[0] =
        static_cast<std::uint16_t>(program_memory_[address]) |
        (static_cast<std::uint16_t>(program_memory_[address + 1]) << 8);
    dut_->program_mem_read_ready = 1;
}

void VerilatedGpu::service_data_memory()
{
    constexpr unsigned channel_count = 2;
    const auto read_valid =
        static_cast<std::uint32_t>(dut_->data_mem_read_valid);
    const auto write_valid =
        static_cast<std::uint32_t>(dut_->data_mem_write_valid);
    std::uint32_t read_ready = 0;
    std::uint32_t write_ready = 0;

    for (unsigned channel = 0; channel < channel_count; ++channel) {
        const std::uint32_t channel_mask = 1U << channel;

        if ((write_valid & channel_mask) != 0) {
            const auto address = static_cast<std::size_t>(
                dut_->data_mem_write_address[channel]);
            data_memory_[address] = static_cast<std::uint8_t>(
                dut_->data_mem_write_data[channel]);
            write_ready |= channel_mask;
        } else if ((read_valid & channel_mask) != 0) {
            const auto address = static_cast<std::size_t>(
                dut_->data_mem_read_address[channel]);
            dut_->data_mem_read_data[channel] = data_memory_[address];
            read_ready |= channel_mask;
        }
    }

    dut_->data_mem_read_ready = read_ready;
    dut_->data_mem_write_ready = write_ready;
}

void VerilatedGpu::check_range(std::size_t offset, std::size_t length,
                               std::size_t capacity,
                               const char* memory_name)
{
    if (offset > capacity || length > capacity - offset)
        throw std::out_of_range(std::string(memory_name) + " access is out of range");
}

} // namespace tinygpu
