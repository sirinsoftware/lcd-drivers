/*
 * ILI9341 Framebuffer
 *
 * ILI9341 chip drive TFT screen up to 320x240.
 *
 */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/fb.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

int rotate = 0;
module_param(rotate, int, 0444);

int rate = 5;
module_param(rate, int, 0644);

int mode_BGR = 1;
module_param(mode_BGR, int, 0644);

#define DEBUG

#define ILI_COMMAND                     1
#define ILI_DATA                        0

#define ILI_GPIO_DC						42


static int global_counter = 0;

static struct ili9341 ili9341_data;

static struct resource ili9341_resources[] = {
	[0] = {
		.start	= SW_VA_SRAM_BASE,
		.end	= SW_VA_SRAM_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= SW_INT_IRQNO_SPI01,
		.end	= SW_INT_IRQNO_SPI01,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ili9341_device = {
	.name		= "ili9341",
	.id		= 0,
	.dev		= {
				.platform_data		= &ili9341_data,
	},
	.resource	= ili9341_resources,
	.num_resources	= ARRAY_SIZE(ili9341_resources),
};


struct ili9341_page {
        unsigned short x;
        unsigned short y;
        unsigned short *buffer;
        unsigned short len;
        int must_update;
};

struct ili9341 {
        struct device *dev;
    	struct spi_device *spi;
        volatile unsigned short *ctrl_io;
        volatile unsigned short *data_io;
        struct fb_info *info;
        unsigned int pages_count;
        struct ili9341_page *pages;
        unsigned long pseudo_palette[25];
        unsigned short *tmpbuf;
        unsigned short *tmpbuf_be;
};

static void ili9341_clear_graph(struct ili9341 *item);

int ili9341_write_spi(struct ili9341 *item, void *buf, size_t len)
{
	struct spi_transfer t = {
		.tx_buf = buf,
		.len = len,
	};
	struct spi_message m;

	if (!item->spi) {
		dev_err(item->info->device,
			"%s: par->spi is unexpectedly NULL\n", __func__);
		return -1;
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(item->spi, &m);
}

int ili9341_write_spi_1_byte(struct ili9341 *item, unsigned char byte)
{
	unsigned char tmp_byte = byte;

	struct spi_transfer t = {
		.tx_buf = &tmp_byte,
		.len = 1,
	};
	struct spi_message m;

	pr_debug("%s(len=1): ", __func__);

	if (!item->spi) {
		dev_err(item->info->device,
			"%s: par->spi is unexpectedly NULL\n", __func__);
		return -1;
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(item->spi, &m);
}

static int ili9341_init_gpio(struct ili9341 *item)
{
	//DC high - data, DC low - command
	return gpio_request_one(ILI_GPIO_DC, GPIOF_OUT_INIT_HIGH,
		item->info->device->driver->name);
}

static void ili9341_free_gpio(void)
{
	gpio_free(ILI_GPIO_DC);
}

static void ili9341_write_data(struct ili9341 *item, unsigned char dc, unsigned char value) {

	if (dc == ILI_COMMAND) {
		gpio_set_value(ILI_GPIO_DC, 0);
		ili9341_write_spi_1_byte(item, value);
		gpio_set_value(ILI_GPIO_DC, 1);
	} else { //ILI_DATA
		ili9341_write_spi_1_byte(item, value);
	}

}

#define MEM_Y   (7) /* MY row address order */
#define MEM_X   (6) /* MX column address order */
#define MEM_V   (5) /* MV row / column exchange */
#define MEM_L   (4) /* ML vertical refresh order */
#define MEM_BGR (3) /* RGB-BGR Order */
#define MEM_H   (2) /* MH horizontal refresh order */

void ili9341_set_display_res(struct ili9341 *item, int xres, int yres,
		int xres_virtual, int yres_virtual, int width, int height)
{
	item->info->var.xres = xres;
	item->info->var.yres = yres;
	item->info->var.xres_virtual = xres_virtual;
	item->info->var.yres_virtual = yres_virtual;
	item->info->var.width = width;
	item->info->var.height = height;
	item->info->fix.line_length = xres * 2;
}

static void ili9341_set_display_options(struct ili9341 *item)
{
	//rotate
	ili9341_write_data(item, ILI_COMMAND, 0x36);
	switch (rotate)
	{
	case 0:
		ili9341_write_data(item, ILI_DATA, 1 << MEM_X);
		ili9341_set_display_res(item, 240, 320, 240, 320, 240, 320);
		break;

	case 90:
		ili9341_write_data(item, ILI_DATA, (1 << MEM_Y) | (1 << MEM_X) | (1 << MEM_V));
		ili9341_set_display_res(item, 320, 240, 320, 240, 320, 240);
		break;

	case 180:
		ili9341_write_data(item, ILI_DATA, 1 << MEM_Y);
		ili9341_set_display_res(item, 240, 320, 240, 320, 240, 320);
		break;

	case 270:
		ili9341_write_data(item, ILI_DATA, (1<<MEM_V) | (1 << MEM_L));
		ili9341_set_display_res(item, 320, 240, 320, 240, 320, 240);
		break;
	}
}

/* Init sequence taken from: Arduino Library for the Adafruit 2.2" display */
static int ili9341_init_display(struct ili9341 *item)
{
	/* Software Reset */
	ili9341_write_data(item, ILI_COMMAND, 0x01);

	mdelay(120);

	/* Display OFF */
	ili9341_write_data(item, ILI_COMMAND, 0x28);

	ili9341_write_data(item, ILI_COMMAND, 0xEF);
	ili9341_write_data(item, ILI_DATA, 0x03);
	ili9341_write_data(item, ILI_DATA, 0x80);
	ili9341_write_data(item, ILI_DATA, 0x02);

	ili9341_write_data(item, ILI_COMMAND, 0xCF);
	ili9341_write_data(item, ILI_DATA, 0x00);
	ili9341_write_data(item, ILI_DATA, 0xC1);
	ili9341_write_data(item, ILI_DATA, 0x30);

	ili9341_write_data(item, ILI_COMMAND, 0xED);
	ili9341_write_data(item, ILI_DATA, 0x64);
	ili9341_write_data(item, ILI_DATA, 0x03);
	ili9341_write_data(item, ILI_DATA, 0x12);
	ili9341_write_data(item, ILI_DATA, 0x81);

	ili9341_write_data(item, ILI_COMMAND, 0xE8);
	ili9341_write_data(item, ILI_DATA, 0x85);
	ili9341_write_data(item, ILI_DATA, 0x00);
	ili9341_write_data(item, ILI_DATA, 0x78);

	ili9341_write_data(item, ILI_COMMAND, 0xCB);
	ili9341_write_data(item, ILI_DATA, 0x39);
	ili9341_write_data(item, ILI_DATA, 0x2C);
	ili9341_write_data(item, ILI_DATA, 0x00);
	ili9341_write_data(item, ILI_DATA, 0x34);
	ili9341_write_data(item, ILI_DATA, 0x02);

	ili9341_write_data(item, ILI_COMMAND, 0xF7);
	ili9341_write_data(item, ILI_DATA, 0x20);

	ili9341_write_data(item, ILI_COMMAND, 0xEA);
	ili9341_write_data(item, ILI_DATA, 0x00);
	ili9341_write_data(item, ILI_DATA, 0x00);

	/* Power Control 1 */
	ili9341_write_data(item, ILI_COMMAND, 0xC0);
	ili9341_write_data(item, ILI_DATA, 0x23);

	/* Power Control 2 */
	ili9341_write_data(item, ILI_COMMAND, 0xC1);
	ili9341_write_data(item, ILI_DATA, 0x10);

	/* VCOM Control 1 */
	ili9341_write_data(item, ILI_COMMAND, 0xC5);
	ili9341_write_data(item, ILI_DATA, 0x3e);
	ili9341_write_data(item, ILI_DATA, 0x28);

	/* VCOM Control 2 */
	ili9341_write_data(item, ILI_COMMAND, 0xC7);
	ili9341_write_data(item, ILI_DATA, 0x86);

	/* COLMOD: Pixel Format Set */
	/* 16 bits/pixel */
	ili9341_write_data(item, ILI_COMMAND, 0x3A);
	ili9341_write_data(item, ILI_DATA, 0x55);

	/* Frame Rate Control */
	/* Division ratio = fosc, Frame Rate = 79Hz */
	ili9341_write_data(item, ILI_COMMAND, 0xB1);
	ili9341_write_data(item, ILI_DATA, 0x00);
	ili9341_write_data(item, ILI_DATA, 0x18);

	/* Display Function Control */
	ili9341_write_data(item, ILI_COMMAND, 0xB6);
	ili9341_write_data(item, ILI_DATA, 0x08);
	ili9341_write_data(item, ILI_DATA, 0x82);
	ili9341_write_data(item, ILI_DATA, 0x27);

	/* MADCTL, required to resolve 'mirroring' effect */
	ili9341_write_data(item, ILI_COMMAND, 0x36);
	if (mode_BGR)	{
		ili9341_write_data(item, ILI_DATA, 0x48);
		ili9341_set_display_options(item);
		printk("COLOR LCD in BGR mode\n");
	} else 	{
		ili9341_write_data(item, ILI_DATA, 0x40);
		ili9341_set_display_options(item);
		printk("COLOR LCD in RGB mode\n");
	}


	/* Gamma Function Disable */
	ili9341_write_data(item, ILI_COMMAND, 0xF2);
	ili9341_write_data(item, ILI_DATA, 0x00);

	/* Gamma curve selected  */
	ili9341_write_data(item, ILI_COMMAND, 0x26);
	ili9341_write_data(item, ILI_DATA, 0x01);

	/* Positive Gamma Correction */
	ili9341_write_data(item, ILI_COMMAND, 0xE0);
	ili9341_write_data(item, ILI_DATA, 0x0F);
	ili9341_write_data(item, ILI_DATA, 0x31);
	ili9341_write_data(item, ILI_DATA, 0x2B);
	ili9341_write_data(item, ILI_DATA, 0x0C);
	ili9341_write_data(item, ILI_DATA, 0x0E);
	ili9341_write_data(item, ILI_DATA, 0x08);
	ili9341_write_data(item, ILI_DATA, 0x4E);
	ili9341_write_data(item, ILI_DATA, 0xF1);
	ili9341_write_data(item, ILI_DATA, 0x37);
	ili9341_write_data(item, ILI_DATA, 0x07);
	ili9341_write_data(item, ILI_DATA, 0x10);
	ili9341_write_data(item, ILI_DATA, 0x03);
	ili9341_write_data(item, ILI_DATA, 0x0E);
	ili9341_write_data(item, ILI_DATA, 0x09);
	ili9341_write_data(item, ILI_DATA, 0x00);

	/* Negative Gamma Correction */
	ili9341_write_data(item, ILI_COMMAND, 0xE1);
	ili9341_write_data(item, ILI_DATA, 0x00);
	ili9341_write_data(item, ILI_DATA, 0x0E);
	ili9341_write_data(item, ILI_DATA, 0x14);
	ili9341_write_data(item, ILI_DATA, 0x03);
	ili9341_write_data(item, ILI_DATA, 0x11);
	ili9341_write_data(item, ILI_DATA, 0x07);
	ili9341_write_data(item, ILI_DATA, 0x31);
	ili9341_write_data(item, ILI_DATA, 0xC1);
	ili9341_write_data(item, ILI_DATA, 0x48);
	ili9341_write_data(item, ILI_DATA, 0x08);
	ili9341_write_data(item, ILI_DATA, 0x0F);
	ili9341_write_data(item, ILI_DATA, 0x0C);
	ili9341_write_data(item, ILI_DATA, 0x31);
	ili9341_write_data(item, ILI_DATA, 0x36);
	ili9341_write_data(item, ILI_DATA, 0x0F);

	/* Sleep OUT */
	ili9341_write_data(item, ILI_COMMAND, 0x11);

	mdelay(120);

	/* Display ON */
	ili9341_write_data(item, ILI_COMMAND, 0x29);

	ili9341_clear_graph(item);

	printk("COLOR LCD driver initialized\n");


	return 0;
}

static void ili9341_set_window(struct ili9341 *item, int xs, int ys, int xe, int ye)
{
	printk("%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

	/* Column address */
	ili9341_write_data(item, ILI_COMMAND, 0x2A);
	ili9341_write_data(item, ILI_DATA, xs >> 8);
	ili9341_write_data(item, ILI_DATA, xs & 0xFF);
	ili9341_write_data(item, ILI_DATA, xe >> 8);
	ili9341_write_data(item, ILI_DATA, xe & 0xFF);

	/* Row adress */
	ili9341_write_data(item, ILI_COMMAND, 0x2B);
	ili9341_write_data(item, ILI_DATA, ys >> 8);
	ili9341_write_data(item, ILI_DATA, ys & 0xFF);
	ili9341_write_data(item, ILI_DATA, ye >> 8);
	ili9341_write_data(item, ILI_DATA, ye & 0xFF);

	/* Memory write */
	ili9341_write_data(item, ILI_COMMAND, 0x2C);
}

static void ili9341_clear_graph(struct ili9341 *item)
{
	switch (rotate)
	{
	case 0:
	case 180:
		ili9341_set_window(item, 0x0000, 0x0000, 0x00ef, 0x013f);
		break;
	case 90:
	case 270:
		ili9341_set_window(item, 0x0000, 0x0000, 0x013f, 0x00ef);
		break;
	}
}

static void ili9341_touch(struct fb_info *info, int x, int y, int w, int h)
{
      struct fb_deferred_io *fbdefio = info->fbdefio;
      struct ili9341 *item = (struct ili9341 *)info->par;

      if (fbdefio) {
          //Schedule the deferred IO to kick in after a delay.
          schedule_delayed_work(&info->deferred_work, fbdefio->delay);
      }
}


static int __init ili9341_video_alloc(struct ili9341 *item)
{
        unsigned int frame_size;

        dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);

        frame_size = item->info->fix.line_length * item->info->var.yres;
        dev_dbg(item->dev, "%s: item=0x%p frame_size=%u\n",
                __func__, (void *)item, frame_size);

        item->pages_count = frame_size / PAGE_SIZE;
        if ((item->pages_count * PAGE_SIZE) < frame_size) {
                item->pages_count++;
        }
        dev_dbg(item->dev, "%s: item=0x%p pages_count=%u\n",
                __func__, (void *)item, item->pages_count);

        item->info->fix.smem_len = item->pages_count * PAGE_SIZE;
        item->info->fix.smem_start =
            (unsigned short*) vmalloc(item->info->fix.smem_len);
        if (!item->info->fix.smem_start) {
                dev_err(item->dev, "%s: unable to vmalloc\n", __func__);
                return -ENOMEM;
        }
        memset((void *)item->info->fix.smem_start, 0, item->info->fix.smem_len);

        return 0;
}

static void ili9341_video_free(struct ili9341 *item)
{
        dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);

        kfree((void *)item->info->fix.smem_start);
}

//This routine will allocate a ili9341_page struct for each vm page in the
//main framebuffer memory. Each struct will contain a pointer to the page
//start, an x- and y-offset, and the length of the pagebuffer which is in the framebuffer.
static int __init ili9341_pages_alloc(struct ili9341 *item)
{
        unsigned short pixels_per_page;
        unsigned short yoffset_per_page;
        unsigned short xoffset_per_page;
        unsigned short index;
        unsigned short x = 0;
        unsigned short y = 0;
        unsigned short *buffer;
        unsigned int len;

        dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);

        item->pages = kmalloc(item->pages_count * sizeof(struct ili9341_page),
                              GFP_KERNEL);
        if (!item->pages) {
                dev_err(item->dev, "%s: unable to kmalloc for ssd1289_page\n",
                        __func__);
                return -ENOMEM;
        }

        pixels_per_page = PAGE_SIZE / (item->info->var.bits_per_pixel / 8);
        yoffset_per_page = pixels_per_page / item->info->var.xres;
        xoffset_per_page = pixels_per_page -
            (yoffset_per_page * item->info->var.xres);
        dev_dbg(item->dev, "%s: item=0x%p pixels_per_page=%hu "
                "yoffset_per_page=%hu xoffset_per_page=%hu\n",
                __func__, (void *)item, pixels_per_page,
                yoffset_per_page, xoffset_per_page);

        buffer = (unsigned short *)item->info->fix.smem_start;
        for (index = 0; index < item->pages_count; index++) {
                len = (item->info->var.xres * item->info->var.yres) -
                    (index * pixels_per_page);
                if (len > pixels_per_page) {
                        len = pixels_per_page;
                }
                dev_dbg(item->dev,
                        "%s: page[%d]: x=%3hu y=%3hu buffer=0x%p len=%3hu\n",
                        __func__, index, x, y, buffer, len);
                item->pages[index].x = x;
                item->pages[index].y = y;
                item->pages[index].buffer = buffer;
                item->pages[index].len = len;

                x += xoffset_per_page;
                if (x >= item->info->var.xres) {
                        y++;
                        x -= item->info->var.xres;
                }
                y += yoffset_per_page;
                buffer += pixels_per_page;
        }

        return 0;
}

static void ili9341_pages_free(struct ili9341 *item)
{
        dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);

        kfree(item->pages);
}

static void ili9341_update(struct fb_info *info, struct list_head *pagelist)
{
    struct ili9341 *item = (struct ili9341 *)info->par;
    struct page *page;
    int i, j;

    //Copy all pages.
    for (i=0; i<item->pages_count; i++) {
    		if(i == item->pages_count-1) {
    			memcpy(item->tmpbuf, item->pages[i].buffer, PAGE_SIZE/2);
    			for (j=0; j<PAGE_SIZE/2; j++) {
    				item->tmpbuf_be[j] = htons(item->tmpbuf[j]);
    			}
    			ili9341_write_spi(item, item->tmpbuf_be, PAGE_SIZE/2);
    		}
            else {
            	memcpy(item->tmpbuf, item->pages[i].buffer, PAGE_SIZE);
    			for (j=0; j<PAGE_SIZE; j++) {
    				item->tmpbuf_be[j] = htons(item->tmpbuf[j]);
    			}
            	ili9341_write_spi(item, item->tmpbuf_be, PAGE_SIZE);
            }
    }
}

static inline __u32 CNVT_TOHW(__u32 val, __u32 width)
{
        return ((val<<width) + 0x7FFF - val)>>16;
}


//This routine is needed because the console driver won't work without it.
static int ili9341_setcolreg(unsigned regno,
                               unsigned red, unsigned green, unsigned blue,
                               unsigned transp, struct fb_info *info)
{
    int ret = 1;

    /*
     * If greyscale is true, then we convert the RGB value
     * to greyscale no matter what visual we are using.
     */
    if (info->var.grayscale)
            red = green = blue = (19595 * red + 38470 * green +
                                  7471 * blue) >> 16;
    switch (info->fix.visual) {
    case FB_VISUAL_TRUECOLOR:
            if (regno < 16) {
                    u32 *pal = info->pseudo_palette;
                    u32 value;

                    red = CNVT_TOHW(red, info->var.red.length);
                    green = CNVT_TOHW(green, info->var.green.length);
                    blue = CNVT_TOHW(blue, info->var.blue.length);
                    transp = CNVT_TOHW(transp, info->var.transp.length);

                    value = (red << info->var.red.offset) |
                            (green << info->var.green.offset) |
                            (blue << info->var.blue.offset) |
                            (transp << info->var.transp.offset);

                    pal[regno] = value;
                    ret = 0;
            }
            break;
    case FB_VISUAL_STATIC_PSEUDOCOLOR:
    case FB_VISUAL_PSEUDOCOLOR:
            break;
    }
    return ret;

	return 0;
}

static int ili9341_blank(int blank_mode, struct fb_info *info)
{
        return 0;
}

static void ili9341_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
        sys_fillrect(p, rect);
        ili9341_touch(p, rect->dx, rect->dy, rect->width, rect->height);

}

