#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/mod_devicetable.h>
#include <linux/of_platform.h> 
#include <linux/fs.h>       // For file operations (struct file_operations)
#include <linux/cdev.h>     // For character device support
#include <linux/uaccess.h>  // For copy_to_user/copy_from_user
#include <linux/completion.h> // For synchronizing user access

// --- Definitions ---
#define DRIVER_NAME "dummy-dma-drv"
#define DEVICE_NAME "dummy_dma" // The name of the device file in /dev/
#define DMA_BUFFER_SIZE 32
#define SIMULATED_TRANSFER_TIME_MS 100 // Simulate 100ms transfer time

// --- MMIO/Register Definitions REMOVED ---
#define DUMMY_DMA_IRQ     32

// --- Data Structures ---
struct dummy_dma_dev {
    struct device *dev;
    int irq;

    // CDEV structures
    struct cdev cdev;
    dev_t dev_num;
    struct class *dev_class;
    
    // Simulated DMA Buffers (Kmalloc'd, but conceptually DMA-coherent)
    u8 *src_buf;
    u8 *dst_buf;
    dma_addr_t src_dma_handle;
    dma_addr_t dst_dma_handle;

    // Work structure for simulating asynchronous hardware transfer
    struct work_struct completion_work;
    
    // Synchronization for transfer completion (blocks user in dummy_write)
    struct completion transfer_complete;
};

// Global definition for the platform device we will register
static struct platform_device *dummy_pdev;

// --- Function Prototypes ---
static irqreturn_t dummy_dma_isr(int irq, void *dev_id);
static void dma_sim_worker(struct work_struct *work);

// --- Character Device File Operations Prototypes ---
static int dummy_open(struct inode *inode, struct file *file);
static int dummy_release(struct inode *inode, struct file *file);
static ssize_t dummy_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
static ssize_t dummy_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos);

// --- DMA Completion Simulation Logic (The "Hardware") ---

/**
* @brief This function simulates the physical hardware transfer.
* It runs in a separate kernel thread (from the workqueue).
*/
static void dma_sim_worker(struct work_struct *work)
{
    struct dummy_dma_dev *ddev = container_of(work, struct dummy_dma_dev, completion_work);
    
    dev_info(ddev->dev, "DMA Sim: Transfer started asynchronously (Simulating %dms delay)...\n", 
            SIMULATED_TRANSFER_TIME_MS);

    // 1. Simulate the data transfer in software
    msleep(SIMULATED_TRANSFER_TIME_MS);
    
    // Perform the actual data copy (simulated "hardware" action)
    memcpy(ddev->dst_buf, ddev->src_buf, DMA_BUFFER_SIZE);
    
    dev_info(ddev->dev, "DMA Sim: Data copy complete. Triggering interrupt handler logic.\n");
    
    // 2. Manually call the ISR logic to simulate an interrupt firing
    dummy_dma_isr(ddev->irq, ddev);

    // 3. Signal completion to unblock the waiting user-space process
    complete(&ddev->transfer_complete);
}

// --- Interrupt Handler (The "CPU" Side) ---

/**
* @brief The actual interrupt service routine.
*/
static irqreturn_t dummy_dma_isr(int irq, void *dev_id)
{
    struct dummy_dma_dev *ddev = (struct dummy_dma_dev *)dev_id;
    
    dev_info(ddev->dev, "Dummy DMA ISR: IRQ %d received!\n", irq);
    
    // Verify Data Integrity (Test the simulation)
    if (memcmp(ddev->src_buf, ddev->dst_buf, DMA_BUFFER_SIZE) == 0) {
        dev_info(ddev->dev, "Dummy DMA ISR: Transfer successful. Buffers match.\n");
    } else {
        dev_err(ddev->dev, "Dummy DMA ISR: Transfer failed. Buffers mismatch.\n");
    }

    return IRQ_HANDLED;
}

// --- Character Device File Operations Implementation ---

