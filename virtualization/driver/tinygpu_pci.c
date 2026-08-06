#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "tinygpu_hw.h"
#include "tinygpu_ioctl.h"

#define TINYGPU_MAX_MINORS 256
#define TINYGPU_DEFAULT_TIMEOUT_MS 1000

struct tinygpu_device {
	struct pci_dev *pdev;
	void __iomem *bar0;
	int irq;
	int minor;

	struct cdev cdev;
	struct device *chardev;
	struct mutex lock;
	wait_queue_head_t waitq;
	bool done;
	u32 irq_status;
};

struct tinygpu_dma_buffer {
	void *cpu_addr;
	dma_addr_t dma_addr;
	size_t size;
};

static dev_t tinygpu_devt;
static struct class *tinygpu_class;
static DEFINE_IDA(tinygpu_ida);

static void tinygpu_write_dma_addr(struct tinygpu_device *gpu, u32 lo_reg,
				   u32 hi_reg, dma_addr_t addr)
{
	writel(lower_32_bits(addr), gpu->bar0 + lo_reg);
	writel(upper_32_bits(addr), gpu->bar0 + hi_reg);
}

static int tinygpu_alloc_dma(struct tinygpu_device *gpu,
			     struct tinygpu_dma_buffer *buf, size_t size)
{
	if (!size)
		return 0;

	buf->cpu_addr = dma_alloc_coherent(&gpu->pdev->dev, size,
					   &buf->dma_addr, GFP_KERNEL);
	if (!buf->cpu_addr)
		return -ENOMEM;

	buf->size = size;
	return 0;
}

static void tinygpu_free_dma(struct tinygpu_device *gpu,
			     struct tinygpu_dma_buffer *buf)
{
	if (!buf->cpu_addr)
		return;

	dma_free_coherent(&gpu->pdev->dev, buf->size, buf->cpu_addr,
			  buf->dma_addr);
	memset(buf, 0, sizeof(*buf));
}

static int tinygpu_copy_from_user_dma(struct tinygpu_dma_buffer *buf,
				      u64 user_ptr, size_t size)
{
	if (!size)
		return 0;
	if (!user_ptr)
		return -EINVAL;
	if (copy_from_user(buf->cpu_addr, (void __user *)(unsigned long)user_ptr,
			   size))
		return -EFAULT;

	return 0;
}

static int tinygpu_copy_to_user_dma(struct tinygpu_dma_buffer *buf,
				    u64 user_ptr, size_t size)
{
	if (!size)
		return 0;
	if (!user_ptr)
		return -EINVAL;
	if (copy_to_user((void __user *)(unsigned long)user_ptr, buf->cpu_addr,
			 size))
		return -EFAULT;

	return 0;
}

static bool tinygpu_range_valid(u32 offset, u32 size, u32 limit)
{
	return offset <= limit && size <= limit - offset;
}

static int tinygpu_validate_submit(const struct tinygpu_submit *submit)
{
	if (submit->reserved)
		return -EINVAL;

	if (submit->thread_count > 0xff)
		return -EINVAL;

	if (submit->program_size > TINYGPU_PROGRAM_MEMORY_SIZE)
		return -EINVAL;

	if (!tinygpu_range_valid(submit->input_offset, submit->input_size,
				 TINYGPU_DATA_MEMORY_SIZE))
		return -EINVAL;

	if (!tinygpu_range_valid(submit->output_offset, submit->output_size,
				 TINYGPU_DATA_MEMORY_SIZE))
		return -EINVAL;

	if (submit->program_size && !submit->program_ptr)
		return -EINVAL;
	if (submit->input_size && !submit->input_ptr)
		return -EINVAL;
	if (submit->output_size && !submit->output_ptr)
		return -EINVAL;

	return 0;
}

static irqreturn_t tinygpu_irq(int irq, void *data)
{
	struct tinygpu_device *gpu = data;
	u32 irq_status;

	irq_status = readl(gpu->bar0 + TINYGPU_REG_IRQ_STATUS);
	if (!(irq_status & (TINYGPU_IRQ_COMPLETE | TINYGPU_IRQ_ERROR)))
		return IRQ_NONE;

	writel(irq_status, gpu->bar0 + TINYGPU_REG_IRQ_STATUS);
	gpu->irq_status = irq_status;
	WRITE_ONCE(gpu->done, true);
	wake_up(&gpu->waitq);

	return IRQ_HANDLED;
}

static int tinygpu_submit_job(struct tinygpu_device *gpu,
			      struct tinygpu_submit *submit)
{
	struct tinygpu_dma_buffer program = {};
	struct tinygpu_dma_buffer input = {};
	struct tinygpu_dma_buffer output = {};
	unsigned long timeout;
	long wait_ret;
	u32 status;
	int ret;

	ret = tinygpu_validate_submit(submit);
	if (ret)
		return ret;

	ret = tinygpu_alloc_dma(gpu, &program, submit->program_size);
	if (ret)
		goto out;
	ret = tinygpu_alloc_dma(gpu, &input, submit->input_size);
	if (ret)
		goto out;
	ret = tinygpu_alloc_dma(gpu, &output, submit->output_size);
	if (ret)
		goto out;

