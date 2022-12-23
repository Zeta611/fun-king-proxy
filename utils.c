/********************************
 * Client/server helper functions
 ********************************/

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include "utils.h"

#define LISTENQ 1024 /* Second argument to listen() */

/*
 * open_clientfd - Open connection to server at <hostname, port> and
 *     return a socket descriptor ready for reading and writing. This
 *     function is reentrant and protocol-independent.
 *
 *     On error, returns:
 *       -2 for getaddrinfo error
 *       -1 with errno set for other errors.
 */
int open_clientfd(char *hostname, char *port)
{
	int clientfd, rc;
	struct addrinfo hints, *listp, *p;

	/* Get a list of potential server addresses */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_socktype = SOCK_STREAM; /* Open a connection */
	hints.ai_flags = AI_NUMERICSERV; /* ... using a numeric port arg. */
	hints.ai_flags |= AI_ADDRCONFIG; /* Recommended for connections */
	if ((rc = getaddrinfo(hostname, port, &hints, &listp)) != 0) {
		fprintf(stderr, "getaddrinfo failed (%s:%s): %s\n", hostname,
			port, gai_strerror(rc));
		return -2;
	}

	/* Walk the list for one that we can successfully connect to */
	for (p = listp; p; p = p->ai_next) {
		/* Create a socket descriptor */
		if ((clientfd = socket(p->ai_family, p->ai_socktype,
				       p->ai_protocol)) < 0)
			continue; /* Socket failed, try the next */

		/* Connect to the server */
		if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
			break; /* Success */
		if (close(clientfd) < 0) {
			/* Connect failed, try another */ // line:netp:openclientfd:closefd
			fprintf(stderr, "open_clientfd: close failed: %s\n",
				strerror(errno));
			return -1;
		}
	}

	/* Clean up */
	freeaddrinfo(listp);
	if (!p) /* All connects failed */
		return -1;
	else /* The last connect succeeded */
		return clientfd;
}

/*
 * open_listenfd - Open and return a listening socket on port. This
 *     function is reentrant and protocol-independent.
 *
 *     On error, returns:
 *       -2 for getaddrinfo error
 *       -1 with errno set for other errors.
 */
int open_listenfd(char *port)
{
	struct addrinfo hints, *listp, *p;
	int listenfd, rc, optval = 1;

	/* Get a list of potential server addresses */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_socktype = SOCK_STREAM;	     /* Accept connections */
	hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG; /* ... on any IP address */
	hints.ai_flags |= AI_NUMERICSERV;	     /* ... using port number */
	if ((rc = getaddrinfo(NULL, port, &hints, &listp)) != 0) {
		fprintf(stderr, "getaddrinfo failed (port %s): %s\n", port,
			gai_strerror(rc));
		return -2;
	}

	/* Walk the list for one that we can bind to */
	for (p = listp; p; p = p->ai_next) {
		/* Create a socket descriptor */
		if ((listenfd = socket(p->ai_family, p->ai_socktype,
				       p->ai_protocol)) < 0)
			continue; /* Socket failed, try the next */

		/* Eliminates "Address already in use" error from bind */
		setsockopt(listenfd, SOL_SOCKET,
			   SO_REUSEADDR, // line:netp:csapp:setsockopt
			   (const void *)&optval, sizeof(int));

		/* Bind the descriptor to the address */
		if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
			break;		   /* Success */
		if (close(listenfd) < 0) { /* Bind failed, try the next */
			fprintf(stderr, "open_listenfd close failed: %s\n",
				strerror(errno));
			return -1;
		}
	}

	/* Clean up */
	freeaddrinfo(listp);
	if (!p) /* No address worked */
		return -1;

	/* Make it a listening socket ready to accept connection requests */
	if (listen(listenfd, LISTENQ) < 0) {
		close(listenfd);
		return -1;
	}
	return listenfd;
}

void msg_posix_error(int code, char *msg) /* Posix-style error */
{
    fprintf(stderr, "%s: %s\n", msg, strerror(code));
}

void msg_unix_error(char *msg)
{
	fprintf(stderr, "%s: %s\n", msg, strerror(errno));
}

void msg_gai_error(int code, char *msg)
{
    fprintf(stderr, "%s: %s\n", msg, gai_strerror(code));
}

void unix_error(char *msg) /* Unix-style error */
{
	fprintf(stderr, "%s: %s\n", msg, strerror(errno));
	exit(0);
}

void posix_error(int code, char *msg) /* Posix-style error */
{
    fprintf(stderr, "%s: %s\n", msg, strerror(code));
    exit(0);
}

int Open_clientfd(char *hostname, char *port)
{
	int rc;
	if ((rc = open_clientfd(hostname, port)) < 0) {
		unix_error("Open_clientfd error");
	}
	return rc;
}

int Open_listenfd(char *port)
{
	int rc;
	if ((rc = open_listenfd(port)) < 0) {
		unix_error("Open_listenfd error");
	}
	return rc;
}
