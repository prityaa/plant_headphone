
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/slab.h>

#define HUMAN_INTERFACE	3

MODULE_AUTHOR("PRITAM");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HID TEST");

static const unsigned char usb_kbd_keycode[256] = {
          0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
         50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
          4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
         27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
         65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
        105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
         72, 73, 82, 83, 86,127,116,117,183,184,185,186,187,188,189,190,
        191,192,193,194,134,138,130,132,128,129,131,137,133,135,136,113,
        115,114,  0,  0,  0,121,  0, 89, 93,124, 92, 94, 95,  0,  0,  0,
        122,123, 90, 91, 85,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
          0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
          0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
          0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
          0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
         29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
        150,158,159,128,136,177,178,176,142,152,173,140
};

struct plan_driver_data {
	struct input_dev *input_dev;
	struct usb_interface *intf;
	struct usb_endpoint_descriptor *ep;
	struct usb_device *usb_dev;
	struct urb *urb;
	unsigned char *transfer_buffer;
        char *hid_report_desc;
	char name[128];
        char phys[64];
};

static inline void
usb_to_input_id(const struct usb_device *dev, struct input_id *id)
{
        id->bustype = BUS_USB;
        id->vendor = le16_to_cpu(dev->descriptor.idVendor);
        id->product = le16_to_cpu(dev->descriptor.idProduct);
        id->version = le16_to_cpu(dev->descriptor.bcdDevice);
}

void print_hex(const char *s)
{
	while(*s)
		printk("%02x", (unsigned int) *s++);
	printk("\n");
}

static void plant_hid_complete(struct urb *urb)
{
	pr_debug("%s : calling complete, status = %d\n", __func__, urb->status);
	if ((urb->status == -ENOENT) ||         /* unlinked */
            (urb->status == -ENODEV) ||         /* device removed */
            (urb->status == -ECONNRESET) ||     /* unlinked */
            (urb->status == -ESHUTDOWN))        /* device disabled */
	        return;
	
	pr_debug("%s : buffer = \n", __func__); 
	print_hex(urb->transfer_buffer);
        usb_submit_urb(urb, GFP_ATOMIC);
}

static void plant_hid_free_urb(struct plan_driver_data *drv_data)
{
	usb_free_urb(drv_data->urb);
	
	pr_debug("%s : freeing buffer %p\n", __func__, drv_data->transfer_buffer);
	if (drv_data->transfer_buffer)
		kfree(drv_data->transfer_buffer);
	pr_debug("%s : freeing urb\n", __func__);
}


static int plant_hid_alloc_urb(struct plan_driver_data *drv_data, int sz)
{
	if ((drv_data->urb = usb_alloc_urb(0, GFP_KERNEL)) == NULL) {
		pr_debug("%s : failed to alloc urb \n", __func__);
		return -ENOMEM;
	}

	pr_debug("%s : sz = %d\n", __func__, sz);
	drv_data->transfer_buffer = kmalloc(sz, GFP_KERNEL);
	pr_debug("%s : transfer_buffer = %p\n", __func__, drv_data->transfer_buffer);
	if (drv_data->transfer_buffer == NULL) {
		usb_free_urb(drv_data->urb);
		pr_debug("%s : failed to alloc urb buffer \n", __func__);
                return -ENOMEM;
	}

	return 0;	
}

static int plant_hid_get_class_descriptor(struct usb_device *dev, int ifnum,
                                unsigned char type, void *buf, int size)
{
        int result, retries = 4;

        memset(buf, 0, size);
        do {
                result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
                        USB_REQ_GET_DESCRIPTOR, USB_RECIP_INTERFACE
                        | USB_DIR_IN, (type << 8), ifnum, buf, size,
                        USB_CTRL_GET_TIMEOUT);

                retries--;
        } while (result < size && retries);
        return result;
}

