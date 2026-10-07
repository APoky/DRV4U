/* SPDX-License-Identifier: GPL-2.0 */
/*

* i2c-envcombo-sim.c - Simple simulator for ENV-COMBO I2C sensor
*
* This module simulates an environmental sensor with temperature
* and humidity registers. It attaches to an existing I2C adapter
* (e.g. /dev/i2c-1) and responds to I2C read transactions.
  */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/slab.h>

#define ENV_COMBO_ADDR             0x39
#define ENV_COMBO_REG_WHO_AM_I     0x00
#define ENV_COMBO_REG_TEMP_OUT_MSB 0x01
#define ENV_COMBO_REG_TEMP_OUT_LSB 0x02
#define ENV_COMBO_REG_HUM_OUT      0x03
#define ENV_COMBO_REG_STATUS       0x0C

#define ENV_COMBO_WHO_AM_I_VAL     0xEB

struct envcombo_sim {
struct i2c_client *client;
struct timer_list timer;
u8 regs[16];   /* simple register map */
int temp;      /* simulated temperature in 0.01°C */
int hum;       /* simulated humidity % */
};

static struct envcombo_sim sim;

/* Timer callback: update simulated values */
static void envcombo_sim_timer_fn(struct timer_list *t)
{
sim.temp += 5;  /* increase temp by 0.05°C */
if (sim.temp > 3500) /* reset if > 35°C */
sim.temp = 2500;

sim.hum += 1;   /* increase humidity */
if (sim.hum > 90)
	sim.hum = 40;

/* Update registers */
sim.regs[ENV_COMBO_REG_TEMP_OUT_MSB] = (sim.temp >> 8) & 0xFF;
sim.regs[ENV_COMBO_REG_TEMP_OUT_LSB] = sim.temp & 0xFF;
sim.regs[ENV_COMBO_REG_HUM_OUT]      = sim.hum & 0xFF;

/* Reschedule timer (expire 1s from now)*/
mod_timer(&sim.timer, jiffies + HZ);

}

/* Simple emulated I2C read */
static s32 envcombo_sim_read_byte(struct i2c_client *client, u8 reg)
{
if (reg < sizeof(sim.regs))
return sim.regs[reg];
return -EINVAL;
}

/* Simple emulated I2C write */
static s32 envcombo_sim_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
if (reg < sizeof(sim.regs)) {
sim.regs[reg] = val;
return 0;
}
return -EINVAL;
}

static const struct i2c_device_id envcombo_sim_id[] = {
{ "env-combo-sim", 0 },
{ }
};
MODULE_DEVICE_TABLE(i2c, envcombo_sim_id);

static int __init envcombo_sim_init(void)
{
struct i2c_adapter *adap;

/* Attach to I2C bus 1 (i2c-1) */
adap = i2c_get_adapter(1);
if (!adap)
	return -ENODEV;

sim.client = i2c_new_dummy_device(adap, ENV_COMBO_ADDR);
i2c_put_adapter(adap);

if (IS_ERR(sim.client))
	return PTR_ERR(sim.client);

/* Initialize registers */
memset(sim.regs, 0, sizeof(sim.regs));
sim.regs[ENV_COMBO_REG_WHO_AM_I] = ENV_COMBO_WHO_AM_I_VAL;
sim.temp = 2500;  /* start at 25.00°C */
sim.hum  = 50;    /* start at 50% RH */

/* Start update timer */
timer_setup(&sim.timer, envcombo_sim_timer_fn, 0);
mod_timer(&sim.timer, jiffies + HZ);

pr_info("env-combo simulator attached at 0x%02x on i2c-1\n", ENV_COMBO_ADDR);
return 0;

}

static void __exit envcombo_sim_exit(void)
{
	del_timer_sync(&sim.timer);
	if (sim.client)
		i2c_unregister_device(sim.client);
	pr_info("env-combo simulator removed\n");
}

module_init(envcombo_sim_init);
module_exit(envcombo_sim_exit);

MODULE_AUTHOR("OpenAI ChatGPT");
MODULE_DESCRIPTION("ENV-COMBO I2C Sensor Simulator");
MODULE_LICENSE("GPL");
