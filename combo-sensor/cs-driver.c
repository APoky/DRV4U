/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h> 
#include <linux/iio/trigger_consumer.h>
#include <linux/mutex.h>
#include <linux/init.h> 
#include <linux/err.h> 

#define ENV_COMBO_REG_WHO_AM_I      0x00
#define ENV_COMBO_REG_TEMP_OUT_MSB  0x01
#define ENV_COMBO_REG_TEMP_OUT_LSB  0x02
#define ENV_COMBO_REG_HUM_OUT       0x03
#define ENV_COMBO_REG_STATUS        0x0C

#define ENV_COMBO_WHO_AM_I_VAL      0xEB

struct env_combo_data {
	struct i2c_client *client;
	struct mutex lock;
	struct iio_trigger *trig;
    struct iio_dev *indio_dev; 
};

/* --- Read Functions --- */

static int env_combo_read_temp(struct env_combo_data *data, int *val)
{
	s32 msb, lsb;
	msb = i2c_smbus_read_byte_data(data->client, ENV_COMBO_REG_TEMP_OUT_MSB);
	if (msb < 0) return msb;
	lsb = i2c_smbus_read_byte_data(data->client, ENV_COMBO_REG_TEMP_OUT_LSB);
	if (lsb < 0) return lsb;
	*val = (s16)((msb << 8) | lsb);
	return IIO_VAL_INT;
}

static int env_combo_read_humidity(struct env_combo_data *data, int *val)
{
	s32 raw = i2c_smbus_read_byte_data(data->client, ENV_COMBO_REG_HUM_OUT);
	if (raw < 0) return raw;
	*val = raw;
	return IIO_VAL_INT;
}

static int env_combo_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask)
{
	struct env_combo_data *data = iio_priv(indio_dev);
	int ret;
	mutex_lock(&data->lock);
	switch (chan->type) {
	case IIO_TEMP:
		ret = env_combo_read_temp(data, val);
		if (ret == IIO_VAL_INT) *val2 = 100;
		break;
	case IIO_HUMIDITYRELATIVE:
		ret = env_combo_read_humidity(data, val);
		if (ret == IIO_VAL_INT) *val2 = 2;
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&data->lock);
	return ret;
}

static const struct iio_chan_spec env_combo_channels[] = {
	{ .type = IIO_TEMP, .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE), },
	{ .type = IIO_HUMIDITYRELATIVE, .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE), },
};

static const struct iio_info env_combo_info = { .read_raw = env_combo_read_raw, };

static const struct i2c_device_id env_combo_ids[] = { { "combo-sensor-i2c", 0 }, { } };


/* --- Probe Function (using low-level kfifo buffer API) --- */

static int env_combo_probe(struct i2c_client *client,
                           const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct env_combo_data *data;
	int ret;
	s32 whoami_id;

	whoami_id = i2c_smbus_read_byte_data(client, ENV_COMBO_REG_WHO_AM_I);
	if (whoami_id < 0 || whoami_id != ENV_COMBO_WHO_AM_I_VAL)
		return -ENODEV;

    /* Using devm_iio_device_alloc (resource managed) */
	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	data->indio_dev = indio_dev; 
	mutex_init(&data->lock);

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = "combo-driver";
	indio_dev->info = &env_combo_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = env_combo_channels;
	indio_dev->num_channels = ARRAY_SIZE(env_combo_channels);

	/* Call iio_kfifo_allocate with zero arguments (void) and assign the buffer pointer */
	indio_dev->buffer = iio_kfifo_allocate();
	if (IS_ERR(indio_dev->buffer)) {
        ret = PTR_ERR(indio_dev->buffer);
		return ret;
    }
    
    /* Using non-resource-managed iio_device_register */
    ret = iio_device_register(indio_dev);
	if (ret)
        /* Clean up the kfifo buffer by passing indio_dev->buffer */
        iio_kfifo_free(indio_dev->buffer); 

	return ret;
}

/* --- Remove Function (Required for non-devm cleanup) --- */

static int env_combo_remove(struct i2c_client *client)
{
    struct env_combo_data *data = i2c_get_clientdata(client);

    /* Unregister the IIO device first */
    iio_device_unregister(data->indio_dev);
    
    /* Clean up the kfifo buffer by passing data->indio_dev->buffer */
    iio_kfifo_free(data->indio_dev->buffer);

    return 0;
}


/* --- Driver Definition --- */

MODULE_DEVICE_TABLE(i2c, env_combo_ids);

static struct i2c_driver env_combo_driver = {
	.driver = { .name = "combo-driver", },
	.probe = env_combo_probe,
    .remove = env_combo_remove, 
	.id_table = env_combo_ids,
};

static int __init env_combo_init(void)
{
	return i2c_add_driver(&env_combo_driver);
}

static void __exit env_combo_exit(void)
{
	i2c_del_driver(&env_combo_driver);
}

module_init(env_combo_init);
module_exit(env_combo_exit);

MODULE_AUTHOR("Tester");
MODULE_DESCRIPTION("COMBO Sensor Driver");
MODULE_LICENSE("GPL");

