#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/io.h>
#include <mach/platform.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <asm/gpio.h>

/*
This code will create a platform device for an attached 16x2 LCD display
through the Raspberry Pi's GPIO pins. It will create a character device 
that can be called via open() and written to. Programs that write to the
device driver will see the output written to the LCD.
*/

#define SYSTIMER_MOD_AUTH "Tony Provencal"
#define SYSTIMER_MOD_DESCR "GPIO LCD Driver"
#define SYSTIMER_MOD_SDEV "GPIO LCD RPi"

static long lcd_ioctl(struct file * flip, unsigned int cmd, unsigned long arg);
static int lcd_open(struct inode *inode, struct file *filp);
static int lcd_release(struct inode *inode, struct file *filp);
static char *lcd_devnode(struct device *dev, umode_t *mode);
static int lcd_init(void);
static int req_gpio(void);
static int lcd_write(const uint8_t data[]);



/* What follows is a list of defined values to be used in lcd_write() calls.
As this code uses the 4-bit bus mode on the LCD, the data to be sent over
the GPIO pins is split up into 4-bit "nibbles" and sent one at a time. As
the gpio pins in use are not sequential, representing the instructions and
data as arrays of 0s and 1s is convenient. */
static const uint8_t reset[] 	   = {0,0,0,0,0,0,0,0};
static const uint8_t clear[]	   = {0,0,0,0,0,0,0,1};
static const uint8_t home[] 	   = {0,0,0,0,0,0,1,0};
static const uint8_t entry[]	   = {0,0,0,0,0,1,1,0};
static const uint8_t displayon[]   = {0,0,0,0,1,1,1,1};
static const uint8_t displayoff[]  = {0,0,0,0,1,0,0,0};
static const uint8_t functionset[] = {0,0,1,0,0,0,0,0};
static const uint8_t startup1[]    = {0,0,1,1,0,0,1,1};
static const uint8_t startup2[]	   = {0,0,1,1,0,0,1,0};
static const uint8_t a[]     	   = {0,1,0,1,0,0,0,1};

static const struct file_operations lcd_fops = {
    .owner=THIS_MODULE,
    .open=lcd_open,
    .release=lcd_release,
    .unlocked_ioctl=lcd_ioctl,
};

struct lcd_data {
    int lcd_mjr;
    struct class *lcd_class;
    spinlock_t lock;    
};

static struct lcd_data lcd = {
    .lcd_mjr=0,
    .lcd_class=NULL,
};

static long lcd_ioctl(struct file * flip, unsigned int cmd, unsigned long arg){
    return -EINVAL;
}

static int lcd_open(struct inode *inode, struct file *filp){
    return 0;
}

static int lcd_release(struct inode *inode, struct file *filp){
    // Free gpio pins from using process
    spin_lock(&lcd.lock);
    gpio_free(4);
    gpio_free(17);
    gpio_free(18);
    gpio_free(22);
    gpio_free(23);
    gpio_free(24);
    gpio_free(25);
    spin_unlock(&lcd.lock);
    return 0;
}

static char *lcd_devnode(struct device *dev, umode_t *mode){
    if (mode) *mode = 0666;
    return NULL;
}

// Initialize the LCD
static int lcd_init(void){
    spin_lock(&lcd.lock);
    mdelay(15);
    lcd_write(reset);
    mdelay(35);
    lcd_write(startup1);
    lcd_write(startup2);
    lcd_write(functionset);
    lcd_write(displayoff);
    lcd_write(clear);
    lcd_write(entry);
    lcd_write(displayon);
    lcd_write(home);
    gpio_set_value(4,1);
    mdelay(35);
    lcd_write(a);
    mdelay(35);
    gpio_set_value(4,0);
    spin_unlock(&lcd.lock);
    return 0;
}
 
//Write to the LCD
static int lcd_write(const uint8_t data[]){
    /* In 4-bit mode, data is transmitted only over the upper
    four data pins (DB4-DB7). These pins are connected to the RPi's
    GPIO pins 22, 23, 24, and 25, respectively. If the data to be
    sent is 8-bits, the most significant four digits are sent first,
    followed by the second.	*/
    gpio_set_value(25, data[0]);
    gpio_set_value(24, data[1]);
    gpio_set_value(23, data[2]);
    gpio_set_value(22, data[3]);
    gpio_set_value(18, 1);
    udelay(50);
    gpio_set_value(18, 0);
    udelay(50);
    gpio_set_value(25, data[4]);
    gpio_set_value(24, data[5]);
    gpio_set_value(23, data[6]);
    gpio_set_value(22, data[7]);
    gpio_set_value(18, 1);
    udelay(50);
    gpio_set_value(18, 0);
    udelay(50);	

    return 0;
}

