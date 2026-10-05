// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/uaccess.h>

#define DEV_NAME "mydma"

/* ---- Hardware register offsets (FROM DATASHEET) ---- */
#define REG_DMA_ADDR   0x00
#define REG_DMA_LEN    0x04
#define REG_DMA_CTRL   0x08
#define REG_DMA_STAT   0x0C

#define DMA_CTRL_START 0x1
#define DMA_STAT_DONE  0x1

#define BUF_SIZE   (PAGE_SIZE * 4)

/* ---- IOCTL ---- */
#define IOCTL_START_STREAM _IO('q', 1)

/* ---- Driver private data ---- */
struct mydma_dev {
    void __iomem *regs;
    int irq;

    void *dma_virt;
    dma_addr_t dma_phys;

    dev_t devno;
    struct cdev cdev;
    struct class *class;

    wait_queue_head_t wq;
    int frame_ready;
    int streaming;
};

static struct mydma_dev *gdev;

/* -------------------------------------------------- */
/* DMA programming (software → hardware)              */
/* -------------------------------------------------- */
static void mydma_program_dma(struct mydma_dev *dev)
{
    writel(dev->dma_phys, dev->regs + REG_DMA_ADDR);
    writel(BUF_SIZE,       dev->regs + REG_DMA_LEN);
    writel(DMA_CTRL_START, dev->regs + REG_DMA_CTRL);
}

/* -------------------------------------------------- */
/* IRQ handler (hardware → software)                  */
/* -------------------------------------------------- */
static irqreturn_t mydma_irq(int irq, void *data)
{
    struct mydma_dev *dev = data;

    /* Clear interrupt */
    writel(DMA_STAT_DONE, dev->regs + REG_DMA_STAT);

    dev->frame_ready = 1;
    wake_up_interruptible(&dev->wq);

    /* Continuous streaming */
    if (dev->streaming)
        mydma_program_dma(dev);

    return IRQ_HANDLED;
}

/* -------------------------------------------------- */
/* Character device operations                        */
/* -------------------------------------------------- */
static int mydma_open(struct inode *inode, struct file *file)
{
    file->private_data = gdev;
    return 0;
}

static long mydma_ioctl(struct file *file,
                        unsigned int cmd,
                        unsigned long arg)
{
    struct mydma_dev *dev = file->private_data;

    switch (cmd) {
    case IOCTL_START_STREAM:
        dev->frame_ready = 0;
        dev->streaming = 1;
        mydma_program_dma(dev);
        return 0;
    default:
        return -EINVAL;
    }
}

static ssize_t mydma_read(struct file *file,
                          char __user *buf,
                          size_t len,
                          loff_t *off)
{
    struct mydma_dev *dev = file->private_data;

    /* Wait for DMA frame completion */
    wait_event_interruptible(dev->wq, dev->frame_ready);

    dev->frame_ready = 0;
    return 0; /* NO DATA COPY */
}

static int mydma_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct mydma_dev *dev = file->private_data;
    unsigned long size = vma->vm_end - vma->vm_start;

    if (size > BUF_SIZE)
        return -EINVAL;

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    return remap_pfn_range(vma,
                           vma->vm_start,
                           dev->dma_phys >> PAGE_SHIFT,
                           size,
                           vma->vm_page_prot);
}

static const struct file_operations mydma_fops = {
    .owner          = THIS_MODULE,
    .open           = mydma_open,
    .unlocked_ioctl = mydma_ioctl,
    .read           = mydma_read,
    .mmap           = mydma_mmap,
};

/* -------------------------------------------------- */
/* Platform driver                                   */
/* -------------------------------------------------- */
static int mydma_probe(struct platform_device *pdev)
{
    struct resource *res;
    int ret;

    gdev = devm_kzalloc(&pdev->dev, sizeof(*gdev), GFP_KERNEL);
    if (!gdev)
        return -ENOMEM;

    /* MMIO from DTS */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    gdev->regs = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(gdev->regs))
        return PTR_ERR(gdev->regs);

    /* IRQ from DTS */
    gdev->irq = platform_get_irq(pdev, 0);
    ret = devm_request_irq(&pdev->dev, gdev->irq,
                           mydma_irq, 0,
                           DEV_NAME, gdev);
    if (ret)
        return ret;

    /* DMA buffer */
    gdev->dma_virt = dma_alloc_coherent(&pdev->dev,
                                        BUF_SIZE,
                                        &gdev->dma_phys,
                                        GFP_KERNEL);
    if (!gdev->dma_virt)
        return -ENOMEM;

    init_waitqueue_head(&gdev->wq);

    /* Character device */
    alloc_chrdev_region(&gdev->devno, 0, 1, DEV_NAME);
    cdev_init(&gdev->cdev, &mydma_fops);
    cdev_add(&gdev->cdev, gdev->devno, 1);

    gdev->class = class_create(THIS_MODULE, DEV_NAME);
    device_create(gdev->class, NULL,
                  gdev->devno, NULL, DEV_NAME);

    dev_info(&pdev->dev,
             "DMA virt=%p phys=%pad irq=%d\n",
             gdev->dma_virt, &gdev->dma_phys, gdev->irq);

    return 0;
}

static int mydma_remove(struct platform_device *pdev)
{
    device_destroy(gdev->class, gdev->devno);
    class_destroy(gdev->class);
    cdev_del(&gdev->cdev);
    unregister_chrdev_region(gdev->devno, 1);

    dma_free_coherent(&pdev->dev, BUF_SIZE,
                      gdev->dma_virt, gdev->dma_phys);
    return 0;
}

static const struct of_device_id mydma_of_match[] = {
    { .compatible = "vendor,mydma" },
    {}
};
MODULE_DEVICE_TABLE(of, mydma_of_match);

static struct platform_driver mydma_driver = {
    .probe  = mydma_probe,
    .remove = mydma_remove,
    .driver = {
        .name           = DEV_NAME,
        .of_match_table = mydma_of_match,
    },
};

module_platform_driver(mydma_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Example");
MODULE_DESCRIPTION("Zero-copy DMA mmap driver");
