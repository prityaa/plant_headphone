
//#define DEBUG
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "plantronics_audio_out.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pritam");
MODULE_DESCRIPTION("isoch test audio");

#define MIN(a,b) (((a) <= (b)) ? (a) : (b))
#define INTERFACE_AUDIO_CLASS	1
#define INTERFACE_CNTL_SUBCLASS	1
#define INTERFACE_STREM_SUBCLASS	2	
#define EP_OUT_ADDR	0x1	
#define EP_IN_ADDR	0x81
#define DEFAULT_FREQ	48000

#define NR_PACKETS_PER_OUT_URB	6
#define ISOC_FRAME_LEN	192 
#define AUDIO_FILE_SIZE	4800000 
#define PLAYING	1
#define NOT_PLAYING	0

static struct usb_driver usb_isoch_driver;

#ifdef DEBUG_ALL
	#define pr_debug_all	printk
#else
	#define pr_debug_all
#endif

unsigned char audio_data[AUDIO_FILE_SIZE];

struct usb_audio_data {
	struct usb_interface *intf;
	struct urb *urb;
	struct usb_device *usb_dev;
	struct usb_class_driver *class;
	struct usb_endpoint_descriptor *ep; 
	spinlock_t spin_lock;
	wait_queue_head_t aud_out_wq;
	struct semaphore audio_out_sem;
	struct kref kref;
	usb_settings *settings;
	int play_flag;
};

static void usb_audio_out_complete(struct urb *urb)
{
	static int offset;
	unsigned long spn_lck_flgs;
	struct usb_audio_data *usb_data = urb->context;
        int err, j, offs = 0;
	pr_debug_all("usb_isoch_complete : urb status = %d\n", 
			urb->status);
	switch (urb->status) {
                case 0:                 /* success */
                        break;
                case -ECONNRESET:       /* unlink */
                case -ENOENT:
                case -ESHUTDOWN:
                        return;
                        
			/* -EPIPE:  should clear the halt */
                default:                /* error */
                        goto resubmit;
        }

resubmit:
	pr_debug_all("%s : rebumitting offset = %d\n", __func__, offset);
        if (offset >= AUDIO_FILE_SIZE) {
		
		spin_lock_irqsave(&usb_data->spin_lock, spn_lck_flgs);
		usb_data->play_flag = NOT_PLAYING;
		spin_unlock_irqrestore(&usb_data->spin_lock, spn_lck_flgs);
		
		wake_up_interruptible(&usb_data->aud_out_wq);
                pr_info("completely read the input file\n");
                offset = 0;
                return;
        }

	spin_lock_irqsave(&usb_data->spin_lock, spn_lck_flgs);
	usb_data->play_flag = PLAYING;
	spin_unlock_irqrestore(&usb_data->spin_lock, spn_lck_flgs);

        memcpy(urb->transfer_buffer, audio_data + offset,
                        (ISOC_FRAME_LEN * NR_PACKETS_PER_OUT_URB));
        for (j = 0; j < NR_PACKETS_PER_OUT_URB; j++) {
                urb->iso_frame_desc[j].offset = offs;
                urb->iso_frame_desc[j].length = ISOC_FRAME_LEN;
                offs += ISOC_FRAME_LEN;
        }

        urb->transfer_buffer_length = 
			ISOC_FRAME_LEN * NR_PACKETS_PER_OUT_URB;
        offset += (ISOC_FRAME_LEN * NR_PACKETS_PER_OUT_URB);
        err = usb_submit_urb(urb, GFP_ATOMIC);
        if (err < 0) {
                pr_err("%s() usb_submit_urb failed for audio out (%d)\n",
                         __func__, err);
        }
	pr_debug("%s : play_flag = %d\n", __func__, usb_data->play_flag);
}

