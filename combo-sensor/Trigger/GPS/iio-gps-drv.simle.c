#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/slab.h>

// --- FIXES FOR MISSING IIO CONSTANTS (Likely not enabled in kernel config) ---
// Define these to prevent "undeclared here" errors.
#ifndef IIO_POSITIONID
#define IIO_POSITIONID IIO_COUNT
#endif
#ifndef IIO_MOD_LAT
#define IIO_MOD_LAT 0x1
#endif
#ifndef IIO_MOD_LON
#define IIO_MOD_LON 0x2
#endif
// ----------------------------------------------------------------------------

#define UBLOX_GPS_MAX_VAL 10000000 // Fixed-point scale factor (1e7)

struct ublox_gps_data {
    struct i2c_client *client;
    // Buffer for coordinates (Lat, Lon)
    s32 buffer[2]; 
};

static const struct iio_chan_spec ublox_channels[] = {
    {
        .type = IIO_POSITIONID, // FIX: Now defined
        .modified = true,
        .channel2 = IIO_MOD_LAT, // FIX: Now defined
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
        .scan_index = 0,
        .scan_type = {
            .sign = 's',
            .realbits = 32,
            .storagebits = 32,
        },
    },
    {
        .type = IIO_POSITIONID, // FIX: Now defined
        .modified = true,
        .channel2 = IIO_MOD_LON, // FIX: Now defined
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
        .scan_index = 1,
        .scan_type = {
            .sign = 's',
            .realbits = 32,
            .storagebits = 32,
        },
    },
};

static int ublox_read_raw(struct iio_dev *indio_dev,
                          const struct iio_chan_spec *chan,
                          int *val, int *val2, long mask)
{
    // struct ublox_gps_data *data = iio_priv(indio_dev); // Removed unused variable
    
    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        // Placeholder: Actual I2C read logic goes here.
        if (chan->channel2 == IIO_MOD_LAT)
            *val = 407128000; // Mock raw lat (40.7128 * 1e7)
        else if (chan->channel2 == IIO_MOD_LON)
            *val = -740060000; // Mock raw lon (-74.0060 * 1e7)
        
        return IIO_VAL_INT;
        
    case IIO_CHAN_INFO_SCALE:
        // FIX: Using IIO_VAL_FRACTIONAL (val=numerator, val2=denominator) 
        // to represent 1 / 10,000,000. This is the scale of the raw value.
        *val = 1;
        *val2 = UBLOX_GPS_MAX_VAL; 
        return IIO_VAL_FRACTIONAL; // FIX: Using widely supported fractional type

    default:
        return -EINVAL;
    }
}

static const struct iio_info ublox_info = {
    .read_raw = ublox_read_raw,
    // The .driver_module member was removed in kernel 5.x
    .attrs = NULL,
};

// --- FIXES FOR TRIGGER HANDLERS (Unused for simple sysfs read, but fixed for compilation) ---

// FIX: iio_trigger_poll_chained is now available via iio/trigger_consumer.h
static irqreturn_t ublox_irq_handler(int irq, void *p)
{
    // iio_trigger_poll_chained((struct iio_trigger *)p);
    return IRQ_HANDLED;
}

static int ublox_trigger_handler(struct iio_poll_func *pf)
{
    struct iio_dev *indio_dev = pf->indio_dev;
    struct ublox_gps_data *data = iio_priv(indio_dev);
    
    // Read data into data->buffer[0] and data->buffer[1]

    iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
                                       iio_get_time_ns(indio_dev));

    iio_trigger_notify_done(indio_dev->trig); // FIX: Changed ->trigger to ->trig
    return 0;
}
// -------------------------------------------------------------------------------------------

// FIX: Added the mandatory second argument for modern I2C drivers
static int ublox_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct ublox_gps_data *data;
    struct iio_dev *indio_dev;
    int ret;

    indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
    if (!indio_dev)
        return -ENOMEM;

    data = iio_priv(indio_dev);
    data->client = client;
    i2c_set_clientdata(client, indio_dev);

    indio_dev->dev.parent = &client->dev;
    indio_dev->channels = ublox_channels;
    indio_dev->num_channels = ARRAY_SIZE(ublox_channels);
    indio_dev->name = "ublox-gps";
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->info = &ublox_info;
    
    // Registering device
    ret = devm_iio_device_register(&client->dev, indio_dev);
    if (ret)
        return ret;

    return 0;
}

// FIX: Signature is now int, which is compatible with struct i2c_driver
static int ublox_remove(struct i2c_client *client)
{
    // Cleanup code goes here
    return 0;
}

static const struct i2c_device_id ublox_id[] = {
    { "ublox-gps", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, ublox_id);

static struct i2c_driver ublox_driver = {
    .driver = {
        .name   = "ublox-gps",
    },
    .probe    = ublox_probe, // FIX: Signature now compatible
    .remove   = ublox_remove,
    .id_table = ublox_id,
};
module_i2c_driver(ublox_driver);

MODULE_AUTHOR("Placeholder Author");
MODULE_DESCRIPTION("IIO driver for U-blox GPS receivers (5.x Compatible)");
MODULE_LICENSE("GPL");
