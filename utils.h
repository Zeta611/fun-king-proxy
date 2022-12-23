#ifndef __UTILS_H__
#define __UTILS_H__

typedef struct sockaddr SA;

/* Reentrant protocol-independent client/server helpers */
int open_clientfd(char *hostname, char *port);
int open_listenfd(char *port);

void msg_unix_error(char *msg);
void msg_posix_error(int code, char *msg);
void msg_gai_error(int code, char *msg);

void unix_error(char *msg);
void posix_error(int code, char *msg);

int Open_clientfd(char *hostname, char *port);
int Open_listenfd(char *port);

#endif /* __UTILS_H__ */
