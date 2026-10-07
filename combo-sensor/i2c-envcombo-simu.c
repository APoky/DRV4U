// SPDX-License-Identifier: GPL-2.0
/*
 * i2c-envcombo-sim.c
 * Minimal ENV-COMBO I2C sensor simulator
 * Creates a virtual I2C adapter and registers a fake client device
 * (type "env-combo" @ 0x39). The adapter implements a tiny master_xfer
 * to emulate SMBus byte and byte-data accesses used by env_combo driver.
 *
 * Build with the Makefile in the same directory.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#define ENV_COMBO_I2C_ADDR    0x39
#define ENV_COMBO_REG_COUNT   0x13
#define ENV_COMBO_WHO_AM_I    0xEB

struct envcombo_sim {
	u8 regs[ENV_COMBO_REG_COUNT];
	struct mutex lock;
	struct i2c_adapter adapter; /* virtual adapter */
	struct i2c_client *client; /* the fake client we register */
};

static struct envcombo_sim *sim;

/* Helper: read register with bounds check */
static u8 sim_reg_read(u8 reg)
{
	if (reg >= ENV_COMBO_REG_COUNT)
		return 0;
	return sim->regs[reg];
}

/* Helper: write register with bounds check */
static void sim_reg_write(u8 reg, u8 val)
{
	if (reg >= ENV_COMBO_REG_COUNT)
		return;
	sim->regs[reg] = val;
}

/* Very small master_xfer implementing the SMBus byte and byte-data
 * patterns that env_combo driver uses (i2c_smbus_read_byte_data,
 * i2c_smbus_read_word_data could be used but we emulate the underlying
 * message sequences). This is NOT a full implementation but enough for
 * simple testing.
 */
static int envcombo_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int i;

	/* We're a single-device adapter. We expect sequences like:
	 *  - Write  : [ addr+w ][ reg ]
	 *  - Read   : [ addr+r ][ data ]  (often preceded by a write of reg)
	 *  - Write  : [ addr+w ][ reg ][ val ] (for byte-data write)
	 * We'll implement a simple heuristic: if a write msg with 1 byte
	 * is followed by a read, treat it as register-pointer + read.
	 */

	for (i = 0; i < num; i++) {
		struct i2c_msg *m = &msgs[i];

		/* Only handle transactions addressed to our device */
		if ((m->addr & 0x3ff) != ENV_COMBO_I2C_ADDR)
			return -ENXIO;

		if (m->flags & I2C_M_RD) {
			/* Read: return simulated register value(s) */
			/* If previous message was a 1-byte write (register pointer), use it */
			if (i > 0 && !(msgs[i-1].flags & I2C_M_RD) && msgs[i-1].len >= 1) {
				u8 reg = msgs[i-1].buf[0];
				int j;
				for (j = 0; j < m->len; j++)
					m->buf[j] = sim_reg_read(reg + j);
			} else {
				/* If no pointer, return starting from 0 */
				int j;
				for (j = 0; j < m->len; j++)
					m->buf[j] = sim_reg_read(j);
			}
		} else {
			/* Write: interpret first byte as register pointer and subsequent
			 * bytes as data to write at that pointer (common for byte-data and
			 * block writes). If len==1 we just set the pointer (no-op here).
			 */
			if (m->len >= 2) {
				u8 reg = m->buf[0];
				int j;
				for (j = 1; j < m->len; j++)
					sim_reg_write(reg + (j - 1), m->buf[j]);
			}
			/* len==1 -> just pointer set; nothing to store */
		}
	}

	return num;
}

static u32 envcombo_func(struct i2c_adapter *adap)
{
	/* advertise basic SMBus byte and byte-data functionality */
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA;
}

static const struct i2c_algorithm envcombo_algo = {
	.master_xfer = envcombo_master_xfer,
	.functionality = envcombo_func,
};

static int __init envcombo_sim_init(void)
{
	struct i2c_board_info info = { };
	int ret;

	sim = kzalloc(sizeof(*sim), GFP_KERNEL);
	if (!sim)
		return -ENOMEM;

	mutex_init(&sim->lock);

	/* Initialize registers with sensible defaults */
	memset(sim->regs, 0, sizeof(sim->regs));
	sim->regs[0x00] = ENV_COMBO_WHO_AM_I; /* WHO_AM_I */
	/* Example: TEMP = 25.00 C -> raw = 2500 = 0x09C4 -> MSB=0x09, LSB=0xC4 */
	sim->regs[0x01] = 0x09; /* TEMP_OUTMSB */
	sim->regs[0x02] = 0xC4; /* TEMP_OUTLSB */
	sim->regs[0x03] = 60;   /* HUM_OUT  (60 * 0.5 = 30 %RH) */

	/* Setup adapter struct */
	sim->adapter.owner = THIS_MODULE;
	sim->adapter.class = I2C_CLASS_HWMON;
	sim->adapter.algo = &envcombo_algo;
	sim->adapter.dev.parent = NULL;
	strncpy(sim->adapter.name, "combo-sim-adapter", sizeof(sim->adapter.name));

	ret = i2c_add_adapter(&sim->adapter);
	if (ret) {
		pr_err("combo-sim: failed to add adapter: %d\n", ret);
		kfree(sim);
		return ret;
	}

	/* Create a fake client on this virtual adapter */
	strncpy(info.type, "combo-sensor-i2c", I2C_NAME_SIZE);
	info.addr = ENV_COMBO_I2C_ADDR;

	sim->client = i2c_new_client_device(&sim->adapter, &info);
	if (IS_ERR(sim->client)) {
		pr_err("envcombo-sim: i2c_new_client_device failed\n");
		i2c_del_adapter(&sim->adapter);
		kfree(sim);
		return PTR_ERR(sim->client);
	}

	pr_info("envcombo-sim: simulator initialized, device at 0x%02x\n", ENV_COMBO_I2C_ADDR);
	return 0;
}

static void __exit envcombo_sim_exit(void)
{
	if (!sim)
		return;

	if (sim->client)
		i2c_unregister_device(sim->client);

	i2c_del_adapter(&sim->adapter);

	kfree(sim);
	pr_info("envcombo-sim: simulator removed\n");
}

module_init(envcombo_sim_init);
module_exit(envcombo_sim_exit);

MODULE_AUTHOR("OpenAI");
MODULE_DESCRIPTION("ENV-COMBO I2C simulator (virtual adapter + fake client)");
MODULE_LICENSE("GPL");