/*
static int usb_kbd_event(struct input_dev *dev, unsigned int type,
                         unsigned int code, int value)
{
        unsigned long flags;
        struct plan_driver_data *kbd = input_get_drvdata(dev);

        if (type != EV_LED)
                return -1;

        spin_lock_irqsave(&kbd->leds_lock, flags);
        kbd->newleds = (!!test_bit(LED_KANA,    dev->led) << 3) | (!!test_bit(LED_COMPOSE, dev->led) << 3) |
                       (!!test_bit(LED_SCROLLL, dev->led) << 2) | (!!test_bit(LED_CAPSL,   dev->led) << 1) |
                       (!!test_bit(LED_NUML,    dev->led));

        if (kbd->led_urb_submitted){
                spin_unlock_irqrestore(&kbd->leds_lock, flags);
                return 0;
        }

        if (*(kbd->leds) == kbd->newleds){
                spin_unlock_irqrestore(&kbd->leds_lock, flags);
                return 0;
        }

        *(kbd->leds) = kbd->newleds;

        kbd->led->dev = kbd->usbdev;
        if (usb_submit_urb(kbd->led, GFP_ATOMIC))
                pr_err("usb_submit_urb(leds) failed\n");
        else
                kbd->led_urb_submitted = true;

        spin_unlock_irqrestore(&kbd->leds_lock, flags);

        return 0;
}
*/

static int plnt_hid_probe(struct usb_interface *intf, 
				const struct usb_device_id *ids)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct usb_interface *interface = intf;
	struct plan_driver_data *drv_data;
	struct usb_endpoint_descriptor *ep;
	struct usb_host_interface *cur_set = intf->cur_altsetting;
	int ret, pipe, mx_pkt_sz, i;
	struct input_dev *input_dev;

	ep = &cur_set->endpoint->desc;
	pr_debug("%s : probe is called \n", __func__);
	pr_debug("%s : setting 3 ,0 interface\n", __func__);	
	if ((ret = usb_set_interface(usb_dev, 3, 0)) < 0) {
		dev_err(&interface->dev, "falied to set intf\n");
                return -ENODEV;
	}
#if 0
	if (!usb_endpoint_is_int_in(ep)) {
		dev_err(&interface->dev, "not intr ep\n");
		return -ENODEV;
	}
#endif
	pipe = usb_rcvintpipe(usb_dev, ep->bEndpointAddress); 
	mx_pkt_sz = ep->wMaxPacketSize;
	pr_debug("%s : pipe = %d, bEndpointAddress = 0x%x, mx_pkt_sz = %d\n", 
		__func__, pipe, ep->bEndpointAddress, mx_pkt_sz);
	pr_debug("%s : bInterfaceClass = %d\n", 
		__func__, cur_set->desc.bInterfaceClass);
	if (cur_set->desc.bInterfaceClass != HUMAN_INTERFACE) {
		dev_err(&interface->dev, "HID NOT found\n");
                return -ENODEV;
	}
	
	pr_debug("%s : ep = %p\n", __func__, ep);
	if(ep == NULL) {
		dev_err(&interface->dev, "ep not found\n");	
		return -ENODEV;
	}

	drv_data = kzalloc(sizeof(struct plan_driver_data), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!drv_data || !input_dev) {
		dev_err(&interface->dev, "%s : mem not available\n", __func__);
		return -ENOMEM;
	}

	pr_debug("%s : allocing urb\n", __func__);
	if ((ret = plant_hid_alloc_urb(drv_data, mx_pkt_sz)) < 0) {
		dev_err(&interface->dev, "%s : alloc urb\n", __func__);
		goto fail1;
	}

	pr_debug("%s : manufacturer = %s\n", __func__, usb_dev->manufacturer);
	if (usb_dev->manufacturer)
                strlcpy(drv_data->name, usb_dev->manufacturer, sizeof(drv_data->name));

	pr_debug("%s : product = %s\n", __func__, usb_dev->product);
        if (usb_dev->product) {
                if (usb_dev->manufacturer)
                        strlcat(drv_data->name, " ", sizeof(drv_data->name));
                strlcat(drv_data->name, usb_dev->product, sizeof(drv_data->name));
        }

        if (!strlen(drv_data->name))
                snprintf(drv_data->name, sizeof(drv_data->name),
                         "USB HIDBP Keyboard %04x:%04x",
                         le16_to_cpu(usb_dev->descriptor.idVendor),
                         le16_to_cpu(usb_dev->descriptor.idProduct));

        usb_make_path(usb_dev, drv_data->phys, sizeof(drv_data->phys));
        strlcat(drv_data->phys, "/input0", sizeof(drv_data->phys));

        input_dev->name = drv_data->name;
        input_dev->phys = drv_data->phys;
        usb_to_input_id(usb_dev, &input_dev->id);
        input_dev->dev.parent = &intf->dev;

        input_set_drvdata(input_dev, drv_data);

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_LED) |
                BIT_MASK(EV_REP);
        input_dev->ledbit[0] = BIT_MASK(LED_NUML) | BIT_MASK(LED_CAPSL) |
                BIT_MASK(LED_SCROLLL) | BIT_MASK(LED_COMPOSE) |
                BIT_MASK(LED_KANA);

	for (i = 0; i < 255; i++)
                set_bit(usb_kbd_keycode[i], input_dev->keybit);
        clear_bit(0, input_dev->keybit);

