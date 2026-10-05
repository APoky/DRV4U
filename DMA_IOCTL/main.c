#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#define DEVICE_PATH "/dev/mydma"
#define BUF_SIZE    (4096 * 4)  // Must match driver

/* Must match driver ioctl */
#define IOCTL_START_STREAM _IO('q', 1)

void process_frame(uint8_t *buf, size_t size) {
    /* Example: just print first 8 bytes of the frame */
    printf("Frame data (first 8 bytes): ");
    for (int i = 0; i < 8 && i < size; i++)
        printf("%02x ", buf[i]);
    printf("\n");
}

int main(void) {
    int fd;
    uint8_t *dma_buf;

    /* 1. Open the character device */
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    /* 2. mmap the DMA buffer */
    dma_buf = mmap(NULL, BUF_SIZE,
                   PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, 0);
    if (dma_buf == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("DMA buffer mapped at %p\n", dma_buf);

    /* 3. Start the DMA stream */
    if (ioctl(fd, IOCTL_START_STREAM) < 0) {
        perror("ioctl START_STREAM");
        munmap(dma_buf, BUF_SIZE);
        close(fd);
        return EXIT_FAILURE;
    }

    printf("DMA streaming started\n");

    /* 4. Main loop: wait for frames and process them */
    for (int frame = 0; frame < 10; frame++) {
        /* read() waits for frame_ready (interrupt) */
        if (read(fd, NULL, 0) < 0) {
            perror("read");
            break;
        }

        /* Directly access the DMA buffer */
        process_frame(dma_buf, BUF_SIZE);
    }

    /* 5. Cleanup */
    munmap(dma_buf, BUF_SIZE);
    close(fd);

    printf("DMA streaming finished\n");
    return EXIT_SUCCESS;
}
