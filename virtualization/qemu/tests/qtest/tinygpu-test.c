/*
 * QTest testcase for the tiny-gpu PCI device
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "hw/pci/pci_regs.h"
#include "qemu/timer.h"

#include "../../../tiny-gpu/virtualization/common/tinygpu_hw.h"

#define TINYGPU_DEVFN QPCI_DEVFN(4, 0)
#define PROGRAM_ADDR  0x100000
#define INPUT_ADDR    0x101000
#define OUTPUT_ADDR   0x102000

static void tinygpu_write_addr(QPCIDevice *dev, QPCIBar bar,
                               uint32_t low_reg, uint32_t high_reg,
                               uint64_t address)
{
    qpci_io_writel(dev, bar, low_reg, address);
    qpci_io_writel(dev, bar, high_reg, address >> 32);
}

static void tinygpu_test_kernel(const uint8_t *program, size_t program_size,
                                const uint8_t *input, size_t input_size,
                                const uint8_t *expected, size_t output_size,
                                uint32_t output_offset,
                                uint32_t thread_count)
{
    g_autofree uint8_t *output = g_malloc0(output_size);
    QTestState *qts;
    QPCIBus *pcibus;
    QPCIDevice *dev;
    QPCIBar bar;
    uint32_t status;

    qts = qtest_init("-machine q35 -m 64M "
                     "-device tinygpu,addr=04.0,exec-delay-ms=1");
    pcibus = qpci_new_pc(qts, NULL);
    dev = qpci_device_find(pcibus, TINYGPU_DEVFN);
    g_assert_nonnull(dev);

    g_assert_cmphex(qpci_config_readw(dev, PCI_VENDOR_ID), ==,
                    TINYGPU_PCI_VENDOR_ID);
    g_assert_cmphex(qpci_config_readw(dev, PCI_DEVICE_ID), ==,
                    TINYGPU_PCI_DEVICE_ID);

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, TINYGPU_BAR_INDEX, NULL);
    g_assert_cmphex(qpci_io_readl(dev, bar, TINYGPU_REG_ID), ==,
                    TINYGPU_DEVICE_ID_VALUE);

    qtest_memwrite(qts, PROGRAM_ADDR, program, program_size);
    qtest_memwrite(qts, INPUT_ADDR, input, input_size);

    qpci_io_writel(dev, bar, TINYGPU_REG_THREAD_COUNT, thread_count);
    tinygpu_write_addr(dev, bar, TINYGPU_REG_PROGRAM_ADDR_LO,
                       TINYGPU_REG_PROGRAM_ADDR_HI, PROGRAM_ADDR);
    qpci_io_writel(dev, bar, TINYGPU_REG_PROGRAM_LENGTH, program_size);
    tinygpu_write_addr(dev, bar, TINYGPU_REG_INPUT_ADDR_LO,
                       TINYGPU_REG_INPUT_ADDR_HI, INPUT_ADDR);
    qpci_io_writel(dev, bar, TINYGPU_REG_INPUT_LENGTH, input_size);
    qpci_io_writel(dev, bar, TINYGPU_REG_INPUT_OFFSET, 0);
    tinygpu_write_addr(dev, bar, TINYGPU_REG_OUTPUT_ADDR_LO,
                       TINYGPU_REG_OUTPUT_ADDR_HI, OUTPUT_ADDR);
    qpci_io_writel(dev, bar, TINYGPU_REG_OUTPUT_LENGTH, output_size);
    qpci_io_writel(dev, bar, TINYGPU_REG_OUTPUT_OFFSET, output_offset);

    qpci_io_writel(dev, bar, TINYGPU_REG_CONTROL, TINYGPU_CONTROL_START);
    status = qpci_io_readl(dev, bar, TINYGPU_REG_STATUS);
    g_assert_cmphex(status, ==, TINYGPU_STATUS_BUSY);

    qtest_clock_step(qts, 2 * 1000 * 1000);

    status = qpci_io_readl(dev, bar, TINYGPU_REG_STATUS);
    g_assert_cmphex(status, ==, TINYGPU_STATUS_DONE);
    qtest_memread(qts, OUTPUT_ADDR, output, output_size);
    g_assert_cmpmem(output, output_size, expected, output_size);

    qpci_iounmap(dev, bar);
    g_free(dev);
    qpci_free_pc(pcibus);
    qtest_quit(qts);
}

static void test_matadd(void)
{
    static const uint8_t program[] = {
        0xde, 0x50, 0x0f, 0x30, 0x00, 0x91, 0x08, 0x92,
        0x10, 0x93, 0x10, 0x34, 0x40, 0x74, 0x20, 0x35,
        0x50, 0x75, 0x45, 0x36, 0x30, 0x37, 0x76, 0x80,
        0x00, 0xf0,
    };
    static const uint8_t input[] = {
        0, 1, 2, 3, 4, 5, 6, 7,
        0, 1, 2, 3, 4, 5, 6, 7,
    };
    static const uint8_t expected[] = {
        0, 2, 4, 6, 8, 10, 12, 14,
    };

    tinygpu_test_kernel(program, sizeof(program), input, sizeof(input),
                        expected, sizeof(expected), 16, 8);
}

static void test_matmul(void)
{
    static const uint8_t program[] = {
        0xde, 0x50, 0x0f, 0x30, 0x01, 0x91, 0x02, 0x92,
        0x00, 0x93, 0x04, 0x94, 0x08, 0x95, 0x02, 0x66,
        0x62, 0x57, 0x07, 0x47, 0x00, 0x98, 0x00, 0x99,
        0x62, 0x5a, 0xa9, 0x3a, 0xa3, 0x3a, 0xa0, 0x7a,
        0x92, 0x5b, 0xb7, 0x3b, 0xb4, 0x3b, 0xb0, 0x7b,
        0xab, 0x5c, 0x8c, 0x38, 0x91, 0x39, 0x92, 0x20,
        0x18, 0x18, 0x50, 0x39, 0x98, 0x80, 0x00, 0xf0,
    };
    static const uint8_t input[] = {
        1, 2, 3, 4,
        1, 2, 3, 4,
    };
    static const uint8_t expected[] = { 7, 10, 15, 22 };

    tinygpu_test_kernel(program, sizeof(program), input, sizeof(input),
                        expected, sizeof(expected), 8, 4);
}

static void test_matmul_branch_join(void)
{
    static const uint8_t program[] = {
        0xde, 0x50, 0x0f, 0x30, 0x01, 0x91, 0x02, 0x92,
        0x00, 0x93, 0x04, 0x94, 0x08, 0x95, 0x02, 0x66,
        0x62, 0x57, 0x07, 0x47, 0x00, 0x98, 0x00, 0x99,
        0x62, 0x5a, 0xa9, 0x3a, 0xa3, 0x3a, 0xa0, 0x7a,
        0x92, 0x5b, 0xb7, 0x3b, 0xb4, 0x3b, 0xb0, 0x7b,
        0xab, 0x5c, 0x8c, 0x38, 0x91, 0x39, 0x92, 0x20,
        0x18, 0x18, 0x0a, 0x9a, 0x8a, 0x20, 0x3a, 0x12,
        0x3c, 0x1e, 0x00, 0x98, 0x00, 0xa0, 0x50, 0x39,
        0x98, 0x80, 0x00, 0xf0,
    };
    static const uint8_t input[] = {
        1, 2, 3, 4,
        1, 2, 3, 4,
    };
    static const uint8_t expected[] = { 7, 10, 0, 0 };

    tinygpu_test_kernel(program, sizeof(program), input, sizeof(input),
                        expected, sizeof(expected), 8, 4);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/tinygpu/matadd", test_matadd);
    qtest_add_func("/tinygpu/matmul", test_matmul);
    qtest_add_func("/tinygpu/matmul-branch-join", test_matmul_branch_join);

    return g_test_run();
}