static int dummy_open(struct inode *inode, struct file *file)
{
    struct dummy_dma_dev *ddev = container_of(inode->i_cdev, struct dummy_dma_dev, cdev);
    file->private_data = ddev;
    dev_info(ddev->dev, "Device opened.\n");
    return 0;
}

static int dummy_release(struct inode *inode, struct file *file)
{
    struct dummy_dma_dev *ddev = file->private_data;
    dev_info(ddev->dev, "Device closed.\n");
    return 0;
}

/**
 * @brief Handles user-space writes. Triggers the simulated DMA transfer.
 * The user writes the source data, which is then copied via DMA (simulation)
 * and the function blocks until the transfer completes.
 */
static ssize_t dummy_write(struct file *file, const char __user *buf, 
                           size_t count, loff_t *ppos)
{
    struct dummy_dma_dev *ddev = file->private_data;
    int ret;

    if (count != DMA_BUFFER_SIZE) {
        dev_err(ddev->dev, "Write size (%zu) must match DMA_BUFFER_SIZE (%d) to trigger transfer.\n", 
                count, DMA_BUFFER_SIZE);
        return -EINVAL;
    }

    // 1. Copy data from user space to the DMA source buffer
    if (copy_from_user(ddev->src_buf, buf, DMA_BUFFER_SIZE)) {
        return -EFAULT;
    }

    // 2. Reset the destination buffer to a known state (0xDD)
    // This proves the transfer overwrites the old data.
    memset(ddev->dst_buf, 0xDD, DMA_BUFFER_SIZE); 
    
    // 3. Reset the completion object, allowing us to wait on it again
    reinit_completion(&ddev->transfer_complete);

    // 4. Schedule the work to simulate DMA transfer
    schedule_work(&ddev->completion_work);

    // 5. Wait for the workqueue to complete the transfer (synchronous wait for user)
    // Wait up to 5 seconds (HZ * 5)
    ret = wait_for_completion_interruptible_timeout(&ddev->transfer_complete, HZ * 5);

    if (ret == 0) {
        dev_err(ddev->dev, "Transfer timed out after 5 seconds.\n");
        return -ETIMEDOUT;
    }
    if (ret == -ERESTARTSYS) {
        dev_warn(ddev->dev, "Transfer interrupted by signal.\n");
        return -ERESTARTSYS;
    }

    dev_info(ddev->dev, "Transfer triggered by user space and successfully completed (blocking wait finished).\n");
    
    // Return the number of bytes written by the user
    return count;
}

/**
 * @brief Handles user-space reads. Returns the data from the destination buffer.
 */
static ssize_t dummy_read(struct file *file, char __user *buf, 
                          size_t count, loff_t *ppos)
{
    struct dummy_dma_dev *ddev = file->private_data;
    size_t len = DMA_BUFFER_SIZE;

    // Check if user is reading from beyond the data size
    if (*ppos >= len)
        return 0;

    // Limit read size to available data size
    if (count > len - *ppos)
        count = len - *ppos;

    // Copy data from destination buffer to user space
    if (copy_to_user(buf, ddev->dst_buf + *ppos, count))
        return -EFAULT;

    *ppos += count;
    
    dev_info(ddev->dev, "Read %zu bytes from destination buffer.\n", count);
    
    return count;
}

// --- File Operations Struct ---
static const struct file_operations dummy_fops = {
    .owner   = THIS_MODULE,
    .open    = dummy_open,
    .release = dummy_release,
    .read    = dummy_read,
    .write   = dummy_write,
};

// --- Driver Core Functions (Platform Driver) ---

