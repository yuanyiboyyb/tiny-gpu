#include "tinygpu/runtime.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <sys/ioctl.h>
#include <unistd.h>

#include "tinygpu_ioctl.h"

namespace tinygpu::runtime {
namespace {

bool is_power_of_two(std::size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

std::size_t align_up(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

std::uint64_t user_pointer(const void *pointer)
{
    return static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(pointer));
}

}  // namespace

tinygpu::tinygpu(std::size_t kernel_size, std::string device_path)
    : device_path_(std::move(device_path)),
      kernel_size_(kernel_size),
      next_data_offset_(kernel_size)
{
    if (kernel_size == 0 || kernel_size > data_memory_size) {
        throw std::invalid_argument("kernel size must be between 1 and 256 bytes");
    }
}

tinygpu::~tinygpu()
{
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void tinygpu::set_launch_params(std::uint32_t thread_count,
                                std::uint32_t timeout_ms)
{
    if (thread_count == 0 || thread_count > 255) {
        throw std::invalid_argument("thread count must be between 1 and 255");
    }
    thread_count_ = thread_count;
    timeout_ms_ = timeout_ms;
}

void tinygpu::load_binary(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open TinyGPU binary '" + path + "'");
    }

    program_.assign(std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>());
    if (input.bad()) {
        throw std::runtime_error("failed to read TinyGPU binary '" + path + "'");
    }
    if (program_.empty()) {
        throw std::invalid_argument("TinyGPU binary is empty");
    }
    if (program_.size() > program_memory_size) {
        throw std::invalid_argument("TinyGPU binary exceeds 256-byte program memory");
    }
    if ((program_.size() & 1u) != 0) {
        throw std::invalid_argument("TinyGPU binary must contain whole 16-bit instructions");
    }
}

tinygpu::device_ptr tinygpu::allocate(std::size_t size, std::size_t alignment)
{
    if (size == 0) {
        throw std::invalid_argument("allocation size must be non-zero");
    }
    if (!is_power_of_two(alignment)) {
        throw std::invalid_argument("allocation alignment must be a power of two");
    }

    const std::size_t offset = align_up(next_data_offset_, alignment);
    if (offset >= data_memory_size || size > data_memory_size - offset) {
        throw std::bad_alloc();
    }
    next_data_offset_ = offset + size;
    return static_cast<device_ptr>(offset);
}

tinygpu::device_ptr tinygpu::allocate_input(const void *source,
                                            std::size_t size,
                                            std::size_t alignment)
{
    if (!source) {
        throw std::invalid_argument("input source pointer is null");
    }
    const device_ptr address = allocate(size, alignment);
    std::memcpy(data_image_.data() + address, source, size);
    return address;
}

tinygpu::device_ptr tinygpu::allocate_output(std::size_t size,
                                             std::size_t alignment)
{
    const device_ptr address = allocate(size, alignment);
    std::memset(data_image_.data() + address, 0, size);
    return address;
}

void tinygpu::set_kernel_bytes(const void *kernel, std::size_t size)
{
    if (!kernel) {
        throw std::invalid_argument("kernel pointer is null");
    }
    if (size != kernel_size_) {
        throw std::invalid_argument("kernel object size does not match reserved kernel size");
    }
    std::memcpy(data_image_.data(), kernel, size);
    kernel_set_ = true;
}

void tinygpu::set_output(void *host_output, device_ptr device_address,
                         std::size_t size)
{
    const std::size_t offset = device_address;
    if (!host_output) {
        throw std::invalid_argument("host output pointer is null");
    }
    if (size == 0 || offset >= next_data_offset_ ||
        size > next_data_offset_ - offset) {
        throw std::out_of_range("output range is not inside allocated data memory");
    }
    host_output_ = host_output;
    output_offset_ = offset;
    output_size_ = size;
    output_set_ = true;
}

void tinygpu::ensure_open()
{
    if (fd_ >= 0) {
        return;
    }
    fd_ = ::open(device_path_.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "failed to open " + device_path_);
    }
}

void tinygpu::run()
{
    if (program_.empty()) {
        throw std::logic_error("load_binary must be called before run");
    }
    if (!kernel_set_) {
        throw std::logic_error("set_kernel must be called before run");
    }
    if (!output_set_) {
        throw std::logic_error("set_output must be called before run");
    }

    ensure_open();

    tinygpu_submit submit{};
    submit.program_ptr = user_pointer(program_.data());
    submit.program_size = static_cast<__u32>(program_.size());
    submit.thread_count = static_cast<__u32>(thread_count_);
    submit.input_ptr = user_pointer(data_image_.data());
    submit.input_size = static_cast<__u32>(next_data_offset_);
    submit.input_offset = 0;
    submit.output_ptr = user_pointer(host_output_);
    submit.output_size = static_cast<__u32>(output_size_);
    submit.output_offset = static_cast<__u32>(output_offset_);
    submit.timeout_ms = static_cast<__u32>(timeout_ms_);
    submit.reserved = 0;

    // TINYGPU_IOCTL_SUBMIT is synchronous. The driver sleeps on its waitqueue
    // until the completion/error IRQ, timeout, or a signal wakes it.
    if (::ioctl(fd_, TINYGPU_IOCTL_SUBMIT, &submit) < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "TinyGPU submit failed");
    }
}

std::size_t tinygpu::kernel_size() const noexcept
{
    return kernel_size_;
}

std::size_t tinygpu::allocated_data_size() const noexcept
{
    return next_data_offset_;
}

std::size_t tinygpu::program_size() const noexcept
{
    return program_.size();
}

const std::uint8_t *tinygpu::data_image() const noexcept
{
    return data_image_.data();
}

}  // namespace tinygpu::runtime
