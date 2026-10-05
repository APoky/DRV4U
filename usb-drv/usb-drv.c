#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/completion.h>

// --- Driver Constants ---
#define ML_VENDOR_ID    0x1130 // Placeholder VID (Common for Dream Cheeky/similar)
#define ML_PRODUCT_ID   0x0202 // Placeholder PID
#define MINOR_BASE      0
#define MINOR_COUNT     1
#define DEVICE_NAME     "ml0"
#define CLASS_NAME      "missile_launcher"

// --- Device Command Mapping (Matches user space code) ---
#define ML_STOP     	0x00
#define ML_UP       	0x01
#define ML_DOWN     	0x02
#define ML_LEFT    	    0x04
#define ML_RIGHT    	0x08
#define ML_FIRE     	0x10

// --- Data structure for the device ---
struct ml_dev {
    struct usb_device *usb_dev;     // Pointer to the USB device
    struct usb_interface *interface; // Pointer to the USB interface
    struct cdev cdev;               // Character device structure
    dev_t dev_num;                  // Device major/minor number
    struct class *dev_class;        // Device class
};

static struct ml_dev *ml_device;

// --- USB Device ID Table ---
static const struct usb_device_id ml_id_table[] = {
    // Replace with your device's actual VID/PID if different
    { USB_DEVICE(ML_VENDOR_ID, ML_PRODUCT_ID) },
    { } /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, ml_id_table);

// --- USB Control Message Send Function ---
/**
 * @brief Sends a command byte to the USB device via a control transfer.
 * @param dev The ml_dev structure for the device.
 * @param command The single byte command received from user space.
 * @return 0 on success, negative error code on failure.
 */
static int ml_send_cmd(struct ml_dev *dev, unsigned char command)
{
    int ret;
    int actual_len;
    // Standard 8-byte control message data used by many USB desktop devices.
    // The command byte is typically placed in the second byte (index 1).
    unsigned char *buf = kzalloc(8, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    // The command is the second byte of the 8-byte payload.
    buf[1] = command; 

    // The USB control message parameters are vendor-specific.
    // This uses a common pattern:
    // bmRequestType: USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE
    // bRequest: 0x01 (Common vendor command)
    // wValue: 0x00
    // wIndex: Interface number (to target the correct interface)
    ret = usb_control_msg(
        dev->usb_dev,                                       // usb_device
        usb_sndctrlpipe(dev->usb_dev, 0),                   // pipe (control OUT)
        0x01,                                               // bRequest (vendor specific)
        (USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT), // bmRequestType
        0x00,                                               // wValue
        dev->interface->cur_altsetting->desc.bInterfaceNumber, // wIndex (interface number)
        buf,                                                // data buffer
        8,                                                  // data length (8 bytes)
        HZ * 5                                              // timeout (5 seconds)
    );

    if (ret < 0) {
        // Log the error returned by the USB core
        dev_err(&dev->interface->dev, "usb_control_msg failed with error %d\n", ret);
    } else {
        actual_len = ret;
        // Check if the command was ML_FIRE, which often requires a second packet
        if (command & ML_FIRE) {
             // In some launchers, FIRE requires a second packet (usually STOP) to be fully registered.
             // However, the user-space code simply sends the FIRE command and sleeps.
             // We will stick to sending just the command requested by user space.
        }
        dev_info(&dev->interface->dev, "Sent command 0x%02x, transferred %d bytes.\n", command, actual_len);
        ret = 0; // Return 0 on successful transfer regardless of length (which is expected to be 8)
    }

    kfree(buf);
    return ret;
}

// --- File Operations Implementation ---

static int ml_open(struct inode *inode, struct file *file)
{
    struct ml_dev *dev = container_of(inode->i_cdev, struct ml_dev, cdev);
    file->private_data = dev;
    dev_info(&dev->interface->dev, "Device opened.\n");
    return 0;
}

static int ml_release(struct inode *inode, struct file *file)
{
    struct ml_dev *dev = file->private_data;
    dev_info(&dev->interface->dev, "Device closed.\n");
    return 0;
}

static ssize_t ml_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    struct ml_dev *dev = file->private_data;
    unsigned char command;
    int retval;

    // The user-space program only sends a single byte command (`write(fd, &cmd, 1);`)
    if (count != 1) {
        dev_warn(&dev->interface->dev, "Write size must be 1 byte, got %zu\n", count);
        return -EINVAL;
    }

    // Copy the single command byte from user space
    if (copy_from_user(&command, buf, 1))
        return -EFAULT;

    // Send the command via USB control message
    retval = ml_send_cmd(dev, command);
    
    if (retval < 0) {
        // Error occurred in sending USB command
        return retval;
    }

    // Success: return the number of bytes successfully processed (which is 1)
    return 1;
}

