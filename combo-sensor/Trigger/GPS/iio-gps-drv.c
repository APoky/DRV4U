#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h> // REQUIRED for devm_iio_kfifo_buffer_setup
// #include <linux/iio/poll.h> // REMOVED: File not found in 5.15.179 headers.
#include <linux/slab.h>
#include <linux/of.h> 
#include <linux/byteorder/generic.h> 

// --- Mandatory IIO Constants (Needed if kernel config is minimal) ---
#ifndef IIO_POSITIONID
#define IIO_POSITIONID IIO_COUNT
#endif
#ifndef IIO_MOD_LAT
#define IIO_MOD_LAT 0x1
#endif
#ifndef IIO_MOD_LON
#define IIO_MOD_LON 0x2
#endif
// ------------------------------------------------------------------

// FIX: Explicitly declare the missing IIO poll function prototypes since 
// <linux/iio/poll.h> is unavailable and other headers don't define them.
extern struct iio_poll_func *iio_pollfunc_alloc(irqreturn_t (*h)(int irq, void *p),
						const char *name, struct iio_dev *indio_dev,
						const char *dev_name);
extern int iio_pollfunc_register(struct iio_poll_func *pf);
extern void iio_pollfunc_unregister(struct iio_poll_func *pf);
extern void iio_pollfunc_free(struct iio_poll_func *pf);


#define UBLOX_GPS_MAX_VAL 10000000 // Fixed-point scale factor (1e7)
#define UBLOX_REG_LAT_LON_START 0x50 // MOCK: Register where 2x s32 coordinates start
#define UBLOX_DATA_LEN 8 // 2 * 4 bytes (s32 lat, s32 lon)

struct ublox_gps_data {
    struct i2c_client *client;
    struct iio_trigger *trig; 
    int irq; 
    
    // Buffer for coordinates (Lat, Lon) used by triggered mode
    s32 buffer[2]; 
};

/**
 * @brief Reads raw data (2x s32: Lat, Lon) from the U-blox device via I2C.
 */
static int ublox_read_data(struct ublox_gps_data *data, s32 *lat, s32 *lon)
{
    int ret;
    u8 tx_buf[1] = { UBLOX_REG_LAT_LON_START }; // Register to start reading from
    u8 rx_buf[UBLOX_DATA_LEN];
    struct i2c_msg msgs[] = {
        {
            .addr = data->client->addr,
            .flags = 0, // Write flag
            .len = 1,
            .buf = tx_buf,
        },
        {
            .addr = data->client->addr,
            .flags = I2C_M_RD, // Read flag
            .len = UBLOX_DATA_LEN,
            .buf = rx_buf,
        },
    };
    
    ret = i2c_transfer(data->client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret != ARRAY_SIZE(msgs)) {
        dev_err(&data->client->dev, "I2C transfer failed: %d\n", ret);
        return ret < 0 ? ret : -EIO;
    }
    
    // Convert Big Endian data (from GPS module) to CPU native format
    *lat = (s32)be32_to_cpu(*((__be32 *)&rx_buf[0])); 
    *lon = (s32)be32_to_cpu(*((__be32 *)&rx_buf[4])); 
    
    return 0; // Success
}
// ---------------------------------------

static const struct iio_chan_spec ublox_channels[] = {
    {
        .type = IIO_POSITIONID,
        .modified = true,
        .channel2 = IIO_MOD_LAT,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
        .scan_index = 0,
        .scan_type = { .sign = 's', .realbits = 32, .storagebits = 32, },
    },
    {
        .type = IIO_POSITIONID,
        .modified = true,
        .channel2 = IIO_MOD_LON,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
        .scan_index = 1,
        .scan_type = { .sign = 's', .realbits = 32, .storagebits = 32, },
    },
};

