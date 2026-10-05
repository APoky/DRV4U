#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#define IIO_DEVICE_NAME "ublox-gps"
#define IIO_DEV_DIR "/sys/bus/iio/devices"
#define SAMPLE_SIZE 16 // Assuming 4 bytes (Lat) + 4 bytes (Lon) + 8 bytes (Timestamp)

/**
 * @brief Utility function to write a value to a sysfs attribute file.
 * @param path The full path to the sysfs file.
 * @param val The string value to write.
 * @return 0 on success, -1 on failure.
 */
static int write_sysfs_str(const char *path, const char *val)
{
    int fd;
    ssize_t len = strlen(val);
    
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening %s: %s\n", path, strerror(errno));
        return -1;
    }

    if (write(fd, val, len) != len) {
        fprintf(stderr, "Error writing to %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/**
 * @brief Finds the sysfs path to the IIO device by name.
 * @param name The expected IIO device name (e.g., "ublox-gps").
 * @param path_out Buffer to store the resulting path.
 * @param path_len Maximum length of path_out.
 * @return 0 on success, -1 on failure.
 */
static int find_iio_device(const char *name, char *path_out, size_t path_len)
{
    DIR *dir;
    struct dirent *entry;
    char path_name[256];
    char actual_name[64];
    int fd, ret = -1;

    dir = opendir(IIO_DEV_DIR);
    if (!dir) {
        fprintf(stderr, "Cannot open %s directory: %s\n", IIO_DEV_DIR, strerror(errno));
        return -1;
    }

    // Iterate through all iio:deviceX entries
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "iio:device", 10) == 0) {
            snprintf(path_name, sizeof(path_name), "%s/%s/name", IIO_DEV_DIR, entry->d_name);

            fd = open(path_name, O_RDONLY);
            if (fd >= 0) {
                ssize_t bytes = read(fd, actual_name, sizeof(actual_name) - 1);
                close(fd);

                if (bytes > 0) {
                    actual_name[bytes - 1] = '\0'; // Remove trailing newline
                    if (strcmp(actual_name, name) == 0) {
                        snprintf(path_out, path_len, "%s/%s", IIO_DEV_DIR, entry->d_name);
                        ret = 0; // Device found
                        break;
                    }
                }
            }
        }
    }

    closedir(dir);
    if (ret != 0) {
        fprintf(stderr, "IIO device '%s' not found in %s.\n", name, IIO_DEV_DIR);
    }
    return ret;
}

int main(int argc, char *argv[])
{
    char dev_sysfs_path[256];
    char dev_iio_path[256];
    char sysfs_file[512];
    int dev_fd = -1;
    s32 raw_lat, raw_lon;
    s64 timestamp;
    unsigned char buffer[SAMPLE_SIZE];
    int ret = 0;

    printf("Starting IIO GPS Reader for device '%s'\n", IIO_DEVICE_NAME);

    // 1. Find the IIO device by name
    if (find_iio_device(IIO_DEVICE_NAME, dev_sysfs_path, sizeof(dev_sysfs_path)) != 0) {
        return EXIT_FAILURE;
    }
    printf("Found device at sysfs path: %s\n", dev_sysfs_path);
    
    // Construct the device node path (e.g., /dev/iio:device0)
    snprintf(dev_iio_path, sizeof(dev_iio_path), "/dev/%s", strrchr(dev_sysfs_path, '/') + 1);

    // 2. Enable scan elements (Latitude and Longitude)
    printf("Configuring channels...\n");

    // Enable Latitude (channel index 0)
    snprintf(sysfs_file, sizeof(sysfs_file), "%s/scan_elements/in_positionid_lat_en", dev_sysfs_path);
    if (write_sysfs_str(sysfs_file, "1") != 0) goto cleanup;

    // Enable Longitude (channel index 1)
    snprintf(sysfs_file, sizeof(sysfs_file), "%s/scan_elements/in_positionid_lon_en", dev_sysfs_path);
    if (write_sysfs_str(sysfs_file, "1") != 0) goto cleanup;
    
    // 3. Set buffer length (optional, but good practice)
    snprintf(sysfs_file, sizeof(sysfs_file), "%s/buffer/length", dev_sysfs_path);
    if (write_sysfs_str(sysfs_file, "16") != 0) goto cleanup; // Set buffer to hold 16 samples

    // 4. Enable the buffer
    printf("Enabling buffer...\n");
    snprintf(sysfs_file, sizeof(sysfs_file), "%s/buffer/enable", dev_sysfs_path);
    if (write_sysfs_str(sysfs_file, "1") != 0) goto cleanup;

    // 5. Open the IIO device file for reading buffered data
    dev_fd = open(dev_iio_path, O_RDONLY);
    if (dev_fd < 0) {
        fprintf(stderr, "Error opening IIO device node %s: %s\n", dev_iio_path, strerror(errno));
        ret = -1;
        goto cleanup;
    }

    printf("Reading data from %s (Press Ctrl+C to stop)...\n", dev_iio_path);
    printf("----------------------------------------------------------------------\n");

    // 6. Main read loop
    while (1) {
        ssize_t bytes_read = read(dev_fd, buffer, SAMPLE_SIZE);
        
        if (bytes_read == -1) {
            if (errno == EINTR) continue;
            fprintf(stderr, "Error reading from device: %s\n", strerror(errno));
            break;
        }

        if (bytes_read != SAMPLE_SIZE) {
            fprintf(stderr, "Warning: Expected %d bytes, got %zd. Data corruption or driver bug.\n", SAMPLE_SIZE, bytes_read);
            continue;
        }

        // Parse data: Lat is first 4 bytes, Lon is next 4 bytes, Timestamp is last 8 bytes
        memcpy(&raw_lat, buffer, sizeof(raw_lat));
        memcpy(&raw_lon, buffer + sizeof(raw_lat), sizeof(raw_lon));
        memcpy(&timestamp, buffer + sizeof(raw_lat) + sizeof(raw_lon), sizeof(timestamp));

        // Scale factor is 10,000,000 (defined in kernel module)
        double lat_deg = (double)raw_lat / 10000000.0;
        double lon_deg = (double)raw_lon / 10000000.0;

        printf("Lat: %.7f | Lon: %.7f | Timestamp: %lld ns\n", lat_deg, lon_deg, timestamp);

        usleep(500000); // Wait 500ms before next read
    }


cleanup:
    printf("\nCleaning up and disabling buffer.\n");

    if (dev_fd >= 0) {
        close(dev_fd);
    }

    // 7. Disable the buffer (clean up)
    snprintf(sysfs_file, sizeof(sysfs_file), "%s/buffer/enable", dev_sysfs_path);
    if (write_sysfs_str(sysfs_file, "0") != 0) {
        fprintf(stderr, "Warning: Failed to disable buffer.\n");
    }

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