	ret = tinygpu_copy_from_user_dma(&program, submit->program_ptr,
					 submit->program_size);
	if (ret)
		goto out;
	ret = tinygpu_copy_from_user_dma(&input, submit->input_ptr,
					 submit->input_size);
	if (ret)
		goto out;

	timeout = msecs_to_jiffies(submit->timeout_ms ?
				  submit->timeout_ms :
				  TINYGPU_DEFAULT_TIMEOUT_MS);

	mutex_lock(&gpu->lock);
	WRITE_ONCE(gpu->done, false);
	gpu->irq_status = 0;

	writel(TINYGPU_CONTROL_SOFT_RESET, gpu->bar0 + TINYGPU_REG_CONTROL);
	writel(TINYGPU_IRQ_COMPLETE | TINYGPU_IRQ_ERROR,
	       gpu->bar0 + TINYGPU_REG_IRQ_STATUS);
	writel(TINYGPU_STATUS_DONE | TINYGPU_STATUS_ERROR,
	       gpu->bar0 + TINYGPU_REG_STATUS);

	tinygpu_write_dma_addr(gpu, TINYGPU_REG_PROGRAM_ADDR_LO,
			       TINYGPU_REG_PROGRAM_ADDR_HI, program.dma_addr);
	writel(submit->program_size, gpu->bar0 + TINYGPU_REG_PROGRAM_LENGTH);

	tinygpu_write_dma_addr(gpu, TINYGPU_REG_INPUT_ADDR_LO,
			       TINYGPU_REG_INPUT_ADDR_HI, input.dma_addr);
	writel(submit->input_size, gpu->bar0 + TINYGPU_REG_INPUT_LENGTH);
	writel(submit->input_offset, gpu->bar0 + TINYGPU_REG_INPUT_OFFSET);

	tinygpu_write_dma_addr(gpu, TINYGPU_REG_OUTPUT_ADDR_LO,
			       TINYGPU_REG_OUTPUT_ADDR_HI, output.dma_addr);
	writel(submit->output_size, gpu->bar0 + TINYGPU_REG_OUTPUT_LENGTH);
	writel(submit->output_offset, gpu->bar0 + TINYGPU_REG_OUTPUT_OFFSET);

	writel(submit->thread_count, gpu->bar0 + TINYGPU_REG_THREAD_COUNT);
	writel(TINYGPU_IRQ_COMPLETE | TINYGPU_IRQ_ERROR,
	       gpu->bar0 + TINYGPU_REG_IRQ_ENABLE);
	writel(TINYGPU_CONTROL_START, gpu->bar0 + TINYGPU_REG_CONTROL);

	wait_ret = wait_event_interruptible_timeout(gpu->waitq,
						    READ_ONCE(gpu->done),
						    timeout);
	status = readl(gpu->bar0 + TINYGPU_REG_STATUS);

	if (wait_ret == 0) {
		writel(TINYGPU_CONTROL_SOFT_RESET, gpu->bar0 + TINYGPU_REG_CONTROL);
		ret = -ETIMEDOUT;
	} else if (wait_ret < 0) {
		writel(TINYGPU_CONTROL_SOFT_RESET, gpu->bar0 + TINYGPU_REG_CONTROL);
		ret = wait_ret;
	} else if ((gpu->irq_status & TINYGPU_IRQ_ERROR) ||
		   (status & TINYGPU_STATUS_ERROR)) {
		ret = -EIO;
	} else if (!(status & TINYGPU_STATUS_DONE)) {
		ret = -EIO;
	} else {
		ret = 0;
	}

	writel(0, gpu->bar0 + TINYGPU_REG_IRQ_ENABLE);
	mutex_unlock(&gpu->lock);

	if (!ret)
		ret = tinygpu_copy_to_user_dma(&output, submit->output_ptr,
					       submit->output_size);

out:
	tinygpu_free_dma(gpu, &output);
	tinygpu_free_dma(gpu, &input);
	tinygpu_free_dma(gpu, &program);
	return ret;
}

static int tinygpu_open(struct inode *inode, struct file *file)
{
	struct tinygpu_device *gpu = container_of(inode->i_cdev,
						 struct tinygpu_device, cdev);

	file->private_data = gpu;
	return 0;
}

static long tinygpu_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct tinygpu_device *gpu = file->private_data;
	struct tinygpu_submit submit;
	int ret;

	if (cmd != TINYGPU_IOCTL_SUBMIT)
		return -ENOTTY;

	if (copy_from_user(&submit, (void __user *)arg, sizeof(submit)))
		return -EFAULT;

	ret = tinygpu_submit_job(gpu, &submit);
	if (ret)
		return ret;

	if (copy_to_user((void __user *)arg, &submit, sizeof(submit)))
		return -EFAULT;

	return 0;
}

static const struct file_operations tinygpu_fops = {
	.owner = THIS_MODULE,
	.open = tinygpu_open,
	.unlocked_ioctl = tinygpu_ioctl,
	.llseek = no_llseek,
};

