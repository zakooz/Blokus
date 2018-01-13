all:	blokus-host blokus-httpd fakefpga

CFLAGS	= -Wall

blokus-host: blokus-host.c
	$(CC) $(CFLAGS) blokus-host.c -o blokus-host

blokus-httpd: blokus-httpd.c
	$(CC) $(CFLAGS) blokus-httpd.c -o blokus-httpd

fakefpga: fakefpga.cpp
	g++ fakefpga.cpp -lpthread -o fakefpga

clean:
	-rm -rf *.o *~ .*~ core blokus-host blokus-httpd *zip

archive:
	-zip blokus-host-`date "+%g%m%d"`.zip Makefile setup_com12.bat *.h *.c blokus.html

