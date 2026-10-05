// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/slab.h>

#define NRF_CMD_TEMP			0x10
#define BRIDGE_POLL_INTERVAL_MS	1000

#define MAX31865_REG_RTD_MSB		0x01
#define MAX31865_REG_RTD_LSB		0x02

struct max31865_bridge {
    struct spi_device *max31865;
    struct spi_device *nrf;

    struct mutex lock;
    struct delayed_work work;

    u32 temp_mC;
    bool stopping;
};

/* ---------------- SPI REGISTER READ ---------------- */

static int max31865_read_reg(struct spi_device *spi, u8 reg)
{
	u8 tx[2];
	u8 rx[2];

	struct spi_transfer t = {
		.tx_buf = tx,
		.rx_buf = rx,
		.len = sizeof(tx),
	};

	int ret;

	/* READ = reg | 0x80 */
	tx[0] = reg | 0x80;
	tx[1] = 0x00;

	ret = spi_sync_transfer(spi, &t, 1);
	if (ret)
		return ret;

	return rx[1];
}

/* ---------------- READ RTD RAW ---------------- */

static int max31865_read_rtd(struct spi_device *spi)
{
	int msb, lsb;
	u16 raw;

	msb = max31865_read_reg(spi, MAX31865_REG_RTD_MSB);
	if (msb < 0)
		return msb;

	lsb = max31865_read_reg(spi, MAX31865_REG_RTD_LSB);
	if (lsb < 0)
		return lsb;

	raw = ((msb << 8) | lsb);

	/* MAX31865: bit0 = fault, remove it */
	raw >>= 1;

	return raw;
}

/* ---------------- CONVERT TO TEMPERATURE ---------------- */
/*
 * NOTE:
 * Real conversion requires Callendar–Van Dusen equation.
 * This is linear approximation for PT100 (for demo).
 */
static int rtd_to_mC(int rtd)
{
	/* Rough scaling: 0.03125°C per count (depends on config) */
	return (rtd * 3125) / 100;
}

/* ---------------- SEND TO NRF52840 ---------------- */

static int nrf_send_temp(struct spi_device *spi, u32 temp_mC)
{
	u8 tx[5];

	tx[0] = NRF_CMD_TEMP;
	put_unaligned_le32(temp_mC, &tx[1]);

	return spi_write(spi, tx, sizeof(tx));
}

/* ---------------- WORKER ---------------- */

static void bridge_work(struct work_struct *work)
{
	struct max31865_bridge *b =
		container_of(to_delayed_work(work),
			     struct max31865_bridge,
			     work);

	int rtd;
	u32 temp;

	mutex_lock(&b->lock);

	rtd = max31865_read_rtd(b->max31865);
	if (rtd >= 0) {
		temp = rtd_to_mC(rtd);
		b->temp_mC = temp;

		if (b->nrf)
			nrf_send_temp(b->nrf, temp);
	}

	mutex_unlock(&b->lock);

	schedule_delayed_work(&b->work,
			      msecs_to_jiffies(1000));
}

/* ---------------- SYSFS ---------------- */

static ssize_t temperature_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct max31865_bridge *b = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", b->temp_mC);
}

static DEVICE_ATTR_RO(temperature);

/* ---------------- HELPER TO FIND SPI DEVICE ---------------- */

static int match_cs(struct device *dev, void *data)
{
	struct spi_device *spi = to_spi_device(dev);
	u32 *cs = data;

	return spi->chip_select == *cs;
}

static struct spi_device *get_spi_device_by_cs(struct spi_controller *ctlr, u32 cs)
{
	struct device *dev;

	dev = device_find_child(&ctlr->dev, &cs, match_cs);
	if (!dev)
		return NULL;

	return to_spi_device(dev);
}

/* ---------------- PROBE ---------------- */

static int bridge_probe(struct spi_device *spi)
{
	struct max31865_bridge *b;
	int ret;

	b = devm_kzalloc(&spi->dev, sizeof(*b), GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	mutex_init(&b->lock);

	/* Assumes spi0.0 = MAX31865, spi0.1 = nRF */
	b->max31865 = spi;
	b->nrf      = get_spi_device_by_cs(spi->controller, 1);

	if (!b->nrf) {
		dev_err(&spi->dev, "Could not find nRF52840 SPI device (CS1)\n");
		return -ENODEV;
	}

	spi_set_drvdata(spi, b);

	ret = device_create_file(&spi->dev, &dev_attr_temperature);
	if (ret) {
		put_device(&b->nrf->dev);
		return ret;
	}

	INIT_DELAYED_WORK(&b->work, bridge_work);
	schedule_delayed_work(&b->work, msecs_to_jiffies(1000));

	dev_info(&spi->dev, "MAX31865 + nRF52840 bridge loaded\n");

	return 0;
}

/* ---------------- REMOVE ---------------- */

static void bridge_remove(struct spi_device *spi)
{
	struct max31865_bridge *b = spi_get_drvdata(spi);

	cancel_delayed_work_sync(&b->work);
	device_remove_file(&spi->dev, &dev_attr_temperature);

	if (b->nrf)
		put_device(&b->nrf->dev);
}

/* ---------------- DEVICE TREE MATCH ---------------- */

static const struct of_device_id bridge_dt[] = {
	{ .compatible = "rpi,max31865_nrf52840_bridge" },
	{}
};
MODULE_DEVICE_TABLE(of, bridge_dt);

/* ---------------- SPI DRIVER ---------------- */

static struct spi_driver bridge_driver = {
	.driver = {
		.name = "max31865_nrf_bridge",
		.of_match_table = bridge_dt,
	},
	.probe = bridge_probe,
	.remove = bridge_remove,
};

module_spi_driver(bridge_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MAX31865 + nRF52840 SPI Bridge Driver");

