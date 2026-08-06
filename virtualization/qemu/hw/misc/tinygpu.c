/*
 * tiny-gpu PCIe device.
 *
 * This is the QEMU-side PCIe/MMIO/DMA shell.  It follows the same style as the
 * qemu-camp GPGPU device: a PCIe endpoint, BAR0 control registers, asynchronous
 * completion through a QEMU timer, MSI-X/MSI/INTx notification, and VMState.
 *
 * CONTROL_START runs the sibling tiny-gpu Verilator model and transfers its
 * program and data memories through PCI DMA.
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qom/object.h"

#include "tinygpu-verilator.h"

/* qemu-x86 and tiny-gpu are expected to be sibling directories. */
#include "../../../tiny-gpu/virtualization/common/tinygpu_hw.h"

#define TYPE_TINYGPU "tinygpu"
OBJECT_DECLARE_SIMPLE_TYPE(TinyGpuState, TINYGPU)

#define TINYGPU_REVISION          0x01
#define TINYGPU_CLASS_CODE        PCI_CLASS_OTHERS
#define TINYGPU_MSIX_VECTORS      1
#define TINYGPU_MSIX_VEC_COMPLETE 0
#define TINYGPU_MSIX_VEC_ERROR    0
#define TINYGPU_MSIX_TABLE_OFFSET 0x800
#define TINYGPU_MSIX_PBA_OFFSET   0x900
#define TINYGPU_IRQ_MASK          (TINYGPU_IRQ_COMPLETE | TINYGPU_IRQ_ERROR)

typedef struct TinyGpuDmaState {
    uint64_t program_addr;
    uint32_t program_length;

    uint64_t input_addr;
    uint32_t input_length;
    uint32_t input_offset;

    uint64_t output_addr;
    uint32_t output_length;
    uint32_t output_offset;
} TinyGpuDmaState;

typedef struct TinyGpuState {
    PCIDevice parent_obj;

    MemoryRegion ctrl_mmio;

    uint32_t control;
    uint32_t status;
    uint32_t thread_count;
    uint32_t irq_enable;
    uint32_t irq_status;
    uint32_t exec_delay_ms;

    TinyGpuDmaState dma;
    QEMUTimer *exec_timer;
} TinyGpuState;

static void tinygpu_complete(void *opaque);

static void tinygpu_raise_irq(TinyGpuState *s, uint32_t irq_bits)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t enabled_bits;

    s->irq_status |= irq_bits & TINYGPU_IRQ_MASK;
    enabled_bits = s->irq_enable & irq_bits;

    if (!enabled_bits) {
        return;
    }

    if (msix_enabled(pdev)) {
        if (enabled_bits & TINYGPU_IRQ_COMPLETE) {
            msix_notify(pdev, TINYGPU_MSIX_VEC_COMPLETE);
        }
        if (enabled_bits & TINYGPU_IRQ_ERROR) {
            msix_notify(pdev, TINYGPU_MSIX_VEC_ERROR);
        }
    } else if (msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

static void tinygpu_update_intx(TinyGpuState *s)
{
    if (!msi_enabled(&s->parent_obj) && !msix_enabled(&s->parent_obj)) {
        pci_set_irq(&s->parent_obj, !!(s->irq_enable & s->irq_status));
    }
}

static void tinygpu_set_error(TinyGpuState *s, const char *message)
{
    qemu_log_mask(LOG_GUEST_ERROR, "tinygpu: %s\n", message);
    s->control &= ~TINYGPU_CONTROL_START;
    s->status &= ~TINYGPU_STATUS_BUSY;
    s->status |= TINYGPU_STATUS_ERROR;
    tinygpu_raise_irq(s, TINYGPU_IRQ_ERROR);
    tinygpu_update_intx(s);
}

static bool tinygpu_check_length(uint32_t length, uint32_t limit,
                                 const char *name)
{
    if (length > limit) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "tinygpu: %s length %u exceeds limit %u\n",
                      name, length, limit);
        return false;
    }
    return true;
}

static bool tinygpu_check_range(uint32_t offset, uint32_t length,
                                uint32_t limit, const char *name)
{
    if (offset > limit || length > limit - offset) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "tinygpu: %s range offset=%u length=%u exceeds limit %u\n",
                      name, offset, length, limit);
        return false;
    }
    return true;
}

