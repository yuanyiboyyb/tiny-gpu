#ifndef TINYGPU_RUNTIME_HPP
#define TINYGPU_RUNTIME_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace tinygpu::runtime {

class tinygpu {
public:
    using device_ptr = std::uint8_t;

    static constexpr std::size_t program_memory_size = 256;
    static constexpr std::size_t data_memory_size = 256;

    explicit tinygpu(std::size_t kernel_size,
                     std::string device_path = "/dev/tinygpu0");
    ~tinygpu();

    tinygpu(const tinygpu &) = delete;
    tinygpu &operator=(const tinygpu &) = delete;
    tinygpu(tinygpu &&) = delete;
    tinygpu &operator=(tinygpu &&) = delete;

    void set_launch_params(std::uint32_t thread_count,
                           std::uint32_t timeout_ms = 1000);
    void load_binary(const std::string &path);

    device_ptr allocate_input(const void *source, std::size_t size,
                              std::size_t alignment = 1);
    device_ptr allocate_output(std::size_t size, std::size_t alignment = 1);

    void set_kernel_bytes(const void *kernel, std::size_t size);

    template <typename Kernel>
    void set_kernel(const Kernel &kernel)
    {
        static_assert(std::is_trivially_copyable_v<Kernel>,
                      "kernel parameters must be trivially copyable");
        set_kernel_bytes(&kernel, sizeof(Kernel));
    }

    void set_output(void *host_output, device_ptr device_address,
                    std::size_t size);

    // Synchronous: returns only after the driver's submit ioctl reports
    // completion. Errors, interruption and timeout are reported as exceptions.
    void run();

    std::size_t kernel_size() const noexcept;
    std::size_t allocated_data_size() const noexcept;
    std::size_t program_size() const noexcept;
    const std::uint8_t *data_image() const noexcept;

private:
    device_ptr allocate(std::size_t size, std::size_t alignment);
    void ensure_open();

    std::string device_path_;
    int fd_ = -1;
    std::size_t kernel_size_ = 0;
    std::size_t next_data_offset_ = 0;
    std::array<std::uint8_t, data_memory_size> data_image_{};
    std::vector<std::uint8_t> program_;
    std::uint32_t thread_count_ = 1;
    std::uint32_t timeout_ms_ = 1000;
    void *host_output_ = nullptr;
    std::size_t output_offset_ = 0;
    std::size_t output_size_ = 0;
    bool kernel_set_ = false;
    bool output_set_ = false;
};

}  // namespace tinygpu::runtime

#endif