static int set_get_freq_audio_out(
			struct usb_audio_data *aud_data, int set_freq)
{
	int err, data[3], ret = -1;
        unsigned int get_freq = 0;
        unsigned short ep_addr;
	struct usb_device *usb_dev;
        data[0] = set_freq;
        data[1] = set_freq >> 8;
        data[2] = set_freq >> 16;
	pr_debug("%s : init freq = %d\n", __func__, set_freq);
	if (aud_data ) {
		ep_addr = aud_data->ep->bEndpointAddress;
		pr_debug("%s : ep_addr = %d\n", __func__, ep_addr);
		usb_dev = aud_data->usb_dev;
		if ((err = usb_control_msg(usb_dev, 
				usb_sndctrlpipe(usb_dev, 0),
				UAC_SET_CUR,
				(USB_TYPE_CLASS | 
					USB_RECIP_ENDPOINT | USB_DIR_OUT),
				UAC_EP_CS_ATTR_SAMPLE_RATE << 8, 
				ep_addr, data, sizeof(data), 
				USB_CTRL_SET_TIMEOUT)) < 0) {
			pr_err("cannot set freq %d to ep %#x\n", 
				set_freq, ep_addr);
			return -EIO;
		}

		if ((err = usb_control_msg(usb_dev, 
				usb_rcvctrlpipe(usb_dev, 0),
				UAC_GET_CUR,
				(USB_TYPE_CLASS | 
					USB_RECIP_ENDPOINT | USB_DIR_IN),
				UAC_EP_CS_ATTR_SAMPLE_RATE << 8, 
				ep_addr, data, sizeof(data), 
				USB_CTRL_GET_TIMEOUT) ) < 0) {
			pr_err("cannot get freq at ep %#x\n", ep_addr);
			return -EIO;
		}

		get_freq = data[0] | (data[1] << 8) | (data[2] << 16);
		if (get_freq != set_freq) {
			pr_err("get freq %d and set freq %d\n",
				get_freq, set_freq);
			return -EIO;
		} else {
			dev_info(&aud_data->intf->dev, 
				"sample rate %d set successfully\n", 
				set_freq);
			aud_data->settings->freq = get_freq;
		}
		ret = get_freq;
	}
	
	return ret;
}

static int init_transefer_buff(struct usb_audio_data *usb_data, 
					const char *mp3_track_name)
{
	struct file *file = NULL;
	struct usb_endpoint_descriptor *ep = usb_data->ep;
	int urb_max_pac_size = ep->wMaxPacketSize;
	int ret;

	pr_debug("%s : ep = %p, usb_data = %p\n", __func__, ep, usb_data);
	if ((ep == NULL) || (usb_data == NULL)) {
		dev_err(&usb_data->intf->dev, 
			"%s : Device not present\n", __func__);
		return -ENXIO;
	}	

	pr_debug("%s : track_name = %s\n", __func__, mp3_track_name);
	file = filp_open(mp3_track_name, O_RDONLY, 0644);
        if (IS_ERR(file)) {
                dev_err(&usb_data->intf->dev, "%s open failed\n", 
					mp3_track_name);
                return -ENOENT;
        }

	pr_debug("%s : urb_max_pac_size = %d\n", 
		__func__, urb_max_pac_size);
	memset(audio_data, '\0', AUDIO_FILE_SIZE);

	ret = kernel_read(file, 0, audio_data, AUDIO_FILE_SIZE);
        filp_close(file, NULL);
	return ret;
}

