#ifndef HELPER_H
#define HELPER_H

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "llist.h"

#ifndef NAME_MAX
#define NAME_MAX 512
#endif // NAME_MAX

#define BAR "-------------------------------------------------------------"
#define PERMS S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH
#define LOGFILE "/home/agastya/Desktop/c/log_synfiles.log"
#define MAX_IPV4_LEN 15
#define PRINTF(format, ...) flog(format, ##__VA_ARGS__)

extern const size_t fdata_size;

extern int logfd;
extern char peer_ipv4[MAX_IPV4_LEN];

extern struct node *client_files_m;     // client's files' metadata
extern struct node *server_files_m;     // our files' metadata
extern struct node *client_requires_m;  // metadata of files that client needs
extern struct node *server_requires_m;  // metadata of files that server needs

const char *get_time(void);
int exists(char *filename, struct node *metadata);

int set_logfile(char *filename);
int flog(const char *format, ...);

int recvall(int s, char *buf, size_t *len);
int sendall(int s, char *buf, size_t *len);

int sendfile_all(int outfd, int infd, size_t *len);
size_t recvfile_all(int sock, const struct filedata *data);

int recv_header_len(int sock, size_t *num);
int send_header_len(int sock, size_t *num);

struct node *recv_headers(int sock, struct node **metadata, size_t count);
struct node *send_headers(int sock, struct node *metadata);

size_t compute_differences(char *dirname, struct node *metadata,
                           struct node **to_store_metadata);

struct node *send_files_from_headers(int sock, struct node *headers);
struct node *recv_files_from_headers(int sock, struct node *headers);

struct node *send_headers_and_files(int sock, struct node *headers);
struct node *recv_headers_and_files(int sock, size_t count,
                                    struct node **headers);

void cleanup(void);

#endif  // HELPER_H
