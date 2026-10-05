#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

// IMPORTANT: Change this to match your target system's SPI device node
// Example: /dev/spidev1.0 (Bus 1, Chip Select 0)
static const char *device = "/dev/spidev1.0"; 

// Configuration variables
static __u8 mode = SPI_MODE_0;           // SPI Mode (0, 1, 2, or 3)
static __u8 bits = 8;                   // Bits per word (8 is standard)
static __u32 speed = 500000;            // Max speed in Hz (500kHz)

// Buffers for the transaction
static unsigned char tx_buffer[32];      // Transmit buffer
static unsigned char rx_buffer[32] = {0, }; // Receive buffer, initialized to zero

/**
 * @brief Prints a message and exits the program with an error status.
 * @param msg The error message string.
 */
static void pabort(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

/**
 * @brief Performs a read/write transaction and prints the results.
 * @param fd The file descriptor for the SPI device.
 * @param len The length of the data to transfer.
 * @return 0 on success, -1 on failure.
 */
static int transfer(int fd, int len)
{
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buffer, // Pointer to transmit buffer
        .rx_buf = (unsigned long)rx_buffer, // Pointer to receive buffer
        .len = len,                         // Length of the transfer
        .speed_hz = speed,                  // Speed override (can be 0 to use default)
        .delay_usecs = 0,                   // Delay after transfer (in microseconds)
        .bits_per_word = bits,              // Bits per word override
    };

    // Use the IOCTL command to initiate the transfer
    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 1)
        pabort("Error: Can't send SPI message");

    // Print the results of the transaction
    printf("TX (%d bytes): ", len);
    for (int i = 0; i < len; i++) {
        printf("%02X ", tx_buffer[i]);
    }
    printf("\n");
    
    printf("RX (%d bytes): ", len);
    for (int i = 0; i < len; i++) {
        printf("%02X ", rx_buffer[i]);
    }
    printf("\n");

    return ret;
}


int main(int argc, char *argv[])
{
    int fd;
    int ret = 0;

    // --- 1. Open the SPI device file ---
    fd = open(device, O_RDWR);
    if (fd < 0)
        pabort("Error: Can't open SPI device");

    // --- 2. Configure the SPI Bus using IOCTLs ---
    
    // Set SPI Mode (CPOL, CPHA)
    ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
    if (ret == -1) pabort("Error: Can't set SPI mode");

    ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
    if (ret == -1) pabort("Error: Can't get SPI mode");

    // Set Bits per Word
    ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    if (ret == -1) pabort("Error: Can't set bits per word");

    ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
    if (ret == -1) pabort("Error: Can't get bits per word");

    // Set Max Speed
    ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    if (ret == -1) pabort("Error: Can't set max speed HZ");

    ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
    if (ret == -1) pabort("Error: Can't get max speed HZ");

    printf("SPI configuration successful:\n");
    printf("  Device: %s\n", device);
    printf("  Mode: %d\n", mode);
    printf("  Bits per word: %d\n", bits);
    printf("  Max speed: %d Hz (%d KHz)\n", speed, speed / 1000);
    printf("----------------------------------------\n");

    // --- 3. Prepare Data and Execute Transfer ---
    
    // Example: A common command sequence (e.g., Read ID Register)
    // Send 0x9F (Read JEDEC ID command) followed by three dummy bytes (0x00)
    tx_buffer[0] = 0x9F; 
    tx_buffer[1] = 0x00;
    tx_buffer[2] = 0x00;
    tx_buffer[3] = 0x00;

    // Perform a 4-byte transfer
    transfer(fd, 4);

    // --- 4. Cleanup ---
    close(fd);

    return ret;
}
