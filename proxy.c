#define _BSD_SOURCE /* Get NI_MAXHOST & NI_MAXSERV from <netdb.h> */
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cache.h"
#include "rio.h"
#include "utils.h"

/* Recommended max cache sizes */
#define MAXLINE 8192 /* Max text line length */
#define MAXBUF 8192  /* Max I/O buffer size */
#define ADDRSTRLEN (NI_MAXHOST + NI_MAXSERV + 10)

#define RIO_WRITEN(FD, BUF, N)                                                 \
	do {                                                                   \
		const size_t n = (N);                                          \
		if (rio_writen(FD, BUF, n) != n) {                             \
			msg_unix_error("rio_writen");                          \
			return -1;                                             \
		};                                                             \
	} while (0);

void forward(int confd);
int clienterror(int fd, char *cause, char *errnum, char *shortmsg,
		char *longmsg);
int forward_requesthdrs(rio_t *rp, int clifd, const char *host);
int serve_static(int fd, const char *filename, int filesize);
void get_filetype(const char *filename, char *filetype);
int parse_uri(char *uri, char *host, char *service, char *path);
void *thread(void *vargp);

struct cache cache;

int main(int argc, char **argv)
{
	/* Check command line args */
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		unix_error("signal");
	}

	cache = Make_cache();

	pthread_t tid;
	const int lisfd = Open_listenfd(argv[1]);

	while (1) {
		/* Create a connection */
		socklen_t addrlen = sizeof(struct sockaddr_storage);
		struct sockaddr_storage caddr;
		int *confd = malloc(sizeof(int));
		if (confd == NULL) {
			/* malloc failed! Maybe crashing is a good idea... */
			msg_unix_error("malloc");
			continue;
		}
		*confd = accept(lisfd, (SA *)&caddr, &addrlen);
		if (*confd < 0) {
			/* Accept failed; continue on the next client attempt */
			msg_unix_error("accept");
			continue;
		}

		char host[NI_MAXHOST];
		char service[NI_MAXSERV];
		int rc = getnameinfo((SA *)&caddr, addrlen, host, NI_MAXHOST,
				     service, NI_MAXSERV, 0);
		char addr_str[ADDRSTRLEN];
		if (rc == 0) {
			snprintf(addr_str, ADDRSTRLEN, "(%s, %s)", host,
				 service);
		} else {
			// Log on stderr but proceed
			msg_gai_error(rc, "getnameinfo");
			snprintf(addr_str, ADDRSTRLEN, "(UNKNOWN)");
		}
		printf("Accepted connection from %s\n", addr_str);

		rc = pthread_create(&tid, NULL, thread, confd);
		if (rc) {
			msg_posix_error(rc, "pthread_create");
			if (close(*confd) < 0) {
				msg_unix_error("close");
			}
		}
	}
}

/*
 * thread routine
 */
void *thread(void *vargp)
{
	const int confd = *((int *)vargp);

	const int rc = pthread_detach(pthread_self());
	free(vargp);
	if (rc) {
		msg_posix_error(rc, "pthread_detach");
		goto cleanup;
	}

	forward(confd);

cleanup:
	if (close(confd) < 0) {
		msg_unix_error("close");
	}

	return NULL;
}

/*
 * forward - forward one HTTP request/response transaction
 */
void forward(int confd)
{
	/* Read request line and headers */
	char buf[2 * MAXLINE + 15];
	rio_t conrio;
	rio_readinitb(&conrio, confd);
	ssize_t rc = rio_readlineb(&conrio, buf, MAXLINE);
	if (rc == 0) {
		return;
	} else if (rc < 0) {
		msg_unix_error("rio_readlineb");
		return;
	}
	printf("Request headers:\n%s", buf);

	char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	sscanf(buf, "%s %s %s", method, uri, version);
	if (strcasecmp(method, "CONNECT") == 0) {
		// Establish connection
		static const char conn_estab[] =
		    "HTTP/1.0 200 Connection Established\r\n\r\n";
		const size_t len = sizeof conn_estab - 1;
		if (rio_writen(confd, conn_estab, len) != len) {
			msg_unix_error("rio_writen");
		}
		return;
	} else if (strcasecmp(method, "GET")) {
		clienterror(confd, method, "501", "Not Implemented",
			    "Proxy does not implement this method");
		return;
	}

	/* Check cache */
	size_t item_size;
	if (get_cache(&cache, uri, (void *)buf, &item_size) == 0) {
		puts("DEBUG: $ hit!");
		if (rio_writen(confd, buf, item_size) != item_size) {
			msg_unix_error("rio_writen");
		}
		return;
	}

	char host[NI_MAXHOST];
	char service[NI_MAXSERV];
	char path[MAXLINE];
	char uri_cpy[MAXLINE];
	strcpy(uri_cpy, uri);
	parse_uri(uri_cpy, host, service, path);

	printf("DEBUG: %s %s %s\n", host, service, path);

	/* Connect to the end server */
	const int clifd = open_clientfd(host, service);
	if (clifd < 0) {
		return;
	}

	rio_t clirio;
	rio_readinitb(&clirio, clifd);

	/* Forward METHOD URI VERSION */
	sprintf(buf, "%s %s %s\r\n", method, /*uri*/path, "HTTP/1.0");
	printf("!!!DEBUG: %s", buf);
	const size_t buflen = strlen(buf);
	if (rio_writen(clifd, buf, buflen) != buflen) {
		msg_unix_error("rio_writen");
		goto cleanup;
	}
	if (forward_requesthdrs(&conrio, clifd, host) < 0) {
		goto cleanup;
	}

	/* Receive from the end server */
	char tmp_item[MAX_OBJECT_SIZE];
	char *cache_p = tmp_item;
	int can_save = 1;
	ssize_t n;
	while ((n = rio_readnb(&clirio, buf, MAXLINE)) != 0) {
		if (n < 0) {
			msg_unix_error("rio_readlineb");
			goto cleanup;
		}
		if (rio_writen(confd, buf, n) != n) {
			msg_unix_error("rio_writen");
			goto cleanup;
		}

		if (n + (cache_p - tmp_item) > MAX_OBJECT_SIZE) {
			can_save = 0;
		} else {
			memcpy(cache_p, buf, n);
			cache_p += n;
		}
	}
	if (can_save) {
		put_cache(&cache, uri, tmp_item, cache_p - tmp_item);
	}

cleanup:
	if (close(clifd) < 0) {
		msg_unix_error("close");
	}
}