// Handler for direct sysfs reads
static int ublox_read_raw(struct iio_dev *indio_dev,
                          const struct iio_chan_spec *chan,
                          int *val, int *val2, long mask)
{
    struct ublox_gps_data *data = iio_priv(indio_dev);
    s32 lat, lon;
    int ret;
    
    // 1. Read data from hardware 
    ret = ublox_read_data(data, &lat, &lon);
    if (ret)
        return ret;
        
    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        if (chan->channel2 == IIO_MOD_LAT)
            *val = lat;
        else if (chan->channel2 == IIO_MOD_LON)
            *val = lon;
        
        return IIO_VAL_INT;
        
    case IIO_CHAN_INFO_SCALE:
        // Scale is 1/10,000,000 (0.0000001)
        *val = 1;
        *val2 = UBLOX_GPS_MAX_VAL; 
        return IIO_VAL_FRACTIONAL;

    default:
        return -EINVAL;
    }
}

static const struct iio_info ublox_info = {
    .read_raw = ublox_read_raw,
    .attrs = NULL,
};

// Trigger handler: Called when the IIO trigger is polled
// Note: This signature is standard for trigger handlers (pollfuncs)
static irqreturn_t ublox_trigger_handler(int irq, void *private_data)
{
    struct iio_poll_func *pf = private_data;
    struct iio_dev *indio_dev = pf->indio_dev;
    struct ublox_gps_data *data = iio_priv(indio_dev);
    s32 lat, lon;
    
    // 1. Read the actual data from the device using I2C
    if (ublox_read_data(data, &lat, &lon))
        goto out;

    // 2. Load the data into the buffer
    data->buffer[0] = lat;
    data->buffer[1] = lon;

    // 3. Push the buffer data and the timestamp to user space
    iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
                                       iio_get_time_ns(indio_dev));

out:
    // Notify the trigger system that processing is complete
    iio_trigger_notify_done(indio_dev->trig); 
    return IRQ_HANDLED;
}

// --- IRQ Handler (Called when the hardware IRQ fires) ---
// Private data (p) is set to data->trig in devm_request_irq
static irqreturn_t ublox_irq_handler(int irq, void *p)
{
    // If the IRQ line fires, tell the IIO trigger to poll the data.
    iio_trigger_poll_chained((struct iio_trigger *)p);
    return IRQ_HANDLED;
}

// Handler for enabling/disabling the device's interrupt line
static int ublox_trigger_set_state(struct iio_trigger *trig, bool state)
{
    struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
    struct ublox_gps_data *data = iio_priv(indio_dev);

    // TODO: WRITE to a U-blox register here to enable/disable the interrupt pin.
    dev_info(&data->client->dev, "GPS IRQ state set to %d\n", state);
    
    return 0;
}

static const struct iio_trigger_ops ublox_trigger_ops = {
    .set_trigger_state = ublox_trigger_set_state,
};

// --- Buffer Functions for Manual Setup ---

// NOTE: Pre-enable hook removed due to missing 'iio_get_channel_type' and 'scan_index' errors.

static int ublox_buffer_postenable(struct iio_dev *indio_dev)
{
    struct ublox_gps_data *data = iio_priv(indio_dev);
    
    // Explicitly call the trigger operation, bypassing the missing iio_trigger_enable wrapper.
    if (data->trig && data->trig->ops->set_trigger_state)
        return data->trig->ops->set_trigger_state(data->trig, true);

    return 0;
}

static int ublox_buffer_predisable(struct iio_dev *indio_dev)
{
    struct ublox_gps_data *data = iio_priv(indio_dev);
    
    // Explicitly call the trigger operation, bypassing the missing iio_trigger_disable wrapper.
    if (data->trig && data->trig->ops->set_trigger_state)
        data->trig->ops->set_trigger_state(data->trig, false);
        
    return 0;
}

static const struct iio_buffer_setup_ops ublox_buffer_setup_ops = {
    // .preenable = ublox_buffer_preenable, // Removed to fix scan_index error
    .postenable = ublox_buffer_postenable,
    .predisable = ublox_buffer_predisable,
};