static void ili9341_imageblit(struct fb_info *p, const struct fb_image *image)
{
        sys_imageblit(p, image);
        ili9341_touch(p, image->dx, image->dy, image->width, image->height);
}

static void ili9341_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
        sys_copyarea(p, area);
        ili9341_touch(p, area->dx, area->dy, area->width, area->height);

}

static ssize_t ili9341_write(struct fb_info *p, const char __user *buf,
                                size_t count, loff_t *ppos)
{
        ssize_t res;
        res = fb_sys_write(p, buf, count, ppos);
        ili9341_touch(p, 0, 0, p->var.xres, p->var.yres);
        return res;
}

static struct fb_ops ili9341_fbops = {
        .owner        = THIS_MODULE,
        .fb_read      = fb_sys_read,
        .fb_write     = ili9341_write,
        .fb_fillrect  = ili9341_fillrect,
        .fb_copyarea  = ili9341_copyarea,
        .fb_imageblit = ili9341_imageblit,
        .fb_setcolreg   = ili9341_setcolreg,
        .fb_blank       = ili9341_blank,
};

static struct fb_fix_screeninfo ili9341_fix __initdata = {
        .id          = "ILI9341",
        .type        = FB_TYPE_PACKED_PIXELS,
        .visual      = FB_VISUAL_TRUECOLOR,
        .accel       = FB_ACCEL_NONE,
        .line_length = 320 * 2,
};

