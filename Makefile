
obj-m += plantronics_audio_out.o

# to enable pr_debug
CFLAGS_plantronics_audio_out.o += -DDEBUG 
CFLAGS_plantronics_audio_out.o += -DDEBUG_ALL

KERN_DIR = '/lib/modules/$(uname -r)/'

all :
	gcc -DDEBUG -o plantronics_audio_app plantronics_audio_app.c
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean : 
	rm plantronics_audio_app
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