static int usb_isoch_open(struct inode *inode, struct file *file)
{
	int ret = 0, minor = 0;
        struct usb_interface *iface = NULL;
        struct usb_audio_data *usb_data = NULL;

	pr_debug("usb_isoch_open : open\n");
	
	/* read minor from inode */
        minor = iminor(inode);
	pr_info("%s : minor = %d\n", __func__, minor);
        
	/* fetch device from usb interface */
        iface = usb_find_interface(&usb_isoch_driver, minor);
        if (iface == NULL) {
                pr_err("%s: can't find device for minor %d\n",
                       __func__, minor);
                ret = -ENODEV;
                goto exit;
        }

        /* get our device */
        usb_data = usb_get_intfdata(iface);
        if (usb_data == NULL) {
                ret = -EFAULT;
                goto exit;
        }

        pr_info("%s : manufacturer = %s, product = %s\n", 
                __func__, usb_data->usb_dev->manufacturer, 
		usb_data->usb_dev->product);	
	
	if(down_interruptible(&usb_data->audio_out_sem) != 0) {
		dev_info(&usb_data->intf->dev, 
			"%s : device has been opened by other device\n",
			__func__);
		return -1;
	}

        /* save our device object in the file's private structure */
        file->private_data = usb_data;
	
	/* increment our usage count for the device */
//        kref_get(&usb_data->kref);
exit:	
	return ret;
}

/*
static void usb_isoch_release(struct kref *kref)
{
        struct usb_isoch *usb_data;

        usb_data = container_of(kref, struct usb_isoch, kref);
        if (usb_data != NULL) {
                usb_put_dev(usb_data->usb_dev);
                kfree(usb_data);
        }
}
*/

static int usb_isoch_close(struct inode *inode, struct file *file)
{
	struct usb_audio_data *usb_data = NULL;
	pr_debug("usb_isoch_close : close\n");
        usb_data = file->private_data;
	up(&usb_data->audio_out_sem);
#if 0
        if (usb_data != NULL) {
                /* decrement the count on our device */
                kref_put(&usb_data->kref, usb_isoch_release);
        }
#endif
	return 0;
}

static int play_mp3_song(struct usb_audio_data *play_usb_data, 
					const char *mp3_track_name) 
{
	struct urb *play_urb = play_usb_data->urb;
	struct usb_interface *play_intf = play_usb_data->intf;
	struct usb_device *usb_dev = 
			interface_to_usbdev(play_usb_data->intf);
	int ret, j, offs = 0, ret_bytes;

	pr_debug("%s : usb_audio_data = %p\n", __func__, play_usb_data);
	if (!play_usb_data) {
		pr_err("%s : device not present\n", __func__);
		return -ENXIO;
	}

        /*opening mp3 file and filling in transefer_buffer */
        ret_bytes = init_transefer_buff(play_usb_data, mp3_track_name) ;
	pr_debug("%s : kernel_read ret = %d\n", __func__, ret_bytes);
	if (ret_bytes <= 0) {
                dev_err(&play_usb_data->intf->dev, 
				"falied to init transefer buffer\n");
                return -ENOENT;
        }

        pr_info("%s : audio.usbdev = %p\n", 
		__func__, play_usb_data->usb_dev);
        pr_info("%s : ep = %p\n", __func__, play_usb_data->ep);

	pr_debug("%s : setting intf 2 and alt 1 intf\n", __func__);
	ret = usb_set_interface(usb_dev, 2, 1);
        if (ret != 0) {
                dev_err(&play_intf->dev, 
			"Cant set first iface for the dev\n");
                return -EIO;
        }

	for (j = 0; j < NR_PACKETS_PER_OUT_URB; j++) {
                play_urb->iso_frame_desc[j].offset = offs;
                play_urb->iso_frame_desc[j].length = ISOC_FRAME_LEN;
                offs += ISOC_FRAME_LEN;
        }
        play_urb->transfer_buffer_length = 
		ISOC_FRAME_LEN * NR_PACKETS_PER_OUT_URB;
	memcpy(play_urb->transfer_buffer, audio_data, 
		(ISOC_FRAME_LEN * NR_PACKETS_PER_OUT_URB));

//	msleep(500);
	if ((ret = usb_submit_urb(play_urb, GFP_ATOMIC))) {
		dev_info(&play_usb_data->intf->dev, 
			"%s : failed to submit urb ret = %d\n",
			__func__, ret);
		return ret; 
	}
	return ret_bytes;
}