//Request GPIO pins
static int req_gpio(void){	
    /* GPIO pins are requested in the kernel with gpio_request. Labels
    are assigned as parameters. Returns 0 on success, a negative number
    on failure. */
    if (gpio_request(4, "RS")) {
        printk("GPIO request failure: %s\n", "RS");
     	    return -1;
    }
    if (gpio_request(17, "RW")) {
        printk("GPIO request failure: %s\n", "RW");
        return -1;
    }
    if (gpio_request(18, "E")) {
        printk("GPIO request failure: %s\n", "E");
        return -1;
    }
    if (gpio_request(22, "DB4")) {
        printk("GPIO request failure: %s\n", "DB4");
        return -1;
    }
    if (gpio_request(23, "DB5")) {
        printk("GPIO request failure: %s\n", "DB5");
        return -1;
    }
    if (gpio_request(24, "DB6")) {
        printk("GPIO request failure: %s\n", "DB6");
        return -1;
    }
    if (gpio_request(25, "DB7")) {
        printk("GPIO request failure: %s\n", "DB7");
        return -1;
    }
	
    // Set the acquired GPIO pins as outputs
    gpio_direction_output( 4, 0);
    gpio_direction_output(17, 0);
    gpio_direction_output(18, 0);
    gpio_direction_output(22, 0);
    gpio_direction_output(23, 0);
    gpio_direction_output(24, 0);
    gpio_direction_output(25, 0);

    return 0;
}

// Module init
static int __init rpigpio_lcd_minit(void){
    // initialize variables
    struct device *dev=NULL;
    int ret=0;
	
    printk(KERN_INFO "%s\n",SYSTIMER_MOD_DESCR);
    printk(KERN_INFO "By: %s\n",SYSTIMER_MOD_AUTH);

    // register character device
    lcd.lcd_mjr=register_chrdev(0,"gpio_lcd",&lcd_fops);
    if (lcd.lcd_mjr<0) {
    	printk(KERN_NOTICE "Cannot register char device\n");
        return lcd.lcd_mjr;
    }
    // create class for LCD for module
    lcd.lcd_class=class_create(THIS_MODULE, "lcd_class");
    if (IS_ERR(lcd.lcd_class)) {
    	unregister_chrdev(lcd.lcd_mjr,"lcd_gpio");
    	return PTR_ERR(lcd.lcd_class);
    }
    lcd.lcd_class->devnode=lcd_devnode;
    // create device for LCD using registered device
    dev=device_create(lcd.lcd_class,NULL,MKDEV(lcd.lcd_mjr,0),(void *)&lcd,"lcd");
    if (IS_ERR(dev)) {
    	class_destroy(lcd.lcd_class);
    	unregister_chrdev(lcd.lcd_mjr,"lcd_gpio");
    	return PTR_ERR(dev);
     }
	
    // request gpio pins and initalize the lcd
    if(req_gpio()<0){
    	class_destroy(lcd.lcd_class);
        unregister_chrdev(lcd.lcd_mjr,"lcd_gpio");
        return PTR_ERR(dev);
    }
    lcd_init();	
	
    //initialize the spinlock
    spin_lock_init(&(lcd.lock));	

    return ret;
}

// Module removal
static void __exit rpigpio_lcd_mcleanup(void){
    // clear the LCD and release all GPIO pins in use
    lcd_write(clear);
    gpio_free(4);
    gpio_free(17);
    gpio_free(18);
    gpio_free(22);
    gpio_free(23);
    gpio_free(24);
    gpio_free(25);
    
    // destroy platform device and other module resources
    device_destroy(lcd.lcd_class,MKDEV(lcd.lcd_mjr,0));
    class_destroy(lcd.lcd_class);
    unregister_chrdev(lcd.lcd_mjr,"lcd_gpio");
    printk(KERN_INFO "Goodbye\n");
    return;
}

module_init(rpigpio_lcd_minit);
module_exit(rpigpio_lcd_mcleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(SYSTIMER_MOD_AUTH);
MODULE_DESCRIPTION(SYSTIMER_MOD_DESCR);
MODULE_SUPPORTED_DEVICE(SYSTIMER_MOD_SDEV);
