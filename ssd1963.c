/*
 * SSD1963 Framebuffer
 *
 * The Solomon Systech SSD1963 chip drive TFT screen up to 320x240.
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
#include <linux/delay.h>

#define NHD_COMMAND			1
#define NHD_DATA			0

static unsigned int nhd_data_pin_config[] = {
	AT91_PIN_PE13, AT91_PIN_PE14, AT91_PIN_PE17, AT91_PIN_PE18,
	AT91_PIN_PE19, AT91_PIN_PE20, AT91_PIN_PE21, AT91_PIN_PE22
};

static unsigned int nhd_gpio_pin_config[] = {

	AT91_PIN_PE0, // RESET
	AT91_PIN_PE2, // DC
	AT91_PIN_PE5, // CLK
	AT91_PIN_PE6,  // RD
	AT91_PIN_PE1  // WR

};

struct ssd1963_page {
	unsigned short x;
	unsigned short y;
	unsigned long *buffer;
	unsigned short len;
	int must_update;
};

struct ssd1963 {
	struct device *dev;
	volatile unsigned short *ctrl_io;
	volatile unsigned short *data_io;
	struct fb_info *info;
	unsigned int pages_count;
	struct ssd1963_page *pages;
	unsigned long pseudo_palette[25];
};

static void nhd_write_data(int command, unsigned short value)
{
	int i, j, bank;
	at91_set_gpio_output(AT91_PIN_PE12, 1); //R/D

	for (i=0; i<ARRAY_SIZE(nhd_data_pin_config); i++)
		at91_set_gpio_output(nhd_data_pin_config[i], (value>>i)&0x01);

	if (command)
		at91_set_gpio_output(AT91_PIN_PE10, 0); //D/C
	else
		at91_set_gpio_output(AT91_PIN_PE10, 1); //D/C

	at91_set_gpio_output(AT91_PIN_PE11, 0); //WR
	at91_set_gpio_output(AT91_PIN_PE26, 0); //CS
	at91_set_gpio_output(AT91_PIN_PE26, 1); //CS
	at91_set_gpio_output(AT91_PIN_PE11, 1); //WR
}

static void nhd_init_gpio_regs(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nhd_data_pin_config); i++) {
		at91_set_gpio_output(nhd_data_pin_config[i], 1);
	}
	at91_set_gpio_output(AT91_PIN_PE11, 1); //WR
	at91_set_gpio_output(AT91_PIN_PE26, 1); //CS
	at91_set_gpio_output(AT91_PIN_PE12, 1); //RD
	at91_set_gpio_output(AT91_PIN_PE10, 1); //D/C
	at91_set_gpio_output(AT91_PIN_PE27, 1); //RESET
}

static void nhd_write_to_register (unsigned char reg ,unsigned char value)
{
	nhd_write_data(NHD_COMMAND, reg);
	nhd_write_data(NHD_DATA, value);
}

static inline void nhd_send_rgb_data (unsigned long color)
{
	nhd_write_data(NHD_DATA,((color)>>16));	    //red
	nhd_write_data(NHD_DATA,((color)>>8));	    //green
	nhd_write_data(NHD_DATA,(color));           //blue
}

static void nhd_set_window(unsigned int s_x, unsigned int e_x, unsigned int s_y, unsigned int e_y)
{
	nhd_write_data(NHD_COMMAND, 0x2a);			//SET page address
	nhd_write_data(NHD_DATA, (s_x)>>8);			//SET start page address=0
	nhd_write_data(NHD_DATA, s_x);
	nhd_write_data(NHD_DATA, (e_x)>>8);			//SET end page address=319
	nhd_write_data(NHD_DATA, e_x);

	nhd_write_data(NHD_COMMAND, 0x2b);			//SET column address
	nhd_write_data(NHD_DATA, (s_y)>>8);			//SET start column address=0
	nhd_write_data(NHD_DATA, s_y);
	nhd_write_data(NHD_DATA, (e_y)>>8);			//SET end column address=239
	nhd_write_data(NHD_DATA, e_y);

}

static void nhd_clear_graph(void)
{
	int i;
	int length=76800;

	nhd_set_window(0x0000, 0x013f, 0x0000, 0x00ef);
	nhd_write_data(NHD_COMMAND, 0x2c);

	for(i=0; i<length; i++) {
		nhd_send_rgb_data(0x00000000);
	}
}

static void ssd1963_copy(struct ssd1963 *item, unsigned int index)
{
	unsigned short x;
	unsigned short y;
	unsigned long *buffer;
	unsigned int len;
	unsigned int count;

	unsigned short startx;
	unsigned short endx;
	unsigned short starty;
	unsigned short endy;
	unsigned short offset;


	x = item->pages[index].x;
	y = item->pages[index].y;
	buffer = item->pages[index].buffer;
	len = item->pages[index].len;
	dev_dbg(item->dev,
		"%s: page[%u]: x=%3hu y=%3hu buffer=0x%p len=%3hu\n",
		__func__, index, x, y, buffer, len);

	switch (index%5) {

	case 0:
#ifdef LCD_MODE_565RGB
		offset = 0;
		startx = x;
		starty = y;
		endx   = 319;
		endy   = y+6;
		len    = 1920;
#else
		offset = 0;
		startx = x;
		starty = y;
		endx   = 319;
		endy   = y+2;
		len    = 960;
#endif
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));	    //green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));		    //blue
		}

		offset = len;

		startx = x;
		starty = y+3;
		endx   = x+63;
		endy   = y+3;
		len	   = 64;
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));		//green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));			//blue
		}

		break;

	case 1:
		offset = 0;
		startx = x;
		starty = y;
		endx   = 319;
		endy   = y;
		len    = 256;
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));		//green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));			//blue
		}

		offset += len;

		startx = 0;
		starty = y+1;
		endx   = 319;
		endy   = y+2;
		len	   = 640;
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));		//green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));			//blue
		}

		offset += len;

		startx = 0;
		starty = y+3;
		endx   = 127;
		endy   = y+3;
		len	   = 128;
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));		//green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));			//blue
		}

		break;

	case 2:
		offset = 0;
		startx = x;
		starty = y;
		endx   = 319;
		endy   = y;
		len    = 192;
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));		//green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));			//blue
		}

		offset += len;

		startx = 0;
		starty = y+1;
		endx   = 319;
		endy   = y+2;
		len	   = 640;
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));		//green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));			//blue
		}
		offset += len;

		startx = 0;
		starty = y+3;
		endx   = 191;
		endy   = y+3;
		len	   = 192;
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));		//green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));			//blue
		}

		break;
	case 3:
		offset = 0;
		startx = x;
		starty = y;
		endx   = 319;
		endy   = y;
		len    = 128;
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));		//green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));			//blue
		}

		offset += len;

		startx = 0;
		starty = y+1;
		endx   = 319;
		endy   = y+2;
		len	   = 640;
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));		//green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));			//blue
		}
		offset += len;

		startx = 0;
		starty = y+3;
		endx   = 255;
		endy   = y+3;
		len	   = 256;
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));		//green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));			//blue
		}

		break;
	case 4:
		offset = 0;
		startx = x;
		starty = y;
		endx   = 319;
		endy   = y;
		len    = 64;
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));		//green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));			//blue
		}
		offset += len;

		startx = 0;
		starty = y+1;
		endx   = 319;
		endy   = y+3;
		len	   = 960;
		nhd_set_window(startx, endx, starty, endy);
		nhd_write_data(NHD_COMMAND, 0x2c);

		for (count = 0; count < len; count++) {
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>16));	    //red
			nhd_write_data(NHD_DATA,(unsigned char)((buffer[count+offset])>>8));		//green
			nhd_write_data(NHD_DATA,(unsigned char)(buffer[count+offset]));			//blue
		}

		break;

	}
}

static void ssd1963_update_all(struct ssd1963 *item)
{
	unsigned short i;
	struct fb_deferred_io *fbdefio = item->info->fbdefio;
	for (i = 0; i < item->pages_count; i++) {
		item->pages[i].must_update=1;
	}
	schedule_delayed_work(&item->info->deferred_work, fbdefio->delay);
}

static void ssd1963_update(struct fb_info *info, struct list_head *pagelist)
{
	struct ssd1963 *item = (struct ssd1963 *)info->par;
	struct page *page;
	int i;

	//We can be called because of pagefaults (mmap'ed framebuffer, pages
	//returned in *pagelist) or because of kernel activity
	//(pages[i]/must_update!=0). Add the former to the list of the latter.
	list_for_each_entry(page, pagelist, lru) {
		item->pages[page->index].must_update=1;
	}

	//Copy changed pages.
	for (i=0; i<item->pages_count; i++) {
		//ToDo: Small race here between checking and setting must_update,
		//maybe lock?
		if (item->pages[i].must_update) {
			item->pages[i].must_update=0;
			ssd1963_copy(item, i);
		}
	}

}

static void __init ssd1963_setup(struct ssd1963 *item)
{
	dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);

	nhd_init_gpio_regs();

	at91_set_gpio_output(AT91_PIN_PE27, 0); //RESET
	udelay(5);							//TODO if not works try using ms instead of us;
	at91_set_gpio_output(AT91_PIN_PE27, 1); //RESET
	udelay(100);							//TODO if not works try using ms instead of us;

	nhd_write_data(NHD_COMMAND, 0x01); 		//Software Reset
	nhd_write_data(NHD_COMMAND, 0x01);
	nhd_write_data(NHD_COMMAND, 0x01);
	udelay(10);
	nhd_write_to_register(0xe0, 0x01);    		//START PLL
	udelay(100);
	nhd_write_to_register(0xe0, 0x03);    		//LOCK PLL
	nhd_write_data(NHD_COMMAND, 0xb0);		//SET LCD MODE  SET TFT 18Bits MODE
	nhd_write_data(NHD_DATA, 0x0c);			//SET TFT MODE 24 bits & hsync+Vsync+DEN MODE
	nhd_write_data(NHD_DATA, 0x80);			//SET TFT MODE & hsync+Vsync+DEN MODE           !!!!
	nhd_write_data(NHD_DATA, 0x01);			//SET horizontal size=320-1 HightByte
	nhd_write_data(NHD_DATA, 0x3f);		    //SET horizontal size=320-1 LowByte
	nhd_write_data(NHD_DATA, 0x00);			//SET vertical size=240-1 HightByte
	nhd_write_data(NHD_DATA, 0xef);			//SET vertical size=240-1 LowByte
	nhd_write_data(NHD_DATA, 0x00);			//SET even/odd line RGB seq.=RGB
	nhd_write_to_register(0xf0,0x00);	        //SET pixel data I/F format=8bit
	nhd_write_to_register(0x3a,0x70);           //SET R G B format = 8 8 8
	nhd_write_data(NHD_COMMAND, 0xe6);   	//SET PCLK freq=6.4MHz  ; pixel clock frequency
	nhd_write_data(NHD_DATA, 0x00);
	nhd_write_data(NHD_DATA, 0xe7);
	nhd_write_data(NHD_DATA, 0x4f);
	nhd_write_data(NHD_COMMAND, 0xb4);		//SET HBP,
	nhd_write_data(NHD_DATA, 0x01);			//SET HSYNC Total 440
	nhd_write_data(NHD_DATA, 0xb8);
	nhd_write_data(NHD_DATA, 0x00);			//SET HBP 68
	nhd_write_data(NHD_DATA, 0x44);
	nhd_write_data(NHD_DATA, 0x0f);			//SET VBP 16=15+1
	nhd_write_data(NHD_DATA, 0x00);			//SET Hsync pulse start position
	nhd_write_data(NHD_DATA, 0x00);
	nhd_write_data(NHD_DATA, 0x00);			//SET Hsync pulse subpixel start position
	nhd_write_data(NHD_COMMAND, 0xb6); 		//SET VBP,
	nhd_write_data(NHD_DATA, 0x01);			//SET Vsync total 265=264+1
	nhd_write_data(NHD_DATA, 0x08);
	nhd_write_data(NHD_DATA, 0x00);			//SET VBP=19
	nhd_write_data(NHD_DATA, 0x13);
	nhd_write_data(NHD_DATA, 0x07);			//SET Vsync pulse 8=7+1
	nhd_write_data(NHD_DATA, 0x00);			//SET Vsync pulse start position
	nhd_write_data(NHD_DATA, 0x00);
	nhd_write_data(NHD_COMMAND, 0x2a);		//SET column address
	nhd_write_data(NHD_DATA, 0x00);			//SET start column address=0
	nhd_write_data(NHD_DATA, 0x00);
	nhd_write_data(NHD_DATA, 0x01);			//SET end column address=319
	nhd_write_data(NHD_DATA, 0x3f);
	nhd_write_data(NHD_COMMAND, 0x2b);		//SET page address
	nhd_write_data(NHD_DATA, 0x00);			//SET start page address=0
	nhd_write_data(NHD_DATA, 0x00);
	nhd_write_data(NHD_DATA, 0x00);			//SET end page address=239
	nhd_write_data(NHD_DATA, 0xef);
	nhd_write_data(NHD_COMMAND, 0x29);		//SET display on

	nhd_set_window(0x0000, 0x013f, 0x0000, 0x00ef);
	nhd_write_data(NHD_COMMAND, 0x2c);

	nhd_clear_graph();

	printk(KERN_ALERT "COLOR LCD driver initialized\n");
}

//This routine will allocate the buffer for the complete framebuffer. This
//is one continuous chunk of 16-bit pixel values; userspace programs
//will write here.
static int __init ssd1963_video_alloc(struct ssd1963 *item)
{
	unsigned int frame_size;

	dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);

	frame_size = item->info->fix.line_length * item->info->var.yres;
	printk(KERN_ALERT "frame_size =%d\n", frame_size);
	dev_dbg(item->dev, "%s: item=0x%p frame_size=%u\n",
		__func__, (void *)item, frame_size);

	item->pages_count = frame_size / PAGE_SIZE;
	if ((item->pages_count * PAGE_SIZE) < frame_size) {
		item->pages_count++;
	}
	printk(KERN_ALERT "pages_count =%d\n", item->pages_count);
	dev_dbg(item->dev, "%s: item=0x%p pages_count=%u\n",
		__func__, (void *)item, item->pages_count);

	item->info->fix.smem_len = item->pages_count * PAGE_SIZE;
	item->info->fix.smem_start =
	    (unsigned long)vmalloc(item->info->fix.smem_len);
	if (!item->info->fix.smem_start) {
		dev_err(item->dev, "%s: unable to vmalloc\n", __func__);
		return -ENOMEM;
	}
	memset((void *)item->info->fix.smem_start, 0, item->info->fix.smem_len);

	return 0;
}

static void ssd1963_video_free(struct ssd1963 *item)
{
	dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);

	kfree((void *)item->info->fix.smem_start);
}

//This routine will allocate a ssd1963_page struct for each vm page in the
//main framebuffer memory. Each struct will contain a pointer to the page
//start, an x- and y-offset, and the length of the pagebuffer which is in the framebuffer.
static int __init ssd1963_pages_alloc(struct ssd1963 *item)
{
	unsigned short pixels_per_page;
	unsigned short yoffset_per_page;
	unsigned short xoffset_per_page;
	unsigned short index;
	unsigned short x = 0;
	unsigned short y = 0;
	unsigned long *buffer;
	unsigned int len;

	dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);

	item->pages = kmalloc(item->pages_count * sizeof(struct ssd1963_page),
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

	buffer = (unsigned long *)item->info->fix.smem_start;
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

static void ssd1963_pages_free(struct ssd1963 *item)
{
	dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);

	kfree(item->pages);
}

static inline __u32 CNVT_TOHW(__u32 val, __u32 width)
{
	return ((val<<width) + 0x7FFF - val)>>16;
}

//This routine is needed because the console driver won't work without it.
static int ssd1963_setcolreg(unsigned regno,
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
}

static int ssd1963_blank(int blank_mode, struct fb_info *info)
{
	return 0;
}

static void ssd1963_touch(struct fb_info *info, int x, int y, int w, int h)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct ssd1963 *item = (struct ssd1963 *)info->par;
	int i, ystart, yend;
	if (fbdefio) {
		//Touch the pages the y-range hits, so the deferred io will update them.
		for (i=0; i<item->pages_count; i++) {
			ystart=item->pages[i].y;
			yend=item->pages[i].y+(item->pages[i].len/info->fix.line_length)+1;
			if (!((y+h)<ystart || y>yend)) {
				item->pages[i].must_update=1;
			}
		}
		//Schedule the deferred IO to kick in after a delay.
		schedule_delayed_work(&info->deferred_work, fbdefio->delay);
	}
}

static void ssd1963_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
	sys_fillrect(p, rect);
	ssd1963_touch(p, rect->dx, rect->dy, rect->width, rect->height);
}

static void ssd1963_imageblit(struct fb_info *p, const struct fb_image *image)
{
	sys_imageblit(p, image);
	ssd1963_touch(p, image->dx, image->dy, image->width, image->height);
}

static void ssd1963_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
	sys_copyarea(p, area);
	ssd1963_touch(p, area->dx, area->dy, area->width, area->height);
}

static ssize_t ssd1963_write(struct fb_info *p, const char __user *buf,
				size_t count, loff_t *ppos)
{
	ssize_t res;
	res = fb_sys_write(p, buf, count, ppos);
	ssd1963_touch(p, 0, 0, p->var.xres, p->var.yres);
	return res;
}

static struct fb_ops ssd1963_fbops = {
	.owner        = THIS_MODULE,
	.fb_read      = fb_sys_read,
	.fb_write     = ssd1963_write,
	.fb_fillrect  = ssd1963_fillrect,
	.fb_copyarea  = ssd1963_copyarea,
	.fb_imageblit = ssd1963_imageblit,
	.fb_setcolreg	= ssd1963_setcolreg,
	.fb_blank	= ssd1963_blank,
};

static struct fb_fix_screeninfo ssd1963_fix __initdata = {
	.id          = "SSD1963",
	.type        = FB_TYPE_PACKED_PIXELS,
	.visual      = FB_VISUAL_TRUECOLOR,
	.accel       = FB_ACCEL_NONE,
#ifdef LCD_MODE_565RGB
	.line_length = 320 * 2,
#else
	.line_length = 320 * 4,
#endif
};

static struct fb_var_screeninfo ssd1963_var __initdata = {
	.xres		= 320,
	.yres		= 240,
	.xres_virtual	= 320,
	.yres_virtual	= 240,
	.width		= 320,
	.height		= 240,
#ifdef LCD_MODE_565RGB
	.bits_per_pixel	= 16,
	.red		= {11, 5, 0},
	.green		= {5, 6, 0},
	.blue		= {0, 5, 0},
#else
	.bits_per_pixel	= 32,
	.transp     = {24, 8, 0},
	.red		= {16, 8, 0},
	.green		= {8, 8, 0},
	.blue		= {0, 8, 0},
#endif
	.activate	= FB_ACTIVATE_NOW,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct fb_deferred_io ssd1963_defio = {
        .delay          = HZ / 20,
        .deferred_io    = &ssd1963_update,
};

static int __init ssd1963_probe(struct platform_device *dev)
{
	int ret = 0;
	struct ssd1963 *item;
	struct resource *ctrl_res;
	struct resource *data_res;
	unsigned int ctrl_res_size;
	unsigned int data_res_size;
	struct resource *ctrl_req;
	struct resource *data_req;
	struct fb_info *info;

	dev_dbg(&dev->dev, "%s\n", __func__);

	item = kzalloc(sizeof(struct ssd1963), GFP_KERNEL);
	if (!item) {
		dev_err(&dev->dev,
			"%s: unable to kzalloc for ssd1963\n", __func__);
		ret = -ENOMEM;
		goto out;
	}
	item->dev = &dev->dev;
	dev_set_drvdata(&dev->dev, item);

	ctrl_res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!ctrl_res) {
		dev_err(&dev->dev,
			"%s: unable to platform_get_resource for ctrl_res\n",
			__func__);
		ret = -ENOENT;
		goto out_item;
	}
	ctrl_res_size = ctrl_res->end - ctrl_res->start + 1;
	ctrl_req = request_mem_region(ctrl_res->start, ctrl_res_size,
				      dev->name);
	if (!ctrl_req) {
		dev_err(&dev->dev,
			"%s: unable to request_mem_region for ctrl_req\n",
			__func__);
		ret = -EIO;
		goto out_item;
	}
	item->ctrl_io = ioremap(ctrl_res->start, ctrl_res_size);
	if (!item->ctrl_io) {
		ret = -EINVAL;
		dev_err(&dev->dev,
			"%s: unable to ioremap for ctrl_io\n", __func__);
		goto out_item;
	}

	data_res = platform_get_resource(dev, IORESOURCE_MEM, 1);
	if (!data_res) {
		dev_err(&dev->dev,
			"%s: unable to platform_get_resource for data_res\n",
			__func__);
		ret = -ENOENT;
		goto out_item;
	}
	data_res_size = data_res->end - data_res->start + 1;
	data_req = request_mem_region(data_res->start,
				      data_res_size, dev->name);
	if (!data_req) {
		dev_err(&dev->dev,
			"%s: unable to request_mem_region for data_req\n",
			__func__);
		ret = -EIO;
		goto out_item;
	}
	item->data_io = ioremap(data_res->start, data_res_size);
	if (!item->data_io) {
		ret = -EINVAL;
		dev_err(&dev->dev,
			"%s: unable to ioremap for data_io\n", __func__);
		goto out_item;
	}

	dev_dbg(&dev->dev, "%s: ctrl_io=%p data_io=%p\n",
		__func__, item->ctrl_io, item->data_io);

	dev_info(&dev->dev, "item=0x%p ctrl=0x%p data=0x%p\n", (void *)item,
		 (void *)ctrl_res->start, (void *)data_res->start);


	info = framebuffer_alloc(sizeof(struct ssd1963), &dev->dev);
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
	info->fbops = &ssd1963_fbops;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->fix = ssd1963_fix;
	info->var = ssd1963_var;

	ret = ssd1963_video_alloc(item);
	if (ret) {
		dev_err(&dev->dev,
			"%s: unable to ssd1963_video_alloc\n", __func__);
		goto out_info;
	}
	info->screen_base = (char __iomem *)item->info->fix.smem_start;
	ret = ssd1963_pages_alloc(item);
	if (ret < 0) {
		dev_err(&dev->dev,
			"%s: unable to ssd1963_pages_init\n", __func__);
		goto out_video;
	}

	info->fbdefio = &ssd1963_defio;
	fb_deferred_io_init(info);

	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&dev->dev,
			"%s: unable to register_frambuffer\n", __func__);
		goto out_pages;
	}


	ssd1963_setup(item);
	ssd1963_update_all(item);

	return ret;

out_pages:
	ssd1963_pages_free(item);
out_video:
	ssd1963_video_free(item);
out_info:
	framebuffer_release(info);
out_item:
	kfree(item);
out:
	return ret;
}


static int ssd1963_remove(struct platform_device *device)
{
	struct fb_info *info = platform_get_drvdata(device);
	struct ssd1963 *item = (struct ssd1963 *)info->par;
	if (info) {
		//ToDo: directio-mode: shouldn't those resources be free()'ed too?
		unregister_framebuffer(info);
		ssd1963_pages_free(item);
		ssd1963_video_free(item);
		framebuffer_release(info);
		kfree(item);
	}
	return 0;
}



static struct platform_driver ssd1963_driver = {
	.probe = ssd1963_probe,
	.remove = ssd1963_remove,
	.driver = {
		   .name = "ssd1963",
		   },
};


static int __init ssd1963_init(void)
{
	int ret = 0;

	pr_debug("%s\n", __func__);

	ret = platform_driver_register(&ssd1963_driver);

	if (ret) {
		pr_err("%s: unable to platform_driver_register\n", __func__);
	}

	return ret;
}

module_init(ssd1963_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Alex Nikitenko, alex.nikitenko@sirinsoftware.com");
MODULE_DESCRIPTION("CoreWind BSP For NHD-5.7-320240WFB-CTXI-T1 Driver");