static int tinygpu_probe(struct pci_dev *pdev,
			 const struct pci_device_id *id)
{
	struct tinygpu_device *gpu;
	u32 device_id;
	dev_t devt;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(TINYGPU_BAR_INDEX), "tinygpu");
	if (ret)
		return ret;

	gpu = devm_kzalloc(&pdev->dev, sizeof(*gpu), GFP_KERNEL);
	if (!gpu)
		return -ENOMEM;

	gpu->pdev = pdev;
	gpu->bar0 = pcim_iomap_table(pdev)[TINYGPU_BAR_INDEX];
	if (!gpu->bar0)
		return -ENOMEM;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	ret = pci_alloc_irq_vectors(pdev, 1, 1,
				    PCI_IRQ_MSIX | PCI_IRQ_MSI | PCI_IRQ_LEGACY);
	if (ret < 0)
		return ret;

	gpu->irq = pci_irq_vector(pdev, 0);
	ret = request_irq(gpu->irq, tinygpu_irq, 0, "tinygpu", gpu);
	if (ret)
		goto err_free_irq_vectors;

	ret = ida_alloc_max(&tinygpu_ida, TINYGPU_MAX_MINORS - 1, GFP_KERNEL);
	if (ret < 0)
		goto err_free_irq;
	gpu->minor = ret;

	mutex_init(&gpu->lock);
	init_waitqueue_head(&gpu->waitq);

	pci_set_master(pdev);
	pci_set_drvdata(pdev, gpu);

	device_id = readl(gpu->bar0 + TINYGPU_REG_ID);
	if (device_id != TINYGPU_DEVICE_ID_VALUE) {
		ret = dev_err_probe(&pdev->dev, -ENODEV,
			"unexpected device ID register %#x\n", device_id);
		goto err_clear_master;
	}

	devt = MKDEV(MAJOR(tinygpu_devt), gpu->minor);
	cdev_init(&gpu->cdev, &tinygpu_fops);
	gpu->cdev.owner = THIS_MODULE;

	ret = cdev_add(&gpu->cdev, devt, 1);
	if (ret)
		goto err_clear_master;

	gpu->chardev = device_create(tinygpu_class, &pdev->dev, devt, NULL,
				     "tinygpu%d", gpu->minor);
	if (IS_ERR(gpu->chardev)) {
		ret = PTR_ERR(gpu->chardev);
		goto err_del_cdev;
	}

	dev_info(&pdev->dev, "tiny-gpu virtual PCI device found at /dev/tinygpu%d\n",
		 gpu->minor);
	return 0;

err_del_cdev:
	cdev_del(&gpu->cdev);
err_clear_master:
	pci_clear_master(pdev);
	ida_free(&tinygpu_ida, gpu->minor);
err_free_irq:
	free_irq(gpu->irq, gpu);
err_free_irq_vectors:
	pci_free_irq_vectors(pdev);
	return ret;
}

static void tinygpu_remove(struct pci_dev *pdev)
{
	struct tinygpu_device *gpu = pci_get_drvdata(pdev);
	dev_t devt = MKDEV(MAJOR(tinygpu_devt), gpu->minor);

	device_destroy(tinygpu_class, devt);
	cdev_del(&gpu->cdev);
	pci_clear_master(pdev);
	free_irq(gpu->irq, gpu);
	pci_free_irq_vectors(pdev);
	ida_free(&tinygpu_ida, gpu->minor);
}

static const struct pci_device_id tinygpu_pci_ids[] = {
	{ PCI_DEVICE(TINYGPU_PCI_VENDOR_ID, TINYGPU_PCI_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, tinygpu_pci_ids);

static struct pci_driver tinygpu_pci_driver = {
	.name = "tinygpu",
	.id_table = tinygpu_pci_ids,
	.probe = tinygpu_probe,
	.remove = tinygpu_remove,
};

static int __init tinygpu_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&tinygpu_devt, 0, TINYGPU_MAX_MINORS,
				  "tinygpu");
	if (ret)
		return ret;

	tinygpu_class = class_create("tinygpu");
	if (IS_ERR(tinygpu_class)) {
		ret = PTR_ERR(tinygpu_class);
		goto err_unregister_chrdev;
	}

	ret = pci_register_driver(&tinygpu_pci_driver);
	if (ret)
		goto err_destroy_class;

	return 0;

err_destroy_class:
	class_destroy(tinygpu_class);
err_unregister_chrdev:
	unregister_chrdev_region(tinygpu_devt, TINYGPU_MAX_MINORS);
	return ret;
}

static void __exit tinygpu_exit(void)
{
	pci_unregister_driver(&tinygpu_pci_driver);
	class_destroy(tinygpu_class);
	unregister_chrdev_region(tinygpu_devt, TINYGPU_MAX_MINORS);
	ida_destroy(&tinygpu_ida);
}

module_init(tinygpu_init);
module_exit(tinygpu_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("tiny-gpu PCI driver with synchronous DMA submit ioctl");
MODULE_AUTHOR("tiny-gpu contributors");
