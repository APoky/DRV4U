// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/timer.h>
#include <linux/random.h>

#define DRIVER_NAME "i2c-sim-adapter"
#define SIM_DEVICE_ADDR 0x40
#define DATA_UPDATE_INTERVAL_SEC 3

struct sim_state {
	u8 last_reg;
	u8 temp;
	u8 humid;
	struct timer_list timer;
	struct i2c_client *client;
};

static struct sim_state sim;

// -----------------------------------------------------------------------------
// I2C bus algorithm (basic register read/write simulation)
// -----------------------------------------------------------------------------
static int sim_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int i;
	for (i = 0; i < num; i++) {
		struct i2c_msg *msg = &msgs[i];

		if (msg->addr != SIM_DEVICE_ADDR)
			return -ENODEV;

		if (msg->flags & I2C_M_RD) {
			u8 val = 0xFF;
			switch (sim.last_reg) {
			case 0x00: val = sim.temp; break;
			case 0x01: val = sim.humid; break;
			}
			msg->buf[0] = val;
			dev_info(&adap->dev, "READ reg=0x%02x val=0x%02x\n", sim.last_reg, val);
		} else {
			sim.last_reg = msg->buf[0];
			dev_dbg(&adap->dev, "WRITE set reg=0x%02x\n", sim.last_reg);
		}
	}
	return num;
}

static u32 sim_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_READ_BYTE_DATA;
}

static const struct i2c_algorithm sim_algo = {
	.master_xfer   = sim_master_xfer,
	.functionality = sim_func,
};

static struct i2c_adapter sim_adapter = {
	.owner = THIS_MODULE,
	.class = I2C_CLASS_HWMON,
	.algo  = &sim_algo,
	.name  = "Simulated I2C Bus",
};

// -----------------------------------------------------------------------------
// Timer callback: generate new sensor data periodically
// -----------------------------------------------------------------------------
static void sim_timer_cb(struct timer_list *t)
{
	get_random_bytes(&sim.temp, 1);
	get_random_bytes(&sim.humid, 1);

	pr_info(DRIVER_NAME ": New data ready (Temp=0x%02x, Humid=0x%02x)\n",
		sim.temp, sim.humid);

	mod_timer(&sim.timer, jiffies + DATA_UPDATE_INTERVAL_SEC * HZ);
}

// -----------------------------------------------------------------------------
// Module Init / Exit
// -----------------------------------------------------------------------------
static int __init sim_init(void)
{
	struct i2c_board_info info = {
		I2C_BOARD_INFO("i2c-envcombo-sim", SIM_DEVICE_ADDR),
	};
	int ret;

	pr_info(DRIVER_NAME ": Initializing simulated I2C adapter\n");

	// 1. Register I2C adapter
	ret = i2c_add_adapter(&sim_adapter);
	if (ret)
		return ret;

	// 2. Create virtual client device (I2C slave)
	sim.client = i2c_new_client_device(&sim_adapter, &info);
	if (IS_ERR(sim.client)) {
		pr_err(DRIVER_NAME ": Failed to create virtual I2C client\n");
		i2c_del_adapter(&sim_adapter);
		return PTR_ERR(sim.client);
	}

	// 3. Setup periodic timer
	timer_setup(&sim.timer, sim_timer_cb, 0);
	mod_timer(&sim.timer, jiffies + msecs_to_jiffies(500));

	pr_info(DRIVER_NAME ": Virtual adapter created. Device at 0x%x\n", SIM_DEVICE_ADDR);
	return 0;
}

static void __exit sim_exit(void)
{
	del_timer_sync(&sim.timer);
	if (sim.client)
		i2c_unregister_device(sim.client);
	i2c_del_adapter(&sim_adapter);
	pr_info(DRIVER_NAME ": Unloaded\n");
}

module_init(sim_init);
module_exit(sim_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ChatGPT & Gemini");
MODULE_DESCRIPTION("Standalone virtual I2C adapter and device simulator (no IRQ, decoupled).");

