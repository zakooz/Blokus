all:	blokus-host blokus-httpd

CFLAGS	= -Wall

blokus-host: blokus-host.c
	$(CC) $(CFLAGS) blokus-host.c -o blokus-host

blokus-httpd: blokus-httpd.c
	$(CC) $(CFLAGS) blokus-httpd.c -o blokus-httpd

clean:
	-rm -rf *.o *~ .*~ core blokus-host blokus-httpd *zip

archive:
	-zip blokus-host-`date "+%g%m%d"`.zip Makefile setup_com12.bat *.h *.c blokus.html