static void tinygpu_soft_reset(TinyGpuState *s)
{
    timer_del(s->exec_timer);
    s->control = 0;
    s->status = TINYGPU_STATUS_DONE;
    s->thread_count = 0;
    s->irq_enable = 0;
    s->irq_status = 0;
    memset(&s->dma, 0, sizeof(s->dma));
    pci_set_irq(&s->parent_obj, 0);
}

static uint64_t tinygpu_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    TinyGpuState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "tinygpu: invalid read size %u at 0x%"HWADDR_PRIx"\n",
                      size, addr);
        return UINT64_MAX;
    }

    switch (addr) {
    case TINYGPU_REG_ID:
        return TINYGPU_DEVICE_ID_VALUE;
    case TINYGPU_REG_CONTROL:
        return s->control;
    case TINYGPU_REG_STATUS:
        return s->status;
    case TINYGPU_REG_THREAD_COUNT:
        return s->thread_count;
    case TINYGPU_REG_PROGRAM_ADDR_LO:
        return s->dma.program_addr & 0xffffffffu;
    case TINYGPU_REG_PROGRAM_ADDR_HI:
        return s->dma.program_addr >> 32;
    case TINYGPU_REG_PROGRAM_LENGTH:
        return s->dma.program_length;
    case TINYGPU_REG_INPUT_ADDR_LO:
        return s->dma.input_addr & 0xffffffffu;
    case TINYGPU_REG_INPUT_ADDR_HI:
        return s->dma.input_addr >> 32;
    case TINYGPU_REG_INPUT_LENGTH:
        return s->dma.input_length;
    case TINYGPU_REG_INPUT_OFFSET:
        return s->dma.input_offset;
    case TINYGPU_REG_OUTPUT_ADDR_LO:
        return s->dma.output_addr & 0xffffffffu;
    case TINYGPU_REG_OUTPUT_ADDR_HI:
        return s->dma.output_addr >> 32;
    case TINYGPU_REG_OUTPUT_LENGTH:
        return s->dma.output_length;
    case TINYGPU_REG_OUTPUT_OFFSET:
        return s->dma.output_offset;
    case TINYGPU_REG_IRQ_ENABLE:
        return s->irq_enable;
    case TINYGPU_REG_IRQ_STATUS:
        return s->irq_status;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "tinygpu: invalid read at 0x%"HWADDR_PRIx"\n", addr);
        return 0;
    }
}

static void tinygpu_start(TinyGpuState *s)
{
    if (s->status & TINYGPU_STATUS_BUSY) {
        tinygpu_set_error(s, "start requested while busy");
        return;
    }

    if (!tinygpu_check_length(s->dma.program_length,
                              TINYGPU_PROGRAM_MEMORY_SIZE, "program")) {
        tinygpu_set_error(s, "invalid program DMA length");
        return;
    }
    if (!tinygpu_check_range(s->dma.input_offset, s->dma.input_length,
                             TINYGPU_DATA_MEMORY_SIZE, "input")) {
        tinygpu_set_error(s, "invalid input memory range");
        return;
    }
    if (!tinygpu_check_range(s->dma.output_offset, s->dma.output_length,
                             TINYGPU_DATA_MEMORY_SIZE, "output")) {
        tinygpu_set_error(s, "invalid output memory range");
        return;
    }

    s->status &= ~(TINYGPU_STATUS_DONE | TINYGPU_STATUS_ERROR);
    s->status |= TINYGPU_STATUS_BUSY;
    s->irq_status = 0;
    tinygpu_update_intx(s);
    timer_mod(s->exec_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + s->exec_delay_ms);
}