static int ublox_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct ublox_gps_data *data;
    struct iio_dev *indio_dev;
    int ret;

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->dev, "I2C adapter does not support I2C_FUNC_I2C\n");
        return -EOPNOTSUPP;
    }

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
    indio_dev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_TRIGGERED;
    indio_dev->info = &ublox_info;

    // ----------------------------------------------------------------------
    // --- MANUAL BUFFER AND TRIGGER SETUP ---
    // ----------------------------------------------------------------------
    
    data->irq = client->irq;

    // 1. Setup the IIO kfifo buffer structure
    // FIX: Pass 0 for mode flags and the ADDRESS (&) of the ops struct.
    ret = devm_iio_kfifo_buffer_setup(&client->dev, indio_dev, 
                                      0, // mode flags (0 or INDIO_BUFFER_TRIGGERED, but usually 0 here)
                                      &ublox_buffer_setup_ops); // FIX: Pass pointer to the struct
    if (ret) {
        dev_err(&client->dev, "Failed to setup kfifo buffer: %d\n", ret);
        return ret;
    }

    if (data->irq <= 0) {
        dev_warn(&client->dev, "No valid IRQ found. Polling mode only.\n");
    } else {
        // 2. Allocate and initialize IIO Trigger
        data->trig = devm_iio_trigger_alloc(&client->dev, "%s-trigger", indio_dev->name);
        if (!data->trig)
            return -ENOMEM;

        iio_trigger_set_drvdata(data->trig, indio_dev);
        data->trig->ops = &ublox_trigger_ops;
        data->trig->dev.parent = &client->dev;
        
        // 3. Request the IRQ line and link it to the IIO Trigger handler
        // The private data 'p' for ublox_irq_handler is the trigger object itself.
        ret = devm_request_irq(&client->dev, data->irq, ublox_irq_handler,
                               IRQF_TRIGGER_RISING | IRQF_SHARED, // Check your DTS for proper flags
                               indio_dev->name, data->trig);
        if (ret) {
            dev_err(&client->dev, "Failed to request IRQ %d: %d\n", data->irq, ret);
            return ret;
        }

        ret = devm_iio_trigger_register(&client->dev, data->trig);
        if (ret)
            return ret;
            
        // 4. Set the device's trigger pointer
        indio_dev->trig = data->trig;
        
        // 5. Connect the trigger to the buffer's polling function.
        // We use extern declarations above to avoid the missing <linux/iio/poll.h> header.
        indio_dev->pollfunc = iio_pollfunc_alloc(ublox_trigger_handler, 
                                                indio_dev->name, 
                                                indio_dev, // indio_dev pointer
                                                NULL);
        if (!indio_dev->pollfunc)
            return -ENOMEM;
            
        ret = iio_pollfunc_register(indio_dev->pollfunc);
        if (ret) {
            iio_pollfunc_free(indio_dev->pollfunc); // Free on failure
            return ret;
        }
    }
    // ----------------------------------------------------------------------

    ret = devm_iio_device_register(&client->dev, indio_dev);
    if (ret)
        return ret;

    dev_info(&client->dev, "U-blox IIO GPS driver registered (IRQ: %d)\n", data->irq);

    return 0;
}

static int ublox_remove(struct i2c_client *client)
{
    struct iio_dev *indio_dev = i2c_get_clientdata(client);

    // Manually clean up the pollfunc allocated via iio_pollfunc_alloc
    if (indio_dev && indio_dev->pollfunc) {
        iio_pollfunc_unregister(indio_dev->pollfunc);
        iio_pollfunc_free(indio_dev->pollfunc);
    }
    
    // Un-registering the iio device automatically cleans up buffers and triggers due to devm_
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
    .probe    = ublox_probe,
    .remove   = ublox_remove,
    .id_table = ublox_id,
};
module_i2c_driver(ublox_driver);

MODULE_AUTHOR("Placeholder Author");
MODULE_DESCRIPTION("IIO driver for U-blox GPS receivers (Fixed 5.15)");
MODULE_LICENSE("GPL");
