#ifndef QUERY_IOCTL_H
#define QUERY_IOCTL_H
#include <linux/ioctl.h>

typedef struct
{
	int freq;
	char mp3_track_name[64];
} usb_settings;
 
#define QUERY_GET_SETTINGS _IOR('q', 1, usb_settings *)
//#define QUERY_CLR_SETTINGS _IO('q', 2)
#define QUERY_CLR_SETTINGS _IOW('q', 2, usb_settings *)
#define QUERY_SET_FREQ _IOW('q', 3, usb_settings *)
#define QUERY_SET_TRACK _IOW('q', 4, usb_settings *)
    
#endif
