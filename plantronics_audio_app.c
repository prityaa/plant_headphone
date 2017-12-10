
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include "plantronics_audio_out.h"
#include <sys/ioctl.h>


#define DEV_FILE	"/dev/plantronics-0"
#define MP3_TRACK_NAME	"/home/prityaa/music/Tutari_Marathi_Ringtone.mp3"

#ifdef	DEBUG
	#define pr_debug printf
#else 
	#define pr_debug
#endif

int get_settings(usb_settings *settings, int fd) 
{
	int ret = 0;
	if (ioctl(fd, QUERY_GET_SETTINGS, settings) < 0) {
		perror("GET");
		ret = -1;
	}
	
	return ret;
}

int clear_settings(usb_settings *settings, int fd) 
{
	int ret = 0;
	if (ioctl(fd, QUERY_CLR_SETTINGS, settings) < 0) {
		perror("CLEAR");
		ret = -2;
	}
	
	return ret;
}

int set_freq(usb_settings *settings, int fd) 
{
	int ret = 0, freq;
	printf("Please Enter frequency to set on USB : ");
	scanf("%d", &freq);

	settings->freq = freq;	
	
	if (ioctl(fd, QUERY_SET_FREQ, settings) < 0) {
		perror("SET FREQ");
		ret = -3;
	}
	
	return ret;
}

int set_track(usb_settings *settings, int fd) 
{
	int ret = 0;
	char track_name[64];
	printf("Please Enter mp3 track to set on USB : ");
	scanf("%s", track_name);

	strcpy(settings->mp3_track_name, track_name);	
	if (ioctl(fd, QUERY_SET_TRACK, settings) < 0) {
		perror("SET TRACK");
		ret = -4;
	}
	
	return ret;
}

int main(int argc, char *argv[])
{
	int f_desc = -1, ret = -1;
	usb_settings settings;
	char dev_file[32];

	int option;
	memset(&settings, '\0', sizeof(usb_settings));
	memset(dev_file, '\0', sizeof(dev_file));
	(argc == 2 ) ?
		strcpy(dev_file, argv[1]):
 		strcpy(dev_file, DEV_FILE);
	
	settings.freq = 48000;
	strcpy(settings.mp3_track_name, MP3_TRACK_NAME);

	do {	
//		system("reset");
		printf("\nAUDIO OUT\n");
		printf("1 . Get settings\n");
		printf("2 . Clear settings\n");
		printf("3 . Set frequency\n");
		printf("4 . Set Track\n");
		printf("5 . Play Track\n");
		printf("6 . Exit\n");
		printf("Please Enter your option : ");
		scanf("%d", &option);
		
		pr_debug("%s : dev file %s is opening\n",  __func__, dev_file);
		if ((f_desc = open(dev_file, O_WRONLY)) < 0 ) {
			perror("DEVICE OPEN");
			return -1;	
		}

		switch(option) {
			case 1:
				if ((get_settings(&settings, f_desc)) < 0)
					pr_debug("falied to get settings\n");
			break;
			case 2:
				if ((clear_settings(&settings, f_desc)) < 0)
                                        pr_debug("falied to set freq settings\n");
			break;
			case 3:
                                if ((set_freq(&settings, f_desc)) < 0)
                                        pr_debug("falied to clear settings\n");
                        break; 
			case 4:
                                if ((set_track(&settings, f_desc)) < 0)
                                        pr_debug("falied to set track settings\n");
                        break;
			case 5:
				pr_debug("%s : file %s is opening\n",  __func__, settings.mp3_track_name);
				ret = write(f_desc, settings.mp3_track_name, sizeof(settings.mp3_track_name));
				printf("char %d writen\n", ret);
				if (ret < 0) 
					printf("falied to write the mp3\n");
			break;	
		}
		pr_debug("%s : Track name = %s\n", __func__, settings.mp3_track_name);
		pr_debug("%s : Freq = %d\n", __func__, settings.freq);
		close(f_desc);
	} while(option !=6);
	
	return 0;
}

