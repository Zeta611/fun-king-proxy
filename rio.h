#ifndef __RIO_H__
#define __RIO_H__

#include <sys/types.h>

/* Persistent state for the robust I/O (Rio) package */
#define RIO_BUFSIZE 8192

typedef struct {
	int rio_fd;		   /* Descriptor for this internal buf */
	int rio_cnt;		   /* Unread bytes in internal buf */
	char *rio_bufptr;	   /* Next unread byte in internal buf */
	char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
} rio_t;

/* Rio (Robust I/O) package */
ssize_t rio_readn(int fd, void *usrbuf, size_t n);
ssize_t rio_writen(int fd, const void *usrbuf, size_t n);
void rio_readinitb(rio_t *rp, int fd);
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n);
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);

#endif /* __RIO_H__ */