static int dummy_probe(struct platform_device *pdev)
{
    int ret;
    struct resource *res;
    struct dummy_dma_dev *ddev;
    
    // 1. Setup Device Structure
    ddev = devm_kzalloc(&pdev->dev, sizeof(*ddev), GFP_KERNEL);
    if (!ddev)
        return -ENOMEM;

    ddev->dev = &pdev->dev;
    platform_set_drvdata(pdev, ddev);

    // 2. CDEV Setup (Part 1: Allocation and Class Creation)
    ret = alloc_chrdev_region(&ddev->dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        dev_err(ddev->dev, "Failed to allocate char dev region: %d\n", ret);
        return ret;
    }
    // FIX: Using THIS_MODULE as the owner for class_create in kernel 5.15+
    ddev->dev_class = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(ddev->dev_class)) {
        dev_err(ddev->dev, "Failed to create class.\n");
        ret = PTR_ERR(ddev->dev_class);
        goto err_cleanup_region;
    }

    // 3. IRQ Setup
    ddev->irq = platform_get_irq(pdev, 0);
    if (ddev->irq < 0) {
        ret = ddev->irq;
        goto err_cleanup_class;
    }

    ret = devm_request_irq(&pdev->dev, ddev->irq, dummy_dma_isr, 
                        IRQF_SHARED, DRIVER_NAME, ddev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request IRQ %d: %d\n", ddev->irq, ret);
        goto err_cleanup_class;
    }
    
    // 4. Initialize DMA buffers
    ddev->src_buf = dma_alloc_coherent(ddev->dev, DMA_BUFFER_SIZE, &ddev->src_dma_handle, GFP_KERNEL);
    ddev->dst_buf = dma_alloc_coherent(ddev->dev, DMA_BUFFER_SIZE, &ddev->dst_dma_handle, GFP_KERNEL);
    if (!ddev->src_buf || !ddev->dst_buf) {
        dev_err(&pdev->dev, "Failed to allocate DMA coherent memory.\n");
        ret = -ENOMEM;
        goto err_dma_alloc;
    }

    // Populate source buffer with initial data (will be overwritten by user write)
    memset(ddev->src_buf, 0xAA, DMA_BUFFER_SIZE / 2);
    memset(ddev->src_buf + DMA_BUFFER_SIZE / 2, 0xBB, DMA_BUFFER_SIZE / 2);
    // Initialize destination buffer to a different value 
    memset(ddev->dst_buf, 0xCC, DMA_BUFFER_SIZE); 
    
    dev_info(&pdev->dev, "Buffers allocated. Src: 0x%llx, Dst: 0x%llx\n", 
            (unsigned long long)ddev->src_dma_handle, (unsigned long long)ddev->dst_dma_handle);
    
    // 5. Initialize Work and Completion structures
    INIT_WORK(&ddev->completion_work, dma_sim_worker);
    init_completion(&ddev->transfer_complete);
    
    // 6. CDEV Setup (Part 2: Initialize, Add to system, Create device file)
    cdev_init(&ddev->cdev, &dummy_fops);
    ddev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&ddev->cdev, ddev->dev_num, 1);
    if (ret < 0) {
        dev_err(ddev->dev, "Failed to add cdev: %d\n", ret);
        goto err_dma_alloc; // Error path includes freeing DMA buffers
    }
    
    // Creates /dev/dummy_dma
    if (IS_ERR(device_create(ddev->dev_class, ddev->dev, ddev->dev_num, ddev, DEVICE_NAME))) {
        dev_err(ddev->dev, "Failed to create device file /dev/%s.\n", DEVICE_NAME);
        ret = -EFAULT;
        goto err_cleanup_cdev;
    }

    // The automatic schedule_work call is removed. The user must now write to /dev/dummy_dma to start the transfer.
    dev_info(&pdev->dev, "Dummy DMA: Device successfully probed. User space control ready at /dev/%s.\n", DEVICE_NAME);

    return 0;

// --- Error Handling Paths ---
err_cleanup_cdev:
    cdev_del(&ddev->cdev);
