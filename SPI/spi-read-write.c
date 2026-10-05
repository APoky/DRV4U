#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <string.h>

#define SPI_DEVICE "/dev/spidev0.0"

// EEPROM Commands (consult your EEPROM datasheet)
#define EEPROM_CMD_READ  0x03
#define EEPROM_CMD_WRITE 0x02
#define EEPROM_CMD_WREN  0x06 // Write Enable

/**
 * @brief Prints the usage information for the program.
 */
void print_usage() {
    printf("Usage: spi-read-write <read|write> <address> <length> [data_to_write...]\n");
    printf("  <read|write>    : Operation to perform.\n");
    printf("  <address>       : The 16-bit memory address (e.g., 0x0100).\n");
    printf("  <length>        : Number of bytes to read or write.\n");
    printf("  [data_to_write] : For write operations, the bytes to write (in hex, e.g., 0xAA 0xBB).\n");
}

/**
 * @brief Performs a full-duplex SPI transfer.
 * @param fd File descriptor for the SPI device.
 * @param tx_buffer The data to transmit.
 * @param rx_buffer The buffer to store received data.
 * @param len The length of the transfer.
 * @return 0 on success, -1 on failure.
 */
int spi_transfer(int fd, uint8_t *tx_buffer, uint8_t *rx_buffer, int len) {
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buffer,
        .rx_buf = (unsigned long)rx_buffer,
        .len = len,
        .speed_hz = 500000,
        .bits_per_word = 8,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("ioctl");
        return -1;
    }
    return 0;
}

/**
 * @brief Reads 'n' bytes from the EEPROM.
 * @param fd File descriptor for the SPI device.
 * @param address The starting address to read from.
 * @param data_buffer The buffer to store the read data.
 * @param length The number of bytes to read.
 * @return 0 on success, -1 on failure.
 */
int eeprom_read(int fd, uint16_t address, uint8_t *data_buffer, int length) {
    uint8_t tx_buf[3 + length];
    uint8_t rx_buf[3 + length];

    // 1. Prepare the command buffer
    tx_buf[0] = EEPROM_CMD_READ;
    tx_buf[1] = (address >> 8) & 0xFF; // High byte of address
    tx_buf[2] = address & 0xFF;        // Low byte of address

    printf("Reading %d bytes from address 0x%04X...\n", length, address);

    // 2. Perform the transfer
    if (spi_transfer(fd, tx_buf, rx_buf, 3 + length) != 0) {
        return -1;
    }

    // 3. Copy the received data into the output buffer
    memcpy(data_buffer, &rx_buf[3], length);

    return 0;
}

/**
 * @brief Writes 'n' bytes to the EEPROM.
 * @param fd File descriptor for the SPI device.
 * @param address The starting address to write to.
 * @param data_buffer The data to write.
 * @param length The number of bytes to write.
 * @return 0 on success, -1 on failure.
 */
int eeprom_write(int fd, uint16_t address, uint8_t *data_buffer, int length) {
    uint8_t tx_buf[3 + length];
    uint8_t wren_cmd = EEPROM_CMD_WREN;

    // 1. Send the Write Enable (WREN) command
    printf("Sending Write Enable (WREN) command...\n");
    if (write(fd, &wren_cmd, 1) != 1) {
        perror("write wren");
        return -1;
    }
    // Small delay to allow the EEPROM to process the WREN command
    usleep(1000);

    // 2. Prepare the write command buffer
    tx_buf[0] = EEPROM_CMD_WRITE;
    tx_buf[1] = (address >> 8) & 0xFF; // High byte of address
    tx_buf[2] = address & 0xFF;        // Low byte of address
    memcpy(&tx_buf[3], data_buffer, length);

    printf("Writing %d bytes to address 0x%04X...\n", length, address);

    // 3. Perform the write transfer
    if (write(fd, tx_buf, 3 + length) != (3 + length)) {
        perror("write data");
        return -1;
    }

    // 4. Wait for the write cycle to complete (consult datasheet!)
    // This is a blocking delay. A more advanced application might poll a status register.
    printf("Waiting for write cycle to complete (5ms)...\n");
    usleep(5000); // 5ms is a common write cycle time

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        print_usage();
        return 1;
    }

    const char *operation = argv[1];
    uint16_t address = strtol(argv[2], NULL, 16);
    int length = atoi(argv[3]);

    int fd = open(SPI_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Configure SPI
    uint8_t mode = SPI_MODE_0;
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) == -1) {
        perror("ioctl mode");
        close(fd);
        return 1;
    }

    if (strcmp(operation, "read") == 0) {
        uint8_t *read_data = malloc(length);
        if (!read_data) {
            perror("malloc");
            close(fd);
            return 1;
        }

        if (eeprom_read(fd, address, read_data, length) == 0) {
            printf("Read successful. Data:\n");
            for (int i = 0; i < length; i++) {
                printf("0x%02X ", read_data[i]);
            }
            printf("\n");
        }
        free(read_data);

    } else if (strcmp(operation, "write") == 0) {
        if (argc < 4 + length) {
            printf("Error: Not enough data bytes provided for write operation.\n");
            print_usage();
            close(fd);
            return 1;
        }

        uint8_t *write_data = malloc(length);
        if (!write_data) {
            perror("malloc");
            close(fd);
            return 1;
        }

        for (int i = 0; i < length; i++) {
            write_data[i] = strtol(argv[4 + i], NULL, 16);
        }

        if (eeprom_write(fd, address, write_data, length) == 0) {
            printf("Write successful.\n");
        }
        free(write_data);

    } else {
        printf("Error: Invalid operation '%s'.\n", operation);
        print_usage();
    }

    close(fd);
    return 0;
}
