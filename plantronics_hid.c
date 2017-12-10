
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

MODULE_AUTHOR("PRITAM");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HID TEST");

static int plnt_hid_probe(struct usb_interface *intf, 
				const struct usb_device_id *ids)
{
	pr_debug("%s : probe is called \n", __func__);
	return 0;
}

static void plnt_hid_remove(struct usb_interface *intf)
{
	pr_debug("%s : disconnect is called \n", __func__);
}

static const struct usb_device_id plnt_hid_device_id[] = {
	//{ USB_DEVICE_INTERFACE_CLASS(0x047f, 0xc024, USB_CLASS_HID) }, 
	{ USB_DEVICE(0x047f, 0xc024) }, 
	{}
};
MODULE_DEVICE_TABLE(usb, plnt_hid_device_id);

static struct usb_driver plantronics_usb_hid_driver = {
	.name = "plnt_hid",
	.probe = plnt_hid_probe,
	.disconnect = plnt_hid_remove,
	.id_table = plnt_hid_device_id,
};

module_usb_driver(plantronics_usb_hid_driver);