static ssize_t usb_isoch_write(struct file *file,
			 const char __user *buf, size_t cnt, loff_t *off)
{
	int retval;
	struct usb_audio_data *write_usb_data = file->private_data;
	struct usb_device *write_usb_dev = write_usb_data->usb_dev;
	usb_settings *settings = write_usb_data->settings;

	
	pr_info("%s : manufacturer = %s, product = %s\n", __func__, 
		write_usb_dev->manufacturer, write_usb_dev->product);
	
	pr_debug("%s : mp3_track_name = %s\n", 
		__func__, settings->mp3_track_name);
	if (copy_from_user(settings->mp3_track_name, buf, 
					MIN(cnt, AUDIO_FILE_SIZE))) {
		dev_err(&write_usb_data->intf->dev, 
				"failed to copy_to_user\n");
		return -EFAULT;
	}

	retval = play_mp3_song(write_usb_data, settings->mp3_track_name); 
	pr_debug("%s : retval = %d\n", __func__, retval);

	pr_debug("%s : play_flag = %d\n", __func__, 
		write_usb_data->play_flag);
	wait_event_interruptible(write_usb_data->aud_out_wq, 
		write_usb_data->play_flag != PLAYING);
	spin_lock(&write_usb_data->spin_lock);
	write_usb_data->play_flag = PLAYING;
	spin_unlock(&write_usb_data->spin_lock);
	
	return retval;
}

static ssize_t usb_isoch_read(struct file *file, char __user *buf, 
					size_t cnt, loff_t *off)
{
	int wrote_cnt = MIN(cnt, AUDIO_FILE_SIZE);
	struct usb_audio_data *read_usb_data = file->private_data;
        struct usb_device *read_usb_dev = read_usb_data->usb_dev;

        pr_info("%s : manufacturer = %s, product = %s\n", __func__, 
		read_usb_dev->manufacturer, read_usb_dev->product);

	return wrote_cnt;
}

static long usb_isoch_ioctl(struct file *file, 
				unsigned int cmd, unsigned long arg)
{
	int ret;
	struct usb_audio_data *ioctl_data = file->private_data;
	usb_settings *settings = ioctl_data->settings;
	usb_settings local_settings;

	pr_debug("%s : cmd = 0X%x\n", __func__, cmd); 
	switch (cmd) {
		case QUERY_GET_SETTINGS:
			pr_debug("%s : get seting: freq = %d,track =%s\n",
				__func__, settings->freq, 
				settings->mp3_track_name);
			memcpy(&local_settings, 
				settings, sizeof(usb_settings));
			if (copy_to_user((usb_settings *)arg, 
				&local_settings, sizeof(usb_settings)))
				return -ENXIO;
		break;
		case QUERY_CLR_SETTINGS:
			memset(&local_settings,'\0',sizeof(usb_settings));
			pr_debug("%s : clr seting: freq =%d, track =%s\n",
				__func__, settings->freq, 
				settings->mp3_track_name);
			if ((ret = set_get_freq_audio_out(ioctl_data, 
						DEFAULT_FREQ)) < 0) {
                                dev_err(&ioctl_data->intf->dev,
					"%s : failed to get freq\n", 
					__func__);
                                return -ENXIO;
                        }
			local_settings.freq = DEFAULT_FREQ;
			settings = &local_settings;
			if (copy_to_user((usb_settings *)arg, 
				&local_settings, sizeof(usb_settings)))
				return -EACCES;
		break;
		case QUERY_SET_FREQ:
			if(copy_from_user(&local_settings, 
				(usb_settings *)arg, sizeof(usb_settings)))
			 	return -EACCES;
			settings = &local_settings;
                        pr_debug("%s : set freq : freq = %d, track =%s\n",
                                __func__, settings->freq, 
				settings->mp3_track_name);
			if ((ret = set_get_freq_audio_out(ioctl_data, 
					local_settings.freq)) < 0) {
                                dev_err(&ioctl_data->intf->dev,
				"%s : failed to get freq\n", __func__);
                                return -ENXIO;
                       } 
		break;
		case QUERY_SET_TRACK:
			if(copy_from_user(&local_settings, 
				(usb_settings *)arg, sizeof(usb_settings)))
				return -EACCES;
			pr_debug("%s : set track: freq = %d, track =%s\n",
				__func__, settings->freq, 
				settings->mp3_track_name);
			pr_debug("%s : set : local freq =%d, track =%s\n",
				__func__, local_settings.freq, 
				local_settings.mp3_track_name);
			strcpy(settings->mp3_track_name, 
				local_settings.mp3_track_name);
			settings = &local_settings;
                break;

		default:
			return -EINVAL;
	}

	return 0;
}