// --- File Operations Struct ---
static const struct file_operations ml_fops = {
    .owner      = THIS_MODULE,
    .open       = ml_open,
    .release    = ml_release,
    .write      = ml_write,
};

// --- USB Driver Probe and Disconnect ---

static int ml_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    int ret = 0;
    struct usb_device *udev = interface_to_usbdev(interface);

    // 1. Allocate memory for device structure
    ml_device = kzalloc(sizeof(*ml_device), GFP_KERNEL);
    if (!ml_device) {
        pr_err("Out of memory\n");
        return -ENOMEM;
    }

    // 2. Initialize device structure
    ml_device->usb_dev = udev;
    ml_device->interface = interface;
    usb_set_intfdata(interface, ml_device);

    // 3. Register a major/minor number for the device (Dynamically allocated)
    ret = alloc_chrdev_region(&ml_device->dev_num, MINOR_BASE, MINOR_COUNT, DEVICE_NAME);
    if (ret < 0) {
        pr_err("Failed to allocate char dev region: %d\n", ret);
        goto error_cleanup_mem;
    }

    // 4. Create the device class (in /sys/class/)
    // FIX: Changed from class_create(THIS_MODULE, CLASS_NAME) to class_create(CLASS_NAME)
    // to comply with modern kernel API found in the user's build environment.
    ml_device->dev_class = class_create(CLASS_NAME);
    if (IS_ERR(ml_device->dev_class)) {
        pr_err("Failed to create class: %ld\n", PTR_ERR(ml_device->dev_class));
        ret = PTR_ERR(ml_device->dev_class);
        goto error_cleanup_region;
    }

    // 5. Initialize the cdev structure and link it to file operations
    cdev_init(&ml_device->cdev, &ml_fops);
    ml_device->cdev.owner = THIS_MODULE;

    // 6. Add the cdev to the system
    ret = cdev_add(&ml_device->cdev, ml_device->dev_num, MINOR_COUNT);
    if (ret < 0) {
        pr_err("Failed to add cdev: %d\n", ret);
        goto error_cleanup_class;
    }

    // 7. Create the actual device file in /dev/ (e.g., /dev/ml0)
    if (IS_ERR(device_create(ml_device->dev_class, &interface->dev, ml_device->dev_num, ml_device, DEVICE_NAME))) {
        pr_err("Failed to create device file %s\n", DEVICE_NAME);
        ret = -EFAULT;
        goto error_cleanup_cdev;
    }

    pr_info("Missile Launcher USB device (%04x:%04x) attached and device file %s created.\n",
            ML_VENDOR_ID, ML_PRODUCT_ID, DEVICE_NAME);
    return 0;

// --- Error Cleanup Path ---
error_cleanup_cdev:
    cdev_del(&ml_device->cdev);
error_cleanup_class:
    class_destroy(ml_device->dev_class);
error_cleanup_region:
    unregister_chrdev_region(ml_device->dev_num, MINOR_COUNT);
error_cleanup_mem:
    kfree(ml_device);
    ml_device = NULL;
    return ret;
}

static void ml_disconnect(struct usb_interface *interface)
{
    struct ml_dev *dev;
    
    // Get the device structure back from the interface
    dev = usb_get_intfdata(interface);
    usb_set_intfdata(interface, NULL);
    
    // 1. Destroy the device file
    device_destroy(dev->dev_class, dev->dev_num);
    
    // 2. Remove the cdev
    cdev_del(&dev->cdev);
    
    // 3. Destroy the class
    class_destroy(dev->dev_class);
    
    // 4. Unregister the char device region
    unregister_chrdev_region(dev->dev_num, MINOR_COUNT);
    
    // 5. Free the memory
    kfree(dev);
    ml_device = NULL;
    
    pr_info("Missile Launcher USB device detached and device file %s removed.\n", DEVICE_NAME);
}

// --- USB Driver Struct ---
static struct usb_driver ml_driver = {
    .name       = "missile_launcher_driver",
    .probe      = ml_probe,
    .disconnect = ml_disconnect,
    .id_table   = ml_id_table,
};

// --- Module Init/Exit ---

static int __init ml_init(void)
{
    pr_info("Loading Missile Launcher USB Driver.\n");
    return usb_register(&ml_driver);
}

static void __exit ml_exit(void)
{
    pr_info("Unloading Missile Launcher USB Driver.\n");
    usb_deregister(&ml_driver);
}

module_init(ml_init);
module_exit(ml_exit);

// --- Module Information ---
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gemini LLM");
MODULE_DESCRIPTION("Driver for USB Missile Launcher based on user-space program commands.");