/*
        input_dev->event = usb_kbd_event;
        input_dev->open = usb_kbd_open;
        input_dev->close = usb_kbd_close;
*/
	pr_debug("%s : assigning drv_data\n", __func__);
	drv_data->intf = interface;
	drv_data->ep = ep;
	drv_data->usb_dev = usb_dev;
	drv_data->input_dev = input_dev;

	if ((drv_data->hid_report_desc = kzalloc(mx_pkt_sz, GFP_KERNEL)) == NULL ) {
		dev_err(&interface->dev, "failed to alloc hid_report_desc\n");
		ret = -ENOMEM;
		goto fail2; 
	}

	if ((plant_hid_get_class_descriptor(usb_dev, cur_set->desc.bInterfaceNumber,
                        HID_DT_REPORT, drv_data->hid_report_desc, mx_pkt_sz)) < 0) {
                dev_err(&interface->dev, "reading report descriptor failed\n");
		ret = -ENOMEM;
		goto fail3;
        }	

	pr_debug("%s : drv_data->hid_report_desc :", __func__); print_hex(drv_data->hid_report_desc);	
	
	usb_fill_int_urb(drv_data->urb, usb_dev, pipe, 
			drv_data->transfer_buffer, mx_pkt_sz, 
			plant_hid_complete, drv_data, ep->bInterval);	

/*
	kbd->irq->transfer_dma = kbd->new_dma;
        kbd->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

        kbd->cr->bRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
        kbd->cr->bRequest = 0x09;
        kbd->cr->wValue = cpu_to_le16(0x200);
        kbd->cr->wIndex = cpu_to_le16(interface->desc.bInterfaceNumber);
        kbd->cr->wLength = cpu_to_le16(1);
*/
	
	usb_submit_urb(drv_data->urb, GFP_KERNEL);
	ret = input_register_device(drv_data->input_dev);
	if (ret)
		goto fail3;

	usb_set_intfdata(intf, drv_data);
	device_set_wakeup_enable(&usb_dev->dev, 1);
	return 0;

fail3:
	if(drv_data->hid_report_desc)
		kfree(drv_data->hid_report_desc);
fail2:
	usb_kill_urb(drv_data->urb);
	plant_hid_free_urb(drv_data);
fail1:
	if (input_dev)
		input_free_device(input_dev);
	if (drv_data)
		kfree(drv_data);
	
	return ret;
	
}

static void plnt_hid_remove(struct usb_interface *intf)
{
	struct plan_driver_data *drv_data =
				usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);
	pr_debug("%s : disconnect is called %p\n", __func__, drv_data);
	if (drv_data) {
		usb_kill_urb(drv_data->urb);
		plant_hid_free_urb(drv_data);
		pr_debug("%s : hid free urb \n", __func__);
		if(drv_data->hid_report_desc)
	                kfree(drv_data->hid_report_desc);
		kfree(drv_data);
	}
}

static struct usb_device_id plnt_hid_device_id[] = {
       // { USB_DEVICE(0x047f, 0xc024) },
	{USB_DEVICE_INTERFACE_CLASS(0x047f, 0xc024, USB_CLASS_HID)},
        {},
};
MODULE_DEVICE_TABLE (usb, plnt_hid_device_id);

static struct usb_driver usb_isoch_driver = {
        .name = "plant_hid",
        .probe = plnt_hid_probe,
        .disconnect = plnt_hid_remove,
        .id_table = plnt_hid_device_id,
};

module_usb_driver(usb_isoch_driver);

