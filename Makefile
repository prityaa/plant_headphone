
obj-m += plantronics_hid.o

# to enable pr_debug
FLAGS_plantronics_audio_out.o += -DDEBUG 
CFLAGS_plantronics_audio_out.o += -DDEBUG_ALL

KERN_DIR = '/lib/modules/$(uname -r)/'

all :
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean : 
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