static void tinygpu_ctrl_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    TinyGpuState *s = opaque;
    uint32_t value = val;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "tinygpu: invalid write size %u at 0x%"HWADDR_PRIx"\n",
                      size, addr);
        return;
    }

    switch (addr) {
    case TINYGPU_REG_CONTROL:
        if (value & TINYGPU_CONTROL_SOFT_RESET) {
            tinygpu_soft_reset(s);
            return;
        }
        s->control = value & TINYGPU_CONTROL_START;
        if (value & TINYGPU_CONTROL_START) {
            tinygpu_start(s);
        }
        break;
    case TINYGPU_REG_STATUS:
        s->status &= ~(value & (TINYGPU_STATUS_DONE | TINYGPU_STATUS_ERROR));
        break;
    case TINYGPU_REG_THREAD_COUNT:
        s->thread_count = value & 0xff;
        break;
    case TINYGPU_REG_PROGRAM_ADDR_LO:
        s->dma.program_addr = (s->dma.program_addr & 0xffffffff00000000ULL) |
                              value;
        break;
    case TINYGPU_REG_PROGRAM_ADDR_HI:
        s->dma.program_addr = (s->dma.program_addr & 0xffffffffULL) |
                              ((uint64_t)value << 32);
        break;
    case TINYGPU_REG_PROGRAM_LENGTH:
        s->dma.program_length = value;
        break;
    case TINYGPU_REG_INPUT_ADDR_LO:
        s->dma.input_addr = (s->dma.input_addr & 0xffffffff00000000ULL) |
                            value;
        break;
    case TINYGPU_REG_INPUT_ADDR_HI:
        s->dma.input_addr = (s->dma.input_addr & 0xffffffffULL) |
                            ((uint64_t)value << 32);
        break;
    case TINYGPU_REG_INPUT_LENGTH:
        s->dma.input_length = value;
        break;
    case TINYGPU_REG_INPUT_OFFSET:
        s->dma.input_offset = value;
        break;
    case TINYGPU_REG_OUTPUT_ADDR_LO:
        s->dma.output_addr = (s->dma.output_addr & 0xffffffff00000000ULL) |
                             value;
        break;
    case TINYGPU_REG_OUTPUT_ADDR_HI:
        s->dma.output_addr = (s->dma.output_addr & 0xffffffffULL) |
                             ((uint64_t)value << 32);
        break;
    case TINYGPU_REG_OUTPUT_LENGTH:
        s->dma.output_length = value;
        break;
    case TINYGPU_REG_OUTPUT_OFFSET:
        s->dma.output_offset = value;
        break;
    case TINYGPU_REG_IRQ_ENABLE:
        s->irq_enable = value & TINYGPU_IRQ_MASK;
        tinygpu_update_intx(s);
        break;
    case TINYGPU_REG_IRQ_STATUS:
        s->irq_status &= ~(value & TINYGPU_IRQ_MASK);
        tinygpu_update_intx(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "tinygpu: invalid write at 0x%"HWADDR_PRIx"\n", addr);
        break;
    }
}

static const MemoryRegionOps tinygpu_ctrl_ops = {
    .read = tinygpu_ctrl_read,
    .write = tinygpu_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void tinygpu_complete(void *opaque)
{
    TinyGpuState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    g_autofree uint8_t *program = NULL;
    g_autofree uint8_t *input = NULL;
    g_autofree uint8_t *output = NULL;
    char error[TINYGPU_VERILATOR_ERROR_SIZE];
    int result;

    program = g_malloc0(MAX(1u, s->dma.program_length));
    input = g_malloc0(MAX(1u, s->dma.input_length));
    output = g_malloc0(MAX(1u, s->dma.output_length));

    if (s->dma.program_length &&
        pci_dma_read(pdev, s->dma.program_addr, program,
                     s->dma.program_length) != MEMTX_OK) {
        tinygpu_set_error(s, "program DMA read failed");
        return;
    }
    if (s->dma.input_length &&
        pci_dma_read(pdev, s->dma.input_addr, input,
                     s->dma.input_length) != MEMTX_OK) {
        tinygpu_set_error(s, "input DMA read failed");
        return;
    }

    result = tinygpu_verilator_run(program, s->dma.program_length,
                                   input, s->dma.input_length,
                                   s->dma.input_offset,
                                   output, s->dma.output_length,
                                   s->dma.output_offset,
                                   s->thread_count, error, sizeof(error));
    if (result != 0) {
        tinygpu_set_error(s, error);
        return;
    }

    if (s->dma.output_length &&
        pci_dma_write(pdev, s->dma.output_addr, output,
                      s->dma.output_length) != MEMTX_OK) {
        tinygpu_set_error(s, "output DMA write failed");
        return;
    }

    s->control &= ~TINYGPU_CONTROL_START;
    s->status = (s->status & ~TINYGPU_STATUS_BUSY) | TINYGPU_STATUS_DONE;
    tinygpu_raise_irq(s, TINYGPU_IRQ_COMPLETE);
    tinygpu_update_intx(s);
}

static void tinygpu_realize(PCIDevice *pdev, Error **errp)
{
    TinyGpuState *s = TINYGPU(pdev);

    pci_config_set_interrupt_pin(pdev->config, 1);
    pcie_endpoint_cap_init(pdev, 0x80);

    memory_region_init_io(&s->ctrl_mmio, OBJECT(s), &tinygpu_ctrl_ops, s,
                          "tinygpu-ctrl", TINYGPU_BAR_SIZE);
    pci_register_bar(pdev, TINYGPU_BAR_INDEX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->ctrl_mmio);

    if (msix_init(pdev, TINYGPU_MSIX_VECTORS, &s->ctrl_mmio, 0,
                  TINYGPU_MSIX_TABLE_OFFSET, &s->ctrl_mmio, 0,
                  TINYGPU_MSIX_PBA_OFFSET, 0, errp)) {
        return;
    }
    /* This device has one permanently assigned completion/error vector. */
    msix_vector_use(pdev, TINYGPU_MSIX_VEC_COMPLETE);
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        msix_vector_unuse(pdev, TINYGPU_MSIX_VEC_COMPLETE);
        msix_uninit(pdev, &s->ctrl_mmio, &s->ctrl_mmio);
        return;
    }

    s->exec_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, tinygpu_complete, s);
    tinygpu_soft_reset(s);
}