static struct fb_var_screeninfo ili9341_var __initdata = {
        .xres           = 320,
        .yres           = 240,
        .xres_virtual   = 320,
        .yres_virtual   = 240,
        .width          = 320,
        .height         = 240,
        .bits_per_pixel = 16,
    	.red   			= {11, 5, 0},
    	.green 			= {5, 6, 0},
    	.blue 			= {0, 5, 0},
        .activate       = FB_ACTIVATE_NOW,
        .vmode          = FB_VMODE_NONINTERLACED,
};

static struct fb_deferred_io ili9341_defio = {
        .delay          = HZ / 35,
        .deferred_io    = &ili9341_update,
};

static int __init ili9341_probe(struct platform_device *dev)
{
        int ret = 0;
        struct ili9341 *item;
        struct fb_info *info;

        dev_dbg(&dev->dev, "%s\n", __func__);

        item = kzalloc(sizeof(struct ili9341), GFP_KERNEL);
        if (!item) {
                dev_err(&dev->dev,
                        "%s: unable to kzalloc for ili9341\n", __func__);
                ret = -ENOMEM;
                goto out;
        }
        item->dev = &dev->dev;
        dev_set_drvdata(&dev->dev, item);
        dev_dbg(&dev->dev, "Before registering SPI\n");

        info = framebuffer_alloc(sizeof(struct ili9341), &dev->dev);
        if (!info) {
                ret = -ENOMEM;
                dev_err(&dev->dev,
                        "%s: unable to framebuffer_alloc\n", __func__);
                goto out_item;
        }
        info->pseudo_palette = &item->pseudo_palette;
        item->info = info;
        info->par = item;
        info->dev = &dev->dev;
        info->fbops = &ili9341_fbops;
        info->flags = FBINFO_FLAG_DEFAULT;
        info->fix = ili9341_fix;
        if (mode_BGR)	{
        	ili9341_var.red.offset  = 0;
        	ili9341_var.red.length  = 5;
        	ili9341_var.red.msb_right  = 0;
        	ili9341_var.green.offset = 5;
        	ili9341_var.green.length  = 6;
        	ili9341_var.green.msb_right  = 0;
        	ili9341_var.blue.offset  = 11;
        	ili9341_var.blue.length  = 5;
        	ili9341_var.blue.msb_right  = 0;
        } else	{
        	ili9341_var.red.offset  = 11;
        	ili9341_var.red.length  = 5;
        	ili9341_var.red.msb_right  = 0;
        	ili9341_var.green.offset = 5;
        	ili9341_var.green.length  = 6;
        	ili9341_var.green.msb_right  = 0;
        	ili9341_var.blue.offset  = 0;
        	ili9341_var.blue.length  = 5;
        	ili9341_var.blue.msb_right  = 0;
        }
        info->var = ili9341_var;

        item->tmpbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
        if (!item->tmpbuf) {
        	ret = -ENOMEM;
        	dev_err(&dev->dev, "%s: unable to allocate memory for tmpbuf\n", __func__);
        	goto out_tmpbuf;
        }

        item->tmpbuf_be = kmalloc(PAGE_SIZE, GFP_DMA);
        if (!item->tmpbuf_be) {
        	ret = -ENOMEM;
        	dev_err(&dev->dev, "%s: unable to allocate memory for tmpbuf_be\n", __func__);
        	goto out_tmpbuf_be;
        }

     	struct device* spidevice = bus_find_device_by_name(&spi_bus_type, NULL, "spi1.0");
     	if (!spidevice) {
     		dev_err(&dev->dev, "%s: Couldn't find SPI device\n", __func__);
     		ret = -ENODEV;
     		goto out_tmpbuf_be;
     	}
     	item->spi = to_spi_device(spidevice);

    	ili9341_init_gpio(item);
     	ili9341_init_display(item);

        ret = ili9341_video_alloc(item);
        if (ret) {
                dev_err(&dev->dev,
                        "%s: unable to ili9341_video_alloc\n", __func__);
                goto out_info;
        }
        info->screen_base = (char __iomem *)item->info->fix.smem_start;
        ret = ili9341_pages_alloc(item);
        if (ret < 0) {
                dev_err(&dev->dev,
                        "%s: unable to ili9341_pages_init\n", __func__);
                goto out_video;
        }

        info->fbdefio = &ili9341_defio;
        fb_deferred_io_init(info);

        ret = register_framebuffer(info);
        if (ret < 0) {
                dev_err(&dev->dev,
                        "%s: unable to register_frambuffer\n", __func__);
                goto out_pages;
        }

        return ret;

out_pages:
		ili9341_pages_free(item);
out_video:
		ili9341_video_free(item);
out_info:
		kfree(item->tmpbuf_be);
out_tmpbuf_be:
		kfree(item->tmpbuf);
out_tmpbuf:
        framebuffer_release(info);
out_item:
        kfree(item);
out:
        return ret;
}

static int ili9341_remove(struct platform_device *device)
{
        struct fb_info *info = platform_get_drvdata(device);
        struct ili9341 *item = (struct ili9341 *)info->par;
        if (info) {
                unregister_framebuffer(info);
                ili9341_pages_free(item);
                ili9341_video_free(item);
                framebuffer_release(info);
                kfree(item);
        }
        ili9341_free_gpio();
        return 0;
}

static struct platform_driver ili9341_driver = {
        .probe = ili9341_probe,
        .remove = ili9341_remove,
        .driver = {
                   .name = "ili9341",
                   },
};

static int __init ili9341_init(void)
{
        int ret = 0;

        pr_debug("%s\n", __func__);

        ret = platform_driver_register(&ili9341_driver);

        if (ret) {
                pr_err("%s: unable to platform_driver_register\n", __func__);
        }

        ret = platform_device_register(&ili9341_device);

        if (ret) {
                pr_err("%s: unable to platform_device_register\n", __func__);
        }

        return ret;
}

module_init(ili9341_init);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Alex Nikitenko, alex.nikitenko@sirinsoftware.com");
MODULE_DESCRIPTION("Framebuffer Driver for PiTFT with ILI9341");