err_dma_alloc:
    if (ddev->src_buf)
        dma_free_coherent(ddev->dev, DMA_BUFFER_SIZE, ddev->src_buf, ddev->src_dma_handle);
    if (ddev->dst_buf)
        dma_free_coherent(ddev->dev, DMA_BUFFER_SIZE, ddev->dst_buf, ddev->dst_dma_handle);
err_cleanup_class:
    class_destroy(ddev->dev_class);
err_cleanup_region:
    unregister_chrdev_region(ddev->dev_num, 1);
    return ret;
}

static int dummy_remove(struct platform_device *pdev)
{
    struct dummy_dma_dev *ddev = platform_get_drvdata(pdev);

    // 1. Clean up CDEV resources
    device_destroy(ddev->dev_class, ddev->dev_num);
    cdev_del(&ddev->cdev);
    class_destroy(ddev->dev_class);
    unregister_chrdev_region(ddev->dev_num, 1);

    // 2. Ensure the work item isn't running or scheduled before removal
    cancel_work_sync(&ddev->completion_work); 

    // 3. Free DMA buffers
    dma_free_coherent(ddev->dev, DMA_BUFFER_SIZE, ddev->src_buf, ddev->src_dma_handle);
    dma_free_coherent(ddev->dev, DMA_BUFFER_SIZE, ddev->dst_buf, ddev->dst_dma_handle);

    dev_info(&pdev->dev, "Dummy DMA: Device removed.\n");
    return 0;
}

// --- Platform Driver Definition ---

static struct platform_driver dummy_dma_driver = {
    .probe = dummy_probe,
    .remove = dummy_remove,
    .driver = {
        .name = DRIVER_NAME,
    },
};

// --- Module Init/Exit: Manually Register the Platform Device ---

static int __init dummy_dma_init(void)
{
    int ret;
    
    pr_info("Loading %s: Manual platform device creation.\n", DRIVER_NAME);
    
    // 1. Define the resources the device needs (IRQ only)
    struct resource dma_resources[] = {
        {
            .start  = DUMMY_DMA_IRQ,
            .end    = DUMMY_DMA_IRQ,
            .flags  = IORESOURCE_IRQ,
        },
    };
    
    // 2. Allocate and initialize the platform device structure
    dummy_pdev = platform_device_alloc(DRIVER_NAME, -1);
    if (!dummy_pdev) {
        pr_err("Failed to allocate platform device.\n");
        return -ENOMEM;
    }
    
    ret = platform_device_add_resources(dummy_pdev, dma_resources, ARRAY_SIZE(dma_resources));
    if (ret) {
        pr_err("Failed to add resources to platform device.\n");
        goto err_add_resources;
    }
    
    // 3. Register the driver first
    ret = platform_driver_register(&dummy_dma_driver);
    if (ret) {
        pr_err("Failed to register platform driver: %d\n", ret);
        goto err_driver_register;
    }
    
    // 4. Register the device (This triggers the probe function!)
    ret = platform_device_add(dummy_pdev);
    if (ret) {
        pr_err("Failed to add platform device: %d\n", ret);
        goto err_add_device;
    }
    
    pr_info("%s loaded successfully and device probed.\n", DRIVER_NAME);
    return 0;

err_add_device:
    platform_driver_unregister(&dummy_dma_driver);
err_driver_register:
    platform_device_del(dummy_pdev);
err_add_resources:
    platform_device_put(dummy_pdev);
    return ret;
}

static void __exit dummy_dma_exit(void)
{
    // 1. Unregister the device (triggers remove function)
    platform_device_unregister(dummy_pdev);
    
    // 2. Unregister the driver
    platform_driver_unregister(&dummy_dma_driver);
    
    pr_info("%s unloaded.\n", DRIVER_NAME);
}

module_init(dummy_dma_init);
module_exit(dummy_dma_exit);

// --- Module Information ---
MODULE_AUTHOR("Gemini");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dummy DMA driver with CDEV for QEMU simulation (MMIO removed, IRQ 32).");
MODULE_VERSION("1.4.1");

