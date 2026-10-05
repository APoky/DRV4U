#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/of_irq.h> // For OF IRQ helpers
#include <linux/irq.h>    // For firing the simulated IRQ

#define DRIVER_NAME "i2c-sim-adapter"
#define SIM_DEVICE_ADDRESS 0x40
#define IRQ_FIRE_INTERVAL_SEC 3 // Fire an IRQ every 3 seconds

// --- Simulated Register State ---
struct sim_data {
	u8 last_reg_addr;  // Stores the register address requested by the last write
	u8 temp_reg_val;   // Simulated Temperature Register (0x00)
	u8 humid_reg_val;  // Simulated Humidity Register (0x01)
	int sensor_irq;    // The IRQ number to fire (read from DTS: 100)
	struct timer_list irq_timer; // Timer for periodic IRQ firing
};

static struct sim_data sim_registers = {0};

// --- I2C Transfer Handler (The core of the data reading simulation) ---
static int sim_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct i2c_msg *msg;
	int i;

	// Check if the transaction targets our simulated device
	if (num < 1 || msgs[0].addr != SIM_DEVICE_ADDRESS) {
		return -ENODEV;
	}

	for (i = 0; i < num; i++) {
		msg = &msgs[i];
		
		if (msg->flags & I2C_M_RD) {
			// --- 2. READ Message (The driver is requesting data after the IRQ) ---
			// Check for valid combined message sequence (Write Address, then Read Data)
			if (i == 0 || !(msgs[i-1].flags & I2C_M_RD)) {
				// This is a repeated start or sequential read following a register write
			}

			if (sim_registers.last_reg_addr == 0x00) {
				*msg->buf = sim_registers.temp_reg_val;
				dev_info(&adap->dev, "Sim: Responded to READ Temp (0x%02x) with 0x%02x.\n", 
						 sim_registers.last_reg_addr, sim_registers.temp_reg_val);
			} else if (sim_registers.last_reg_addr == 0x01) {
				*msg->buf = sim_registers.humid_reg_val;
				dev_info(&adap->dev, "Sim: Responded to READ Humid (0x%02x) with 0x%02x.\n", 
						 sim_registers.last_reg_addr, sim_registers.humid_reg_val);
			} else {
				*msg->buf = 0xFF; 
				dev_warn(&adap->dev, "Sim: Unknown register 0x%02x requested. Returning 0xFF.\n", 
						 sim_registers.last_reg_addr);
			}
		} else {
			// --- 1. WRITE Message (The driver is sending the register address) ---
			if (msg->len != 1) {
				dev_err(&adap->dev, "Write message must be 1 byte (register address).\n");
				return -EIO;
			}
			sim_registers.last_reg_addr = msg->buf[0];
			dev_dbg(&adap->dev, "Sim: Received WRITE, next read will be from register 0x%02x.\n", 
					sim_registers.last_reg_addr);
		}
	}

	return num;
}

// --- I2C Adapter Definition ---
static u32 sim_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_READ_BYTE_DATA | I2C_FUNC_I2C;
}

static const struct i2c_algorithm sim_algo = {
	.master_xfer    = sim_master_xfer,
	.functionality  = sim_func,
};

static struct i2c_adapter sim_adapter = {
	.owner          = THIS_MODULE,
	.class          = I2C_CLASS_HWMON,
	.algo           = &sim_algo,
	.name           = "Virtual Sensor Adapter",
	.nr             = -1,
};

// --- IRQ Timer Logic ---

/**
 * @brief Generates new random data and fires the simulated IRQ.
 */
static void irq_timer_callback(struct timer_list *t)
{
	// 1. Generate new random data
	get_random_bytes(&sim_registers.temp_reg_val, 1);
	get_random_bytes(&sim_registers.humid_reg_val, 1);
	
	pr_info(DRIVER_NAME ": New data ready (T: 0x%02x, H: 0x%02x). Firing IRQ %d...\n", 
			sim_registers.temp_reg_val, sim_registers.humid_reg_val, sim_registers.sensor_irq);

	// 2. Fire the IRQ to notify the cs-driver
	if (sim_registers.sensor_irq > 0) {
		// This manually triggers the IRQ handler in the cs-driver
		generic_handle_irq(sim_registers.sensor_irq);
	}

	// 3. Reschedule the timer
	mod_timer(&sim_registers.irq_timer, jiffies + (IRQ_FIRE_INTERVAL_SEC * HZ));
}

// --- Module Init/Exit ---

static int __init sim_adapter_init(void)
{
	int ret;
	struct device_node *sensor_node;

	pr_info(DRIVER_NAME ": Registering virtual I2C adapter.\n");

	// 1. Register the adapter (creates the virtual I2C bus)
	ret = i2c_add_adapter(&sim_adapter);
	if (ret < 0) {
		pr_err(DRIVER_NAME ": Failed to add I2C adapter: %d\n", ret);
		return ret;
	}

	// 2. Find the sensor device node to get the IRQ number
	// Searches for the node defined in sim_device.dtsi: sim_sensor@40
	sensor_node = of_find_compatible_node(NULL, NULL, "i2c-envcombo-sim");
	if (!sensor_node) {
		pr_err(DRIVER_NAME ": Failed to find I2C sensor device node in DTS. Ensure DTS is loaded.\n");
		ret = -ENODEV;
		goto err_del_adapter;
	}
	
	// 3. Get the IRQ number (100 in the DTS)
	sim_registers.sensor_irq = irq_of_parse_and_map(sensor_node, 0);
	of_node_put(sensor_node); // Release the node reference
	
	if (sim_registers.sensor_irq <= 0) {
		pr_err(DRIVER_NAME ": Failed to parse IRQ from device tree.\n");
		ret = -EINVAL;
		goto err_del_adapter;
	}

	// 4. Initialize and start the IRQ timer
	timer_setup(&sim_registers.irq_timer, irq_timer_callback, 0);
	// Fire immediately upon load, then every 3 seconds
	mod_timer(&sim_registers.irq_timer, jiffies + msecs_to_jiffies(100)); 

	pr_info(DRIVER_NAME ": Virtual adapter registered as i2c-%d. IRQ Timer started (IRQ %d).\n", 
			sim_adapter.nr, sim_registers.sensor_irq);
	return 0;

err_del_adapter:
	i2c_del_adapter(&sim_adapter);
	return ret;
}

static void __exit sim_adapter_exit(void)
{
	del_timer_sync(&sim_registers.irq_timer);
	i2c_del_adapter(&sim_adapter);
	pr_info(DRIVER_NAME ": Unloaded virtual I2C adapter and timer.\n");
}

module_init(sim_adapter_init);
module_exit(sim_adapter_exit);

MODULE_AUTHOR("Gemini");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Virtual I2C Adapter that simulates register reads and periodically fires an IRQ.");

