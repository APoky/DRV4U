#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define DEVICE_PATH "/dev/dummy_dma"
#define DMA_BUFFER_SIZE 32
#define TEST_VALUE_SRC 0x42 // ASCII '*'
#define TEST_VALUE_DST 0x55 // ASCII 'U'

int main() {
    int fd;
    ssize_t bytes_written, bytes_read;
    // Source buffer initialized with a pattern
    char src_buffer[DMA_BUFFER_SIZE];
    // Destination buffer initialized with a different pattern
    char dst_buffer[DMA_BUFFER_SIZE];

    printf("--- DMA Driver Character Device Test ---\n");
    printf("Opening device file: %s\n", DEVICE_PATH);

    // 1. Open the device
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        fprintf(stderr, "Error: Is the dma-sim-drv-qemu.ko module loaded?\n");
        return EXIT_FAILURE;
    }

    // 2. Initialize buffers
    memset(src_buffer, TEST_VALUE_SRC, DMA_BUFFER_SIZE);
    memset(dst_buffer, TEST_VALUE_DST, DMA_BUFFER_SIZE);
    
    printf("Source buffer initialized with 0x%X pattern.\n", TEST_VALUE_SRC);
    printf("Destination buffer initialized with 0x%X pattern.\n", TEST_VALUE_DST);
    printf("Attempting to write %d bytes (triggers DMA transfer)...\n", DMA_BUFFER_SIZE);

    // 3. Write data (triggers the kernel's dummy_write -> workqueue -> completion)
    bytes_written = write(fd, src_buffer, DMA_BUFFER_SIZE);
    
    if (bytes_written != DMA_BUFFER_SIZE) {
        perror("Failed to write to device");
        fprintf(stderr, "Error: Only wrote %zd bytes. Check kernel logs (dmesg).\n", bytes_written);
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Write successful. Transfer simulation complete in kernel.\n");

    // 4. Read data (reads from the destination buffer)
    bytes_read = read(fd, dst_buffer, DMA_BUFFER_SIZE);
    
    if (bytes_read != DMA_BUFFER_SIZE) {
        perror("Failed to read from device");
        fprintf(stderr, "Error: Only read %zd bytes.\n", bytes_read);
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Read successful. Verifying buffers...\n");

    // 5. Verify data
    if (memcmp(src_buffer, dst_buffer, DMA_BUFFER_SIZE) == 0) {
        printf("\nTEST SUCCESS: Source and Destination buffers match!\n");
        printf("The DMA driver successfully completed the transfer simulation.\n");
        close(fd);
        return EXIT_SUCCESS;
    } else {
        printf("\nTEST FAILURE: Source and Destination buffers mismatch.\n");
        fprintf(stderr, "Data corruption detected. Check kernel logic.\n");
        close(fd);
        return EXIT_FAILURE;
    }
}

