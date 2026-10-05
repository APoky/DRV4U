#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/of_irq.h> 
#include <linux/fs.h>
#include <linux/platform_device.h>

// --- Definitions ---
#define DRIVER_NAME "cs-i2c-driver"

// Simulated Register Addresses
#define REG_TEMPERATURE 0x00
#define REG_HUMIDITY    0x01

// --- Data Structures ---
struct cs_i2c_dev {
	struct i2c_client *client;
	struct work_struct read_work; // Workqueue for processing IRQ event
	u8 temp_val;
	u8 humid_val;
};

// --- Function Prototypes ---
static irqreturn_t cs_irq_handler(int irq, void *dev_id);
static void cs_read_data_worker(struct work_struct *work);

/**
 * @brief Work function executed upon IRQ. Reads the I2C registers.
 * This runs in process context (bottom half) and can safely block for I2C operations.
 */
static void cs_read_data_worker(struct work_struct *work)
{
	struct cs_i2c_dev *cdev_dev = container_of(work, struct cs_i2c_dev, read_work);
	struct i2c_client *client = cdev_dev->client;
	s32 temp, humid;

	dev_info(&client->dev, "WORKER: Processing IRQ event. Starting I2C read...\n");

	// 1. Read Temperature Register (0x00)
	// i2c_smbus_read_byte_data performs a combined Write (Reg Address) followed by a Read (Data)
	temp = i2c_smbus_read_byte_data(client, REG_TEMPERATURE);
	
	// 2. Read Humidity Register (0x01)
	humid = i2c_smbus_read_byte_data(client, REG_HUMIDITY);

	if (temp < 0 || humid < 0) {
		dev_err(&client->dev, "I2C read failed in worker! Temp: %d, Humid: %d\n", temp, humid);
		return;
	}

	cdev_dev->temp_val = (u8)temp;
	cdev_dev->humid_val = (u8)humid;

	dev_info(&client->dev, "WORKER: Data received: Temp=0x%02x (%d), Humidity=0x%02x (%d). Waiting for next IRQ...\n", 
			 cdev_dev->temp_val, cdev_dev->temp_val, cdev_dev->humid_val, cdev_dev->humid_val);
}

/**
 * @brief Top-half IRQ handler. Schedules the work function.
 * This runs in atomic context (high priority) and MUST be very fast.
 */
static irqreturn_t cs_irq_handler(int irq, void *dev_id)
{
	struct cs_i2c_dev *cdev_dev = (struct cs_i2c_dev *)dev_id;

	dev_dbg(&cdev_dev->client->dev, "IRQ %d received (Data Ready). Scheduling work.\n", irq);
	
	// The IRQ fires -> schedule the bottom half (workqueue) to perform the actual I2C transaction
	schedule_work(&cdev_dev->read_work);

	return IRQ_HANDLED;
}

// --- I2C Driver Core Functions ---

static int cs_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	struct cs_i2c_dev *cdev_dev;
	int irq;

	dev_info(&client->dev, "CS Driver Probe called for device at 0x%x.\n", client->addr);

	// 1. Allocate and initialize device structure
	cdev_dev = devm_kzalloc(&client->dev, sizeof(*cdev_dev), GFP_KERNEL);
	if (!cdev_dev)
		return -ENOMEM;

	cdev_dev->client = client;
	i2c_set_clientdata(client, cdev_dev);

	// 2. Get the IRQ number (100) from the DTS 
        irq = platform_get_irq(to_platform_device(&client->dev), 0);
        if (irq < 0) {
		dev_err(&client->dev, "Failed to get IRQ number from device tree: %d\n", irq);
		return irq;
	}

	// 3. Initialize Workqueue
	INIT_WORK(&cdev_dev->read_work, cs_read_data_worker);

	// 4. Request IRQ line
	ret = devm_request_irq(&client->dev, irq, cs_irq_handler, 
						   IRQF_SHARED, DRIVER_NAME, cdev_dev);
	if (ret) {
		dev_err(&client->dev, "Failed to request IRQ %d: %d\n", irq, ret);
		return ret;
	}
	
	dev_info(&client->dev, "CS Driver: Successfully bound and listening on IRQ %d.\n", irq);
	return 0;
}

static int cs_remove(struct i2c_client *client)
{
	struct cs_i2c_dev *cdev_dev = i2c_get_clientdata(client);
	
	// Ensure no pending work before removing the device
	cancel_work_sync(&cdev_dev->read_work);

	dev_info(&client->dev, "CS Driver: Device removed.\n");
	
	return 0;
}

// Device matching table for the "i2c-envcombo-sim" device name defined in the DTS
static const struct i2c_device_id cs_id[] = {
	{ "i2c-envcombo-sim", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cs_id);

// --- I2C Driver Definition ---
static struct i2c_driver cs_driver = {
	.driver = {
		.name = DRIVER_NAME,
	},
	.probe = cs_probe,
	.remove = cs_remove,
	.id_table = cs_id,
};

module_i2c_driver(cs_driver);

MODULE_AUTHOR("Gemini");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Combo Sensor I2C driver triggered by IRQ to read simulated I2C registers.");