static void tinygpu_exit(PCIDevice *pdev)
{
    TinyGpuState *s = TINYGPU(pdev);

    timer_free(s->exec_timer);
    msix_vector_unuse(pdev, TINYGPU_MSIX_VEC_COMPLETE);
    msix_uninit(pdev, &s->ctrl_mmio, &s->ctrl_mmio);
    msi_uninit(pdev);
}

static void tinygpu_reset(DeviceState *dev)
{
    tinygpu_soft_reset(TINYGPU(dev));
}

static const Property tinygpu_properties[] = {
    DEFINE_PROP_UINT32("exec-delay-ms", TinyGpuState, exec_delay_ms, 1),
};

static const VMStateDescription vmstate_tinygpu = {
    .name = "tinygpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, TinyGpuState),
        VMSTATE_UINT32(control, TinyGpuState),
        VMSTATE_UINT32(status, TinyGpuState),
        VMSTATE_UINT32(thread_count, TinyGpuState),
        VMSTATE_UINT32(irq_enable, TinyGpuState),
        VMSTATE_UINT32(irq_status, TinyGpuState),
        VMSTATE_UINT64(dma.program_addr, TinyGpuState),
        VMSTATE_UINT32(dma.program_length, TinyGpuState),
        VMSTATE_UINT64(dma.input_addr, TinyGpuState),
        VMSTATE_UINT32(dma.input_length, TinyGpuState),
        VMSTATE_UINT32(dma.input_offset, TinyGpuState),
        VMSTATE_UINT64(dma.output_addr, TinyGpuState),
        VMSTATE_UINT32(dma.output_length, TinyGpuState),
        VMSTATE_UINT32(dma.output_offset, TinyGpuState),
        VMSTATE_END_OF_LIST()
    },
};

static void tinygpu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    pc->realize = tinygpu_realize;
    pc->exit = tinygpu_exit;
    pc->vendor_id = TINYGPU_PCI_VENDOR_ID;
    pc->device_id = TINYGPU_PCI_DEVICE_ID;
    pc->revision = TINYGPU_REVISION;
    pc->class_id = TINYGPU_CLASS_CODE;

    device_class_set_legacy_reset(dc, tinygpu_reset);
    dc->desc = "tiny-gpu PCIe accelerator";
    dc->vmsd = &vmstate_tinygpu;
    device_class_set_props(dc, tinygpu_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo tinygpu_type_info = {
    .name = TYPE_TINYGPU,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(TinyGpuState),
    .class_init = tinygpu_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void tinygpu_register_types(void)
{
    type_register_static(&tinygpu_type_info);
}

type_init(tinygpu_register_types);
