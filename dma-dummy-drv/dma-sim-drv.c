#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/mod_devicetable.h> // <-- FIX: Added this header for struct of_device_id

// --- Definitions ---
#define DRIVER_NAME "dummy-dma-drv"
#define DMA_BUFFER_SIZE 32
#define SIMULATED_TRANSFER_TIME_MS 100 // Simulate 100ms transfer time

// Dummy register offsets (MMIO simulation)
#define DMA_REG_CONTROL 0x0
#define DMA_REG_STATUS 0x4

// --- Data Structures ---
struct dummy_dma_dev {
    struct device *dev;
    void __iomem *regs;
    int irq;

    // Simulated DMA Buffers (Kmalloc'd, but conceptually DMA-coherent)
    u8 *src_buf;
    u8 *dst_buf;
    dma_addr_t src_dma_handle;
    dma_addr_t dst_dma_handle;

    // Work structure for simulating asynchronous hardware transfer
    struct work_struct completion_work;
};

// --- Function Prototypes ---
static irqreturn_t dummy_dma_isr(int irq, void *dev_id);
static void dma_sim_worker(struct work_struct *work);

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
    // In a real driver, the hardware does this. Here, we mimic it.
    msleep(SIMULATED_TRANSFER_TIME_MS);
    
    // Perform the actual data copy (this is the simulated "hardware" action)
    memcpy(ddev->dst_buf, ddev->src_buf, DMA_BUFFER_SIZE);
    
    // 2. Simulate hardware completion (writing to a register)
    iowrite32(0xDEADBEEF, ddev->regs + DMA_REG_STATUS); // Write a dummy value to status reg
    
    dev_info(ddev->dev, "DMA Sim: Data copy complete. Triggering interrupt handler logic.\n");
    
    // 3. Trigger the ISR logic (This is what a real hardware interrupt would achieve)
    // Note: We are just calling the ISR handler directly, bypassing the hardware IRQ line, 
    // because we are already in a safe interrupt context (workqueue).
    dummy_dma_isr(ddev->irq, ddev);
}

// --- Interrupt Handler (The "CPU" Side) ---

/**
 * @brief The actual interrupt service routine registered for IRQ 105.
 * This runs when the "hardware" (our worker thread) signals completion.
 */
static irqreturn_t dummy_dma_isr(int irq, void *dev_id)
{
    struct dummy_dma_dev *ddev = (struct dummy_dma_dev *)dev_id;
    u32 status;

    // 1. Read status register (clears the interrupt in real hardware)
    status = ioread32(ddev->regs + DMA_REG_STATUS);
    
    dev_info(ddev->dev, "Dummy DMA ISR: IRQ %d received! Status: 0x%x\n", irq, status);
    
    // 2. Verify Data Integrity (Test the simulation)
    if (memcmp(ddev->src_buf, ddev->dst_buf, DMA_BUFFER_SIZE) == 0) {
        dev_info(ddev->dev, "Dummy DMA ISR: Transfer successful. Buffers match.\n");
    } else {
        dev_err(ddev->dev, "Dummy DMA ISR: Transfer failed. Buffers mismatch.\n");
    }

    // This IRQ handler never returns IRQ_NONE because it's always triggered intentionally
    return IRQ_HANDLED;
}

// --- Driver Core Functions (Platform Driver) ---

static int dummy_probe(struct platform_device *pdev)
{
    int ret;
    struct resource *res;
    struct dummy_dma_dev *ddev;
    
    ddev = devm_kzalloc(&pdev->dev, sizeof(*ddev), GFP_KERNEL);
    if (!ddev)
        return -ENOMEM;

    ddev->dev = &pdev->dev;
    platform_set_drvdata(pdev, ddev);

    // 1. MMIO Register Mapping
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    ddev->regs = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(ddev->regs)) {
        dev_err(&pdev->dev, "Failed to map device registers.\n");
        return PTR_ERR(ddev->regs);
    }
    dev_info(&pdev->dev, "Registers mapped to %p\n", ddev->regs);
    
    // 2. IRQ Setup
    ddev->irq = platform_get_irq(pdev, 0);
    if (ddev->irq < 0)
        return ddev->irq;

    ret = devm_request_irq(&pdev->dev, ddev->irq, dummy_dma_isr, 
                           IRQF_SHARED, DRIVER_NAME, ddev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request IRQ %d: %d\n", ddev->irq, ret);
        return ret;
    }
    
    // 3. Initialize DMA buffers
    ddev->src_buf = dma_alloc_coherent(ddev->dev, DMA_BUFFER_SIZE, &ddev->src_dma_handle, GFP_KERNEL);
    ddev->dst_buf = dma_alloc_coherent(ddev->dev, DMA_BUFFER_SIZE, &ddev->dst_dma_handle, GFP_KERNEL);
    if (!ddev->src_buf || !ddev->dst_buf) {
        dev_err(&pdev->dev, "Failed to allocate DMA coherent memory.\n");
        ret = -ENOMEM;
        goto err_dma_alloc;
    }

    // Populate source buffer with known data
    memset(ddev->src_buf, 0xAA, DMA_BUFFER_SIZE / 2);
    memset(ddev->src_buf + DMA_BUFFER_SIZE / 2, 0xBB, DMA_BUFFER_SIZE / 2);
    // Initialize destination buffer to a different value to prove transfer occurred
    memset(ddev->dst_buf, 0xCC, DMA_BUFFER_SIZE); 
    
    dev_info(&pdev->dev, "Buffers allocated. Src: 0x%llx, Dst: 0x%llx\n", 
             (unsigned long long)ddev->src_dma_handle, (unsigned long long)ddev->dst_dma_handle);
    
    // 4. Initialize and Schedule Simulated Transfer
    INIT_WORK(&ddev->completion_work, dma_sim_worker);
    schedule_work(&ddev->completion_work); // Start the transfer immediately on probe

    dev_info(&pdev->dev, "Dummy DMA: Device successfully probed. Initial transfer scheduled.\n");

    return 0;

err_dma_alloc:
    if (ddev->src_buf)
        dma_free_coherent(ddev->dev, DMA_BUFFER_SIZE, ddev->src_buf, ddev->src_dma_handle); 
    if (ddev->dst_buf)
        dma_free_coherent(ddev->dev, DMA_BUFFER_SIZE, ddev->dst_buf, ddev->dst_dma_handle);
    return ret;
}

static void dummy_remove(struct platform_device *pdev) // <-- FIX: Changed return type from 'int' to 'void'
{
    struct dummy_dma_dev *ddev = platform_get_drvdata(pdev);

    // Ensure the work item isn't running or scheduled before removal
    cancel_work_sync(&ddev->completion_work); 

    // Free DMA buffers
    dma_free_coherent(ddev->dev, DMA_BUFFER_SIZE, ddev->src_buf, ddev->src_dma_handle);
    dma_free_coherent(ddev->dev, DMA_BUFFER_SIZE, ddev->dst_buf, ddev->dst_dma_handle);

    dev_info(&pdev->dev, "Dummy DMA: Device removed.\n");
    // Removed 'return 0;'
}

// --- Platform Driver Definition ---

static const struct of_device_id dummy_dma_of_match[] = {
    { .compatible = "vendor,dummy-dma-controller", },
    { }
};
MODULE_DEVICE_TABLE(of, dummy_dma_of_match);

static struct platform_driver dummy_dma_driver = {
    .probe = dummy_probe,
    .remove = dummy_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = dummy_dma_of_match,
    },
};

module_platform_driver(dummy_dma_driver);

MODULE_AUTHOR("Gemini");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dummy DMA driver for QEMU simulation");
MODULE_VERSION("1.0");