/*
 * clienterror - returns an error message to the client
 */
int clienterror(int fd, char *cause, char *errnum, char *shortmsg,
		char *longmsg)
{
	char buf[MAXLINE];

	/* Print the HTTP response headers */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	RIO_WRITEN(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n\r\n");
	RIO_WRITEN(fd, buf, strlen(buf));

	/* Print the HTTP response body */
	sprintf(buf, "<html><title>Proxy Error</title>");
	RIO_WRITEN(fd, buf, strlen(buf));
	sprintf(buf, "<body bgcolor=ffffff>\r\n");
	RIO_WRITEN(fd, buf, strlen(buf));
	sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
	RIO_WRITEN(fd, buf, strlen(buf));
	sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
	RIO_WRITEN(fd, buf, strlen(buf));
	sprintf(buf, "<hr><em>The Proxy Web server</em>\r\n");
	RIO_WRITEN(fd, buf, strlen(buf));

	return 0;
}

#define HOST_HDR_LEN 5
#define USER_HDR_LEN 11
#define CONN_HDR_LEN 11
#define PROX_HDR_LEN 17

/*
 * forward_requesthdrs - read HTTP request headers
 */
int forward_requesthdrs(rio_t *rp, int clifd, const char *host)
{
	static const char host_hdr[] = "Host: ";
	static const char user_hdr[] =
	    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) "
	    "Gecko/20120305 Firefox/10.0.3\r\n";
	static const char conn_hdr[] = "Connection: close\r\n";
	static const char prox_hdr[] = "Proxy-Connection: close\r\n";

	/* Flags to keep track of if the original request headers contained the
	 * header */
	int host_fnd = 0;

	char buf[MAXLINE];

	while (1) {
		if (rio_readlineb(rp, buf, MAXLINE) < 0) {
			msg_unix_error("rio_readlineb");
			return -1;
		}

		if (strcmp(buf, "\r\n") == 0) {
			break;
		}

		if (strncmp(user_hdr, buf, USER_HDR_LEN) == 0 ||
		    strncmp(conn_hdr, buf, CONN_HDR_LEN) == 0 ||
		    strncmp(prox_hdr, buf, PROX_HDR_LEN) == 0) {
			/* Skip these, as we are going to manually send these */
			continue;
		}

		if (strncmp(host_hdr, buf, HOST_HDR_LEN) == 0) {
			/* Do not modify the host header */
			host_fnd = 1;
		}
		RIO_WRITEN(clifd, buf, strlen(buf));
		printf("DEBUG: %s", buf);
	}

	if (!host_fnd) {
		strcpy(buf, user_hdr);
		strcat(buf, host);
		strcat(buf, "\r\n");
		RIO_WRITEN(clifd, buf, strlen(buf));
		printf("DEBUG: %s", buf);
	}

	RIO_WRITEN(clifd, user_hdr, sizeof user_hdr - 1);
	printf("DEBUG: %s", user_hdr);
	RIO_WRITEN(clifd, conn_hdr, sizeof conn_hdr - 1);
	printf("DEBUG: %s", conn_hdr);
	RIO_WRITEN(clifd, prox_hdr, sizeof prox_hdr - 1);
	printf("DEBUG: %s", prox_hdr);
	RIO_WRITEN(clifd, "\r\n", 2);
	printf("DEBUG: \r\n");

	return 0;
}

int parse_uri(char *uri, char *host, char *service, char *path)
{
	printf("DEBUG: parse_uri: %s\n", uri);
	char *host_p;
	if ((host_p = strstr(uri, "://")) == NULL) {
		/* Does not start with http(s):// */
		host_p = uri;
	} else {
		/* Does start with http(s):// */
		host_p += 3;
	}
	if (*host_p == '\0') {
		return -1;
	}

	char *port_p;
	if ((port_p = strstr(host_p, ":")) == NULL) {
		/* Implicit 80 port */
		strcpy(service, "80");
	} else {
		*port_p++ = '\0'; /* '\0' terminate host_p & ++ to skip ':' */
		for (; *port_p >= '0' && *port_p <= '9'; ++port_p) {
			*service++ = *port_p;
		}
		*service = '\0';
	}

	char *path_p;
	if ((path_p = strstr(host_p, "/")) != NULL) {
		strcpy(path, path_p);
		/* '\0' terminate host_p, in case port was implicit */
		*path_p = '\0';
	} else if (port_p != NULL) {
		strcpy(path, port_p);
	} else {
		strcpy(path, "/");
	}

	strcpy(host, host_p);

	return 0;
}
