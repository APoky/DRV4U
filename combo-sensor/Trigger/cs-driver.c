// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/err.h> // For IS_ERR and PTR_ERR

#define ENV_COMBO_REG_WHO_AM_I      0x00
#define ENV_COMBO_REG_TEMP_OUT_MSB  0x01
#define ENV_COMBO_REG_TEMP_OUT_LSB  0x02
#define ENV_COMBO_REG_HUM_OUT       0x03
#define ENV_COMBO_WHO_AM_I_VAL      0xEB

struct env_combo_data {
	struct i2c_client *client;
	struct mutex lock;
	int irq;
	struct iio_trigger *trig;
};

/* Read temperature (signed 16-bit, 0.01 °C per LSB) */
static int env_combo_read_temp(struct env_combo_data *data, int *val)
{
	s32 msb = i2c_smbus_read_byte_data(data->client, ENV_COMBO_REG_TEMP_OUT_MSB);
	s32 lsb = i2c_smbus_read_byte_data(data->client, ENV_COMBO_REG_TEMP_OUT_LSB);
	if (msb < 0 || lsb < 0)
		return -EIO;

	*val = (s16)((msb << 8) | lsb);
	return IIO_VAL_INT;
}

/* Read humidity (unsigned 8-bit, 0.5% RH per LSB) */
static int env_combo_read_humidity(struct env_combo_data *data, int *val)
{
	s32 raw = i2c_smbus_read_byte_data(data->client, ENV_COMBO_REG_HUM_OUT);
	if (raw < 0)
		return -EIO;

	*val = raw;
	return IIO_VAL_INT;
}

/* IIO read_raw callback */
static int env_combo_read_raw(struct iio_dev *indio_dev,
		const struct iio_chan_spec *chan,
		int *val, int *val2, long mask)
{
	struct env_combo_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	switch (chan->type) {
	case IIO_TEMP:
		ret = env_combo_read_temp(data, val);
		if (ret == IIO_VAL_INT)
			*val2 = 100; // 0.01°C per LSB
		break;
	case IIO_HUMIDITYRELATIVE:
		ret = env_combo_read_humidity(data, val);
		if (ret == IIO_VAL_INT)
			*val2 = 2; // 0.5% RH per LSB
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&data->lock);

	return ret;
}

/* Channel definitions */
static const struct iio_chan_spec env_combo_channels[] = {
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_HUMIDITYRELATIVE,
		.indexed = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	},
};

/* IIO device info */
static const struct iio_info env_combo_info = {
	.read_raw = env_combo_read_raw,
};

/* IRQ handler: push data to buffer and notify */
static irqreturn_t env_combo_irq_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct env_combo_data *data = iio_priv(indio_dev);
	s64 sample[2];
	int temp, hum;

	mutex_lock(&data->lock);
	if (env_combo_read_temp(data, &temp) < 0)
		temp = 0;
	if (env_combo_read_humidity(data, &hum) < 0)
		hum = 0;
	mutex_unlock(&data->lock);

	sample[0] = temp;
	sample[1] = hum;

	iio_push_to_buffers(indio_dev, sample);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

/* Probe function */
static int env_combo_probe(struct i2c_client *client,
                           const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct env_combo_data *data;
	int whoami, ret;

	whoami = i2c_smbus_read_byte_data(client, ENV_COMBO_REG_WHO_AM_I);
	if (whoami != ENV_COMBO_WHO_AM_I_VAL)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	data->irq = client->irq;
	mutex_init(&data->lock);

	i2c_set_clientdata(client, data);

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = "env-combo";
	indio_dev->info = &env_combo_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = env_combo_channels;
	indio_dev->num_channels = ARRAY_SIZE(env_combo_channels);

	if (data->irq > 0) {
		data->trig = devm_iio_trigger_alloc(&client->dev,
						    "%s-dev%d", client->name, client->addr);
		if (IS_ERR(data->trig))
			return PTR_ERR(data->trig);

		indio_dev->trig = data->trig;

		ret = devm_request_threaded_irq(&client->dev, data->irq,
						NULL, env_combo_irq_handler,
						IRQF_ONESHOT | IRQF_TRIGGER_RISING,
						client->name, indio_dev);
		if (ret)
			return ret;

		ret = devm_iio_triggered_buffer_setup(&client->dev,
						      indio_dev,
						      NULL,
						      env_combo_irq_handler,
						      NULL);
		if (ret)
			return ret;
	}

	return devm_iio_device_register(&client->dev, indio_dev);
}

/* I2C device ID table */
static const struct i2c_device_id env_combo_id[] = {
	{ "env-combo", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, env_combo_id);

/* I2C driver definition */
static struct i2c_driver env_combo_driver = {
	.driver = {
		.name = "env-combo",
	},
	.probe = env_combo_probe,
	.id_table = env_combo_id,
};
module_i2c_driver(env_combo_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tester");
MODULE_DESCRIPTION("ENV-COMBO IIO driver (Temp+Humidity) with IRQ triggered buffer");

