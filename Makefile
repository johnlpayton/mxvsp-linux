# 2016-may-14 jlp
#   appears to work fully
#   had to improve irq7 handling
#   had to modify LAPJ to send "call LapjEmptied" when computing new Va
#
# 2016-may-07 Server/client/direct without lapj is working
#
# Direct, no LAPJ partially working
#  gtk error durint downloads (srec)
# I think I'm not using the gui correctly.
# moving to enable remote operation first.
# want to get a remote windows msvsp working
#
CC = gcc


#CFLAGS = -Wformat-y2k			\
#	-DG_DISABLE_DEPRECATED 	 	\
#	-DGDK_DISABLE_DEPRECATED 	\
#	-DGDK_PIXBUF_DISABLE_DEPRECATED \
#	`pkg-config gtk+-3.0 vte-2.90 libglade-2.0 --cflags`

#Gtk-2
#CFLAGS = -DEASYWINDEBUG=1 -DUSEGDK=1 -Wformat=0 `pkg-config gtk+-2.0 vte libglade-2.0  --cflags`
#LKFLAGS = `pkg-config gtk+-2.0 vte libglade-2.0  --libs`

#Gtk-3
CFLAGS = -DEASYWINDEBUG=0 -DUSEGDK=1 -DLAPJDEBUG=1 -DLAPJSTREAM=0\
	-DUSEGDKLOCKS=1 -DUSEPLOCKS=0 -DENABLELAPJ=1 \
	-g \
	-DGLIB_DISABLE_DEPRECATION_WARNINGS \
	-DG_DISABLE_DEPRECATED \
	-DGDK_DISABLE_DEPRECATED \
	-DGDK_PIXBUF_DISABLE_DEPRECATED \
	-DGTK_DISABLE_DEPRECATION_WARNINGS \
	-Wformat=0 `pkg-config gtk+-3.0 vte-2.90 libglade-2.0 --cflags`

LKFLAGS =  `pkg-config gtk+-3.0 vte-2.90 --libs`

#LAPJ_D=../../Lapj160515
LAPJ_D=../../Lapj160521
#LAPJ_D=../../Lapj

LKFILES = vspterm.o \
 vspmenu.o \
 muxpacket.o \
 muxctl.o \
 muxwrte.o \
 framer.o \
 miscutil.o \
 muxsock.o \
 muxlisten.o \
 evbcmds.o \
 lapj_iface.o \
 muxevb.o \
 init.o \
 vdownload.o \
 vupload.o \
 ./Minini/minIni.o \
 $(LAPJ_D)/lapj.a \
 memlockGTK.o


DEPENDS = Makefile \
 jtypes.h \
 framer.h \
 memlock.h \
 muxctl.h \
 muxevb.h \
 muxsock.h \
 muxsock_P.h \
 muxw2ctl.h \
 vdownload.h \
 vspmenu.h \
 vspterm.h \
 $(LAPJ_D)/lapj.h \
 w32defs.h

COMDIR = ../../Common
TFLAGS = -I "../../Common"


all: gmxvsp

cproto: $(LKFILES) cproto.o Makefile
	$(CC) $(LKFILES) cproto.o -o cproto $(LKFLAGS)

gmxvsp: $(LKFILES) gmxvsp.o $(DEPENDS)
	$(CC) $(LKFILES) gmxvsp.o -o gmxvsp $(LKFLAGS)

cproto.o: cproto.c cproto.h $(DEPENDS)
	$(CC) -c cproto.c -o cproto.o  $(CFLAGS)

gmxvsp.o: gmxvsp.c $(DEPENDS)
	$(CC) -c gmxvsp.c -o gmxvsp.o  $(CFLAGS)

vspterm.o: vspterm.c $(DEPENDS)
	$(CC) -c vspterm.c -o vspterm.o  $(CFLAGS)

vspmenu.o:  vspmenu.c $(DEPENDS)
	$(CC) -c vspmenu.c -o vspmenu.o  $(CFLAGS)

framer.o: framer.c $(DEPENDS)
	$(CC) -c framer.c -o framer.o  $(CFLAGS)

muxpacket.o: muxpacket.c $(DEPENDS)
	$(CC) -c muxpacket.c -o muxpacket.o  $(CFLAGS)

memlockGTK.o: memlockGTK.c $(DEPENDS)
	$(CC) -c memlockGTK.c -o memlockGTK.o  $(CFLAGS)

muxctl.o: muxctl.c $(DEPENDS)
	$(CC) -c muxctl.c -o muxctl.o  $(CFLAGS)

conexpr.o: $(COMDIR)/conexpr.c $(DEPENDS)
	$(CC) -c $(COMDIR)/conexpr.c -o conexpr.o $(CFLAGS)

morevte.o: morevte.c $(DEPENDS)
	$(CC) -c morevte.c -o morevte.o $(CFLAGS)

muxwrte.o: muxwrte.c $(DEPENDS)
	$(CC) -c muxwrte.c -o muxwrte.o $(CFLAGS)

miscutil.o: miscutil.c $(DEPENDS)
	$(CC) -c miscutil.c -o miscutil.o $(CFLAGS)

muxsock.o: muxsock.c $(DEPENDS) muxsock_P.h
	$(CC) -c muxsock.c -o muxsock.o $(CFLAGS)

muxlisten.o: muxlisten.c $(DEPENDS) muxsock_P.h
	$(CC) -c muxlisten.c -o muxlisten.o $(CFLAGS)

evbcmds.o: evbcmds.c $(DEPENDS)
	$(CC) -c evbcmds.c -o evbcmds.o $(CFLAGS)

muxevb.o: muxevb.c $(DEPENDS) muxevbW.h
	$(CC) -c muxevb.c -o muxevb.o $(CFLAGS) -I$(LAPJ_D)

lapj_iface.o: lapj_iface.c $(DEPENDS)
	$(CC) -c lapj_iface.c -o lapj_iface.o $(CFLAGS) -I$(LAPJ_D)

init.o: init.c $(DEPENDS)
	$(CC) -c init.c -o init.o $(CFLAGS)

vdownload.o: vdownload.c $(DEPENDS)
	$(CC) -c vdownload.c -o vdownload.o $(CFLAGS)

vupload.o: vupload.c $(DEPENDS)
	$(CC) -c vupload.c -o vupload.o $(CFLAGS)

#./Minini/minIni.o: ./Minini/minIni.c Makefile
#	$(CC) -c ./Minini/minIni.c -o minIni.o $(CFLAGS)

clean:
	rm -f *.o