static struct file_operations isoch_dev_fops =
{
	.owner = THIS_MODULE,
	.open = usb_isoch_open,
	.release = usb_isoch_close,
	.read = usb_isoch_read,
	.write = usb_isoch_write,
	.unlocked_ioctl = usb_isoch_ioctl,
};

static struct usb_class_driver usb_class_driver = {
        .name           = "plantronics-%d",
        .fops           = &isoch_dev_fops,
        .minor_base     = 245,
};

static int parse_audio_data(struct usb_interface *intf, 
        unsigned short *max_size)
{

        struct usb_host_endpoint *ep;
        struct usb_host_interface *host_intf;
        int i, type = -EINVAL;
	int intf_cls, intf_subcls, altset, size;
        unsigned char ep_addr = 0, nr_ep = 0;

        *max_size = 0;
	host_intf = &intf->altsetting[0];
	intf_cls = host_intf->desc.bInterfaceClass;
	intf_subcls = host_intf->desc.bInterfaceSubClass;

	pr_debug("%s : intf_cls = %d, intf_subcls = %d",
			 __func__, intf_cls, intf_subcls);

	/* verifying that class is audio */
	if ( (intf_cls != INTERFACE_AUDIO_CLASS) || 
		(intf_subcls == INTERFACE_CNTL_SUBCLASS) ) {
		pr_debug("%s : Not a audio class \n", __func__);
		return -EINVAL;		
	}

	altset = intf->num_altsetting;
	pr_debug("%s : Number of alt setting = %d\n", __func__, altset);
        for (i = 0; i < altset; i++) {
                host_intf = &intf->altsetting[i];
                nr_ep = host_intf->desc.bNumEndpoints;
		pr_debug("%s : nr_ep[%d] = %d\n", __func__, i, nr_ep);
                if (nr_ep) {
                        ep = &host_intf->endpoint[0];
                        ep_addr = ep->desc.bEndpointAddress;
                        size = ep->desc.wMaxPacketSize;
			pr_debug("%s : ep_addr[%d] = 0x%x, size = %d\n", 
				__func__, i, ep_addr, size);
                        if ((ep_addr) && (*max_size < size)) {
                                *max_size = size;
                                if (ep_addr & USB_DIR_IN) {
                                        pr_info("%s : Audio in endpoint = %x\n",
                                                __func__, ep_addr);
                                        type = 1;
				} else {
                                        pr_info("%s : Audio out endpoint = %x\n",
                                                __func__, ep_addr);
                                        type = 0;
					break;
				}
			}
		}
        }

        pr_debug("%s : max packet size = %d, type = %d\n", 
			__func__, *max_size, type);
        return type;
}

static void free_audio_out_urb(struct usb_audio_data *aud_data)
{
	int err = 0;
	struct urb *urb;
	urb = aud_data->urb;
	
	pr_debug("%s : urb = %p\n", __func__, urb);	
	if (urb) {
		if (urb->status)
			err = usb_unlink_urb(urb);
		if ((err) && (err != -EINPROGRESS)) {
			pr_err("%s : urb unlinked failed  err = %d",
					__func__, err);
		}
		if (urb->transfer_buffer)
			kfree(urb->transfer_buffer);
		usb_free_urb(urb);
	}
}

