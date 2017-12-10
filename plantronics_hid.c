
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>

#define HUMAN_INTERFACE	3

MODULE_AUTHOR("PRITAM");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HID TEST");

struct plan_driver_data {
	struct usb_interface *intf;
	struct usb_endpoint_descriptor *ep;
	struct usb_device *usb_dev;
	struct urb *urb;
	unsigned char *transfer_buffer;
};

static void plant_hid_complete(struct urb *urb)
{
	pr_debug("%s : calling complete\n", __func__);
	if ((urb->status == -ENOENT) ||         /* unlinked */
            (urb->status == -ENODEV) ||         /* device removed */
            (urb->status == -ECONNRESET) ||     /* unlinked */
            (urb->status == -ESHUTDOWN))        /* device disabled */
                goto exit_clear;
        return;
	
	pr_debug("%s : buffer = %s\n", __func__, (char *)urb->transfer_buffer);

exit_clear:
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

static int plnt_hid_probe(struct usb_interface *intf, 
				const struct usb_device_id *ids)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct usb_interface *interface = intf;
	struct plan_driver_data *drv_data;
	struct usb_endpoint_descriptor *ep;
	struct usb_host_interface *cur_set = intf->cur_altsetting;
	struct urb *urb;
	int ret, pipe, mx_pkt_sz;
	
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
#if 1 
	pr_debug("%s : ep = %p\n", __func__, ep);
	if(ep == NULL) {
		dev_err(&interface->dev, "ep not found\n");	
		return -ENODEV;
	}

	drv_data = kzalloc(sizeof(struct plan_driver_data), GFP_KERNEL);
	if (drv_data == NULL) {
		dev_err(&interface->dev, "%s : mem not available\n", __func__);
		return -ENOMEM;
	}

	pr_debug("%s : allocing urb\n", __func__);
	if ((ret = plant_hid_alloc_urb(drv_data, mx_pkt_sz)) < 0) {
		dev_err(&interface->dev, "%s : alloc urb\n", __func__);
		kfree(drv_data);
                return -ENOMEM;
	}

	pr_debug("%s : assigning drv_data\n", __func__);
	drv_data->intf = interface;
	drv_data->ep = ep;
	drv_data->usb_dev = usb_dev;
	
	pr_debug("%s : buffer = %p\n", __func__, drv_data->transfer_buffer);
	usb_fill_int_urb(urb, usb_dev, pipe, 
			drv_data->transfer_buffer, mx_pkt_sz, 
			plant_hid_complete, drv_data, ep->bInterval);	
#endif
	usb_set_intfdata(intf, drv_data);
	usb_submit_urb(urb, GFP_KERNEL);

	return 0;
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

