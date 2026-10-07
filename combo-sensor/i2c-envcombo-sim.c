// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/mutex.h>

/*
it's simulation need node in dts
    &i2c1 {
        status = "okay";
    
       combo_sensor@39 {
           compatible = "combo-sensor";
          reg = <0x39>;
       };
    };

*/

#define ENV_COMBO_I2C_ADDR      0x39
#define ENV_COMBO_WHO_AM_I_VAL  0xEB

/* Register map size (0x00 – 0x12) */
#define ENV_COMBO_REG_COUNT     0x13

struct envcombo_sim {
	struct i2c_client *client;
	u8 regs[ENV_COMBO_REG_COUNT];
	struct mutex lock;
};

static struct i2c_driver envcombo_dummy_driver;

/* ---------- I2C operations ---------- */
static s32 envcombo_sim_read_byte(struct i2c_client *client, u8 reg)
{
	struct envcombo_sim *dev = i2c_get_clientdata(client);
	s32 val = -EINVAL;

	mutex_lock(&dev->lock);
	if (reg < ENV_COMBO_REG_COUNT)
		val = dev->regs[reg];
	mutex_unlock(&dev->lock);

	return val;
}

static s32 envcombo_sim_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
	struct envcombo_sim *dev = i2c_get_clientdata(client);

	mutex_lock(&dev->lock);
	if (reg < ENV_COMBO_REG_COUNT)
		dev->regs[reg] = val;
	mutex_unlock(&dev->lock);

	return 0;
}

static const struct i2c_device_id envcombo_sim_ids[] = {
	{ "sim-sensor", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, envcombo_sim_ids);

static int envcombo_sim_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct envcombo_sim *dev;

	dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	i2c_set_clientdata(client, dev);
	mutex_init(&dev->lock);

	/* Initialize fake register values */
	memset(dev->regs, 0, sizeof(dev->regs));
	dev->regs[0x00] = ENV_COMBO_WHO_AM_I_VAL; /* WHO_AM_I */
	dev->regs[0x01] = 0x01; /* TEMP_OUT_MSB = 0x012C => 30.0 °C */
	dev->regs[0x02] = 0x2C;
	dev->regs[0x03] = 60;   /* HUM_OUT = 60 * 0.5 %RH = 30 %RH */

	dev_info(&client->dev, "COMBO-SENSOR simulator ready\n");
	return 0;
}

static int envcombo_sim_remove(struct i2c_client *client)
{
	struct envcombo_sim *dev = i2c_get_clientdata(client);
	mutex_destroy(&dev->lock);
	return 0;
}

/* ---------- Adapter ops ---------- */
/*
 * In real hardware, i2c_smbus_* calls talk to the bus controller.
 * For the simulator, we override adapter algorithm to redirect
 * reads/writes to our fake register map.
 */
static s32 envcombo_master_xfer(struct i2c_adapter *adap,
				struct i2c_msg *msgs, int num)
{
	struct i2c_client *client = i2c_get_adapdata(adap);
	struct envcombo_sim *dev = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD) {
			/* Read: msgs[i].buf[0] already has reg addr? */
			u8 reg = msgs[i-1].buf[0]; /* simple assumption */
			if (reg < ENV_COMBO_REG_COUNT)
				msgs[i].buf[0] = dev->regs[reg];
		} else {
			/* Write: first byte = reg, second = val */
			u8 reg = msgs[i].buf[0];
			u8 val = msgs[i].buf[1];
			if (reg < ENV_COMBO_REG_COUNT)
				dev->regs[reg] = val;
		}
	}
	return num;
}

static u32 envcombo_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA;
}

static const struct i2c_algorithm envcombo_algo = {
	.master_xfer = envcombo_master_xfer,
	.functionality = envcombo_func,
};

static struct i2c_adapter envcombo_adapter = {
	.owner = THIS_MODULE,
	.class = I2C_CLASS_HWMON,
	.algo = &envcombo_algo,
	.name = "combo-sim-adapter",
};

/* ---------- Module init/exit ---------- */
static int __init envcombo_sim_init(void)
{
	struct i2c_board_info info = {
		I2C_BOARD_INFO("combo-sensor-i2c", ENV_COMBO_I2C_ADDR),
	};
	struct i2c_client *client;

	int ret = i2c_add_adapter(&envcombo_adapter);
	if (ret)
		return ret;

        client = i2c_new_client_device(&envcombo_adapter, &info);
        if (IS_ERR(client)) {
            i2c_del_adapter(&envcombo_adapter);
            return PTR_ERR(client);
        }

	i2c_set_adapdata(&envcombo_adapter, client);

	return 0;
}

static void __exit envcombo_sim_exit(void)
{
	struct i2c_client *client = i2c_get_adapdata(&envcombo_adapter);
	if (client)
		i2c_unregister_device(client);

	i2c_del_adapter(&envcombo_adapter);
}

module_init(envcombo_sim_init);
module_exit(envcombo_sim_exit);

MODULE_AUTHOR("OpenAI");
MODULE_DESCRIPTION("COMBO SENSOR I2C Simulator");
MODULE_LICENSE("GPL");

