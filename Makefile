# Makefile for Proxy Lab 
#
# You may modify this file any way you like (except for the handin
# rule). You instructor will type "make" on your specific Makefile to
# build your proxy from sources.

CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -lpthread

all: proxy

rio.o: rio.c rio.h
	$(CC) $(CFLAGS) -c rio.c

utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -c utils.c

cache.o: cache.c utils.c utils.h
	$(CC) $(CFLAGS) -c cache.c

proxy.o: proxy.c rio.h utils.h cache.h
	$(CC) $(CFLAGS) -c proxy.c

proxy: proxy.o rio.o utils.o cache.o
	$(CC) $(CFLAGS) proxy.o rio.o utils.o cache.o -o proxy $(LDFLAGS)

# Creates a tarball in ../proxylab-handin.tar that you can then
# hand in. DO NOT MODIFY THIS!
handin:
	(make clean; cd ..; tar cvzf assign7.tar.gz -X proxylab-handout/exclude.lst proxylab-handout)

clean:
	rm -f *~ *.o proxy core *.tar *.zip *.gzip *.bzip *.gz

