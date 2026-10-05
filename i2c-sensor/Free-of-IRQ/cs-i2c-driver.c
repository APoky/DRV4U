// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/timer.h>

#define DRIVER_NAME "cs-i2c-driver"
#define REG_TEMPERATURE 0x00
#define REG_HUMIDITY    0x01
#define POLL_INTERVAL_MS 3000

struct cs_i2c_dev {
	struct i2c_client *client;
	struct work_struct work;
	struct timer_list timer;
	u8 temp;
	u8 humid;
};

// Bottom-half work: read I2C registers
static void cs_work(struct work_struct *w)
{
	struct cs_i2c_dev *dev = container_of(w, struct cs_i2c_dev, work);
	s32 t = i2c_smbus_read_byte_data(dev->client, REG_TEMPERATURE);
	s32 h = i2c_smbus_read_byte_data(dev->client, REG_HUMIDITY);

	if (t < 0 || h < 0) {
		dev_err(&dev->client->dev, "I2C read failed: t=%d h=%d\n", t, h);
		return;
	}

	dev->temp = t;
	dev->humid = h;

	dev_info(&dev->client->dev, "Read: Temp=0x%02x(%d) Humid=0x%02x(%d)\n",
		 dev->temp, dev->temp, dev->humid, dev->humid);
}

// Timer callback: schedule the work to read I2C
static void cs_timer_cb(struct timer_list *t)
{
	struct cs_i2c_dev *dev = from_timer(dev, t, timer);
	schedule_work(&dev->work);
	mod_timer(&dev->timer, jiffies + msecs_to_jiffies(POLL_INTERVAL_MS));
}

// Probe: initialize workqueue and timer
static int cs_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct cs_i2c_dev *dev;

	dev_info(&client->dev, "Probe called for device 0x%x\n", client->addr);

	dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->client = client;
	i2c_set_clientdata(client, dev);

	INIT_WORK(&dev->work, cs_work);
	timer_setup(&dev->timer, cs_timer_cb, 0);
	mod_timer(&dev->timer, jiffies + msecs_to_jiffies(POLL_INTERVAL_MS));

	dev_info(&client->dev, "Bound successfully, polling every %d ms\n", POLL_INTERVAL_MS);
	return 0;
}

static int cs_remove(struct i2c_client *client)
{
	struct cs_i2c_dev *dev = i2c_get_clientdata(client);
	cancel_work_sync(&dev->work);
	del_timer_sync(&dev->timer);
	dev_info(&client->dev, "Removed\n");
	return 0;
}

static const struct i2c_device_id cs_ids[] = {
	{ "i2c-envcombo-sim", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cs_ids);

static struct i2c_driver cs_driver = {
	.driver = { .name = DRIVER_NAME },
	.probe  = cs_probe,
	.remove = cs_remove,
	.id_table = cs_ids,
};

module_i2c_driver(cs_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ChatGPT & Gemini");
MODULE_DESCRIPTION("Client driver polling virtual sensor via I2C every 3 seconds.");

