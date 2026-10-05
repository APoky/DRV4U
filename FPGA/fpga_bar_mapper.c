#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <linux/mm.h>
#include <linux/io.h>

// --- Driver Constants ---
#define DRV_NAME "fpga_bar_mapper"
#define MAX_BAR_SIZE (1 * 1024 * 1024) // Limit mapping to 1MB
#define BAR_NUM 0                      // Assuming configuration registers are in BAR 0

// --- PCIe Device ID (PLACEHOLDERS) ---
// You must replace these with your FPGA's Vendor and Device IDs
#define VENDOR_ID_FPGA  0x10EE // Example: Xilinx Vendor ID
#define DEVICE_ID_FPGA  0x7024 // Example: Custom Device ID

// --- Private Data Structure ---
struct fpga_dev {
    struct pci_dev *pdev;   // Pointer to the PCI device structure
    void __iomem *bar_vaddr; // Mapped virtual address (kernel space)
    resource_size_t bar_phys_addr; // Physical address (BAR start)
    resource_size_t bar_len;       // Length of the BAR
    
    dev_t dev_num;
    struct cdev cdev;
    struct class *dev_class;
};

// --- Character Device File Operations ---

static int fpga_open(struct inode *inode, struct file *file)
{
    struct fpga_dev *fpga_data;
    fpga_data = container_of(inode->i_cdev, struct fpga_dev, cdev);
    file->private_data = fpga_data;
    return 0;
}

static int fpga_release(struct inode *inode, struct file *file)
{
    return 0;
}

// THE CRITICAL MAPPING FUNCTION: Exposes Physical BAR memory to userspace
static int fpga_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct fpga_dev *fpga_data = file->private_data;
    resource_size_t phys_addr = fpga_data->bar_phys_addr;
    resource_size_t size = vma->vm_end - vma->vm_start;
    
    // 1. Basic Boundary Checks
    if (size > fpga_data->bar_len || vma->vm_pgoff + (size >> PAGE_SHIFT) > (fpga_data->bar_len >> PAGE_SHIFT)) {
        pr_err("%s: VMA size or offset out of BAR limits.\n", DRV_NAME);
        return -EINVAL;
    }
    
    // 2. Set Memory Flags
    // MAP_SHARED: Ensures changes are seen by all processes and the hardware
    // VM_IO: Marks the VMA as memory mapped to I/O space
    // VM_DONTEXPAND/VM_DONTDUMP: Prevents accidental swapping or dumping
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;

    // 3. Perform the Mapping
    // io_remap_pfn_range maps the physical address to the VMA
    // Note: The physical address must be shifted to get the Page Frame Number (PFN)
    if (io_remap_pfn_range(vma, vma->vm_start, 
                           (phys_addr + vma->vm_pgoff) >> PAGE_SHIFT,
                           size, vma->vm_page_prot)) {
        pr_err("%s: Failed to map BAR region.\n", DRV_NAME);
        return -EAGAIN;
    }

    return 0;
}

static const struct file_operations fpga_fops = {
    .owner = THIS_MODULE,
    .open = fpga_open,
    .release = fpga_release,
    .mmap = fpga_mmap, // The bridge function
};

// --- PCI Driver Functions ---

static int fpga_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    int ret;
    struct fpga_dev *fpga_data;
    resource_size_t bar_start;

    pr_info("%s: Probing device V:0x%04x D:0x%04x\n", DRV_NAME, pdev->vendor, pdev->device);

    // 1. Allocate and Check Private Data
    fpga_data = devm_kzalloc(&pdev->dev, sizeof(*fpga_data), GFP_KERNEL);
    if (!fpga_data) return -ENOMEM;
    fpga_data->pdev = pdev;
    pci_set_drvdata(pdev, fpga_data);

    // 2. Enable the PCI Device
    ret = pci_enable_device(pdev);
    if (ret) {
        pr_err("%s: Failed to enable PCI device.\n", DRV_NAME);
        return ret;
    }

    // 3. Request and Map BAR Memory
    ret = pci_request_regions(pdev, DRV_NAME);
    if (ret) {
        pr_err("%s: Failed to request PCI regions.\n", DRV_NAME);
        goto err_disable;
    }
    
    bar_start = pci_resource_start(pdev, BAR_NUM);
    fpga_data->bar_len = pci_resource_len(pdev, BAR_NUM);
    fpga_data->bar_phys_addr = bar_start;
    
    pr_info("%s: BAR %d: physical address 0x%llx, length 0x%llx\n", 
            DRV_NAME, BAR_NUM, (unsigned long long)fpga_data->bar_phys_addr, 
            (unsigned long long)fpga_data->bar_len);

    // ioremap the BAR for kernel-space access (optional, but good practice)
    fpga_data->bar_vaddr = pci_iomap(pdev, BAR_NUM, 0); 
    if (!fpga_data->bar_vaddr) {
        pr_err("%s: Failed to iomap BAR %d.\n", DRV_NAME, BAR_NUM);
        goto err_release_regions;
    }
    
    // 4. Character Device Registration
    ret = alloc_chrdev_region(&fpga_data->dev_num, 0, 1, DRV_NAME);
    if (ret < 0) {
        pr_err("%s: Failed to alloc chrdev region.\n", DRV_NAME);
        goto err_iounmap;
    }

    fpga_data->dev_class = class_create(THIS_MODULE, DRV_NAME);
    if (IS_ERR(fpga_data->dev_class)) {
        ret = PTR_ERR(fpga_data->dev_class);
        goto err_unregister_chrdev;
    }
    
    cdev_init(&fpga_data->cdev, &fpga_fops);
    fpga_data->cdev.owner = THIS_MODULE;
    ret = cdev_add(&fpga_data->cdev, fpga_data->dev_num, 1);
    if (ret < 0) {
        pr_err("%s: Failed to add cdev.\n", DRV_NAME);
        goto err_destroy_class;
    }

    // Create the device node /dev/fpga_bar_mapper0
    device_create(fpga_data->dev_class, &pdev->dev, fpga_data->dev_num, NULL, "%s%d", DRV_NAME, 0);

    return 0;

err_destroy_class:
    class_destroy(fpga_data->dev_class);
err_unregister_chrdev:
    unregister_chrdev_region(fpga_data->dev_num, 1);
err_iounmap:
    pci_iounmap(pdev, fpga_data->bar_vaddr);
err_release_regions:
    pci_release_regions(pdev);
err_disable:
    pci_disable_device(pdev);
    return ret;
}

static void fpga_remove(struct pci_dev *pdev)
{
    struct fpga_dev *fpga_data = pci_get_drvdata(pdev);

    device_destroy(fpga_data->dev_class, fpga_data->dev_num);
    class_destroy(fpga_data->dev_class);
    unregister_chrdev_region(fpga_data->dev_num, 1);
    cdev_del(&fpga_data->cdev);
    
    pci_iounmap(pdev, fpga_data->bar_vaddr);
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    
    pr_info("%s: Device removed.\n", DRV_NAME);
}

static const struct pci_device_id fpga_pci_ids[] = {
    { PCI_DEVICE(VENDOR_ID_FPGA, DEVICE_ID_FPGA) },
    { 0, } // End of list
};
MODULE_DEVICE_TABLE(pci, fpga_pci_ids);

static struct pci_driver fpga_driver = {
    .name = DRV_NAME,
    .id_table = fpga_pci_ids,
    .probe = fpga_probe,
    .remove = fpga_remove,
};

module_pci_driver(fpga_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("FPGA PCIe BAR Mapper");