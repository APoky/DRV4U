#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

/*
 * This custom driver is designed to bind to a specific peripheral on the SPI bus.
 * It is NOT the "spidev" driver that exposes /dev/spidevX.Y.
 */

#define DRIVER_NAME "acme_spi_peripheral"
#define ACME_TRANSFER_SIZE 4

// Function to perform a simple self-test transaction
static int spi_peripheral_transfer_test(struct spi_device *spi)
{
	int ret;
	u8 tx_buf[ACME_TRANSFER_SIZE] = { 0xAA, 0xBB, 0xCC, 0xDD }; // Data to send
	u8 rx_buf[ACME_TRANSFER_SIZE] = { 0 }; // Buffer for received data

	dev_info(&spi->dev, "Starting SPI self-test transfer...\n");
	
	// Perform a synchronous write and read operation
	ret = spi_write_then_read(spi, tx_buf, ACME_TRANSFER_SIZE, 
				  rx_buf, ACME_TRANSFER_SIZE);

	if (ret) {
		dev_err(&spi->dev, "SPI transfer failed (error: %d)\n", ret);
		return ret;
	}

	dev_info(&spi->dev, "TX: 0x%02x 0x%02x 0x%02x 0x%02x\n",
		 tx_buf[0], tx_buf[1], tx_buf[2], tx_buf[3]);
	dev_info(&spi->dev, "RX: 0x%02x 0x%02x 0x%02x 0x%02x\n",
		 rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);
	
	dev_info(&spi->dev, "SPI self-test transfer complete.\n");
	return 0;
}


static int spi_peripheral_probe(struct spi_device *spi)
{
	int ret;
	dev_info(&spi->dev, "Acme SPI peripheral driver initialized.\n");

	// --- 1. Configure Bus Parameters ---
	// Set 8 bits per word and 500kHz speed
	spi->bits_per_word = 8;
	spi->max_speed_hz = 500000;
	
	// The spi_setup() call pushes these parameters to the controller
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "Failed to set up SPI bus: %d\n", ret);
		return ret;
	}
	dev_info(&spi->dev, "Bus configured successfully (Speed: %d Hz, Mode: %d).\n", 
			spi->max_speed_hz, spi->mode);

	// --- 2. Run Self-Test ---
	ret = spi_peripheral_transfer_test(spi);
	if (ret) {
		dev_err(&spi->dev, "Self-test failed!\n");
	}

	return 0;
}

// NOTE: The remove function must return void in kernel 5.15
static void spi_peripheral_remove(struct spi_device *spi)
{
	dev_info(&spi->dev, "Acme SPI peripheral driver removed.\n");
}

// Define the Device Tree compatible string used for matching the driver to hardware
static const struct of_device_id spi_peripheral_of_match[] = {
	{ .compatible = "acme,spi-peripheral" }, // Match node in DTS
	{ }
};
MODULE_DEVICE_TABLE(of, spi_peripheral_of_match);

// Define the SPI driver structure
static struct spi_driver spi_peripheral_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = spi_peripheral_of_match,
	},
	.probe = spi_peripheral_probe,
	.remove = spi_peripheral_remove,
};

module_spi_driver(spi_peripheral_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gemini");
MODULE_DESCRIPTION("Acme Test SPI Peripheral Driver");