static int init_audio_out_urb(struct usb_audio_data *init_aud_data) 
{
	struct urb *urb = NULL;
	struct device *dev = &init_aud_data->intf->dev;
	struct usb_device *usb_dev = init_aud_data->usb_dev;
	struct usb_endpoint_descriptor *ep = init_aud_data->ep; 
	int ep_addr = ep->bEndpointAddress;

	int packets = NR_PACKETS_PER_OUT_URB;
	int buf_size, i = 0;

	pr_debug("%s : ep = %p\n", __func__, ep);
	if (ep == NULL) {
		dev_err(dev, "%s : ep is null \n", __func__);
		return -ENOMEM;
	}
	
	buf_size = ep->wMaxPacketSize * NR_PACKETS_PER_OUT_URB;
	pr_debug("%s : buf_size = %x, ep_addr = 0X%x, intval = %x\n", 
		__func__, buf_size, ep_addr, ep->bInterval);

	pr_debug("%s : initing urb[%d] \n", __func__, i);
	if ((urb = usb_alloc_urb(packets, GFP_ATOMIC)) == NULL) {
		dev_dbg(dev, "%s : failed to alloc urb\n",
				__func__);
		return -ENOMEM;
	}

	if ( (urb->transfer_buffer = 
		kzalloc(buf_size, GFP_ATOMIC | __GFP_NOWARN )) == NULL) {
		usb_free_urb(urb);
		dev_err(dev, "%s : falied to alloc buffer\n", 
				__func__);
		return -ENOMEM;
	}
	urb->pipe = usb_sndisocpipe(usb_dev, ep_addr);	

	pr_debug("%s : pipe = %d, max_packet = %d\n", __func__,
			usb_sndisocpipe(usb_dev, ep_addr),
			usb_maxpacket(usb_dev, 
				urb->pipe, usb_pipeout(urb->pipe)));

	urb->interval = ep->bInterval;
	urb->transfer_flags = URB_ISO_ASAP;
	urb->dev = init_aud_data->usb_dev;
	urb->transfer_buffer_length = buf_size * packets;
	urb->number_of_packets = packets;
	urb->context = init_aud_data;
	urb->complete = usb_audio_out_complete;
	init_aud_data->urb = urb;
	pr_debug("%s : urb's initiated\n", __func__);

	return 0;
}

static int init_usb_settings(struct usb_audio_data *set_data, 
						usb_settings *settings)
{
	int ret;
	settings = kzalloc(sizeof(usb_settings), GFP_KERNEL);
	if (!settings) {
		dev_err(&set_data->intf->dev, 
			"%s : failed to alloc for settings\n", __func__);
		return -ENOMEM;
	}
	
	settings->freq = DEFAULT_FREQ;
	set_data->settings = settings;

	if ((ret = set_get_freq_audio_out(set_data, DEFAULT_FREQ)) < 0) {
		dev_err(&set_data->intf->dev, 
			"%s : failed to set freq\n", __func__);
		return -EIO;
	}
	return 0;	
}

/* probe callback */
static int usb_isoch_probe(struct usb_interface *intf, 
					const struct usb_device_id *ids)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
        struct usb_host_interface *interface;
        struct usb_endpoint_descriptor *endpoint;
	usb_settings *settings;
	
	struct usb_audio_data *aud_data;
	int ret;
	unsigned short maxp;

	if (intf == NULL) {
		printk("Empty intf\n");
		return -EINVAL;
	}
	interface = intf->cur_altsetting;

	pr_debug("%s : setting up interface\n", __func__);
	ret = usb_set_interface(usb_dev, 2, 1);
        if (ret != 0) {
                dev_err(&intf->dev, "Cant set first iface for the dev\n");
                return -EIO;
        }

	pr_debug("%s : interface->desc.bNumEndpoints = %d\n", 
			__func__, interface->desc.bNumEndpoints);
	endpoint = &interface->endpoint[0].desc;
#if 0 
        pr_info("%s : device = %p", __func__, usb_dev);
        pr_info("%s : ep = %p \n", __func__, endpoint);
	if (endpoint == NULL) {
		printk("Empty endpoint\n");
                return -EINVAL;
	}
#endif
	/* 
	   if set (0, 0) it will reset device 
	   so intf and usb data will be null 
	   so sertting (2, 1), as audio out 
	*/

	pr_debug("%s : setting intf 2 and alt 1 intf usb_dev = %p\n", 
		__func__, usb_dev);
	if(usb_dev == NULL) {
		printk("Empty usb_dev\n");
		return -EINVAL;	
	}

	pr_debug("%s : allcing driver data\n", __func__);
	aud_data = kzalloc(sizeof(struct usb_audio_data), GFP_KERNEL);
	if (aud_data == NULL) {
		dev_err(&intf->dev, "falied to alloc mem\n");
		return -ENOMEM;	
	}
	
	pr_debug("%s : parsing audio interface\n", __func__);
	ret = parse_audio_data(intf, &maxp);
	if (ret) {
		dev_err(&intf->dev, "falied to get AUDIO out device\n");
		ret = -EINVAL;
		goto fail1;
	}
	
	//aud_data->transfer_buffer = transfer_buffer;
	aud_data->usb_dev = usb_dev;
	aud_data->ep = endpoint;
	aud_data->intf = intf;
	aud_data->class = &usb_class_driver;
	aud_data->play_flag = PLAYING; 

	ret = init_audio_out_urb(aud_data);
	pr_debug("%s : init urb ret = %d\n", __func__, ret);
	if (ret < 0) {
		dev_err(&intf->dev, "%s : failed to init urb\n", 
			__func__);
		ret = -ENOMEM;
		goto fail1;
	}
	
	ret = init_usb_settings(aud_data, settings);
	pr_debug("%s : init usb settings ret = %d\n", __func__, ret);
	if (ret < 0) {
		dev_err(&intf->dev, "falied to init usb settings\n");
		goto fail2;
	}

	if ((ret = usb_register_dev(intf, &usb_class_driver)) < 0) {
                dev_err(&intf->dev, "%s : falied to get minor for dev\n", 
			__func__);
		ret = -EIO;
		goto fail3;
        }

	usb_set_intfdata(intf, aud_data);
	dev_info(&intf->dev, "%s : Product %s is registered\n", 
		usb_dev->manufacturer, usb_dev->product);

	spin_lock_init(&aud_data->spin_lock);
	sema_init(&aud_data->audio_out_sem,1);
	init_waitqueue_head(&aud_data->aud_out_wq);

	return 0;

fail3:
	if (aud_data->settings);
		kfree(aud_data->settings);
fail2:
	if(aud_data)
		free_audio_out_urb(aud_data);
fail1:
	if (aud_data)
		kfree(aud_data);
	return ret;
}

static void usb_isoch_remove(struct usb_interface *intf)
{
	struct usb_audio_data *aud_data =
				usb_get_intfdata(intf);
	dev_info(&intf->dev, "usb isoch is disconnected successfully\n");
	usb_set_intfdata(intf, NULL);
	usb_deregister_dev(intf, &usb_class_driver);
	if (aud_data) {
		pr_info("%s : freeing buffers\n", __func__);
		if (aud_data->settings);
	                kfree(aud_data->settings);
		free_audio_out_urb(aud_data);
		if (aud_data)
			kfree(aud_data);	
	}
	pr_debug("%s : usb is disconnected\n", __func__);
}

static struct usb_device_id usb_device_ids[] = {
	{ USB_DEVICE(0x047f, 0xc024) },
	{}, 
};
MODULE_DEVICE_TABLE (usb, usb_device_ids);

static struct usb_driver usb_isoch_driver = {
	.name = "plantronics_audio_maintenance",
	.probe = usb_isoch_probe,
	.disconnect = usb_isoch_remove,
	.id_table = usb_device_ids,
};

module_usb_driver(usb_isoch_driver);
