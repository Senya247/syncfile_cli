#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "helper.h"
#include "llist.h"

#define PORT 6969
#define WALLPAPERS "/home/ubuntu/Pictures/Wallpapers"

void check(int val, const char *msg);

const size_t fdata_size = sizeof(struct filedata);

struct node *client_files_m;    /* client's files' metadata*/
struct node *server_files_m;    /* our files' metadata*/
struct node *client_requires_m; /* metadata of files that client needs*/
struct node *server_requires_m; /* metadata of files that server needs*/
struct node *result;            /* to hold result from function*/

int logfd = STDOUT_FILENO;    /* file descriptor of log file*/
char peer_ipv4[MAX_IPV4_LEN]; /* string to hold peer ipv4 addr, max can be
                               xxx.xxx.xxx.xxx*/
size_t client_files_mc;       /* client files metadata count*/
size_t client_requires_mc;    /* client requires metadata count. number of
                               files whose metadata client needs*/
size_t server_files_mc;       /*Number of files server has*/
size_t server_requires_mc;    /*Number of files server needs*/

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <directory>\n", argv[0]);
    }
    if (chdir(argv[1]) < 0) {
        flog("Error in changing directory to %s\n", argv[1]);
        goto finish;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_aton("192.168.1.41", &(serv_addr.sin_addr));

    check(connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)),
          "connect");

    // send server client's files
    client_files_mc = fill_list(&client_files_m, ".");
    flog("(HEADER) Sending headers of %ld files\n", client_files_mc);
    send_header_len(sockfd, &client_files_mc);
    send_headers(sockfd, client_files_m);

    // receive headers of what files client needs and the files
    recv_header_len(sockfd, &client_requires_mc);
    flog("(STATUS) Need to receive %ld files\n\n", client_requires_mc);

    result =
        recv_headers_and_files(sockfd, client_requires_mc, &client_requires_m);
    if (result) {
        flog("(ERROR) Error in receiving %s\n", result->data.filename);
        goto finish;
    }
    // not needed anymore
    free_list(&client_files_m);
    free_list(&client_requires_m);

    /*Receive headers of server files and find what files server needs*/
    recv_header_len(sockfd, &server_files_mc);
    flog("(STATUS) Server will send %ld headers\n\n", server_files_mc);
    /*receive headers of server files*/
    result = recv_headers(sockfd, &server_files_m, server_files_mc);
    if (result) {
        flog("(ERROR) Error in receiving file headers of %s\n",
             result->data.filename);
        goto finish;
    }

    server_requires_mc =
        compute_differences(".", server_files_m, &server_requires_m);

    // tell server how many files it needs and sending
    flog("(STATUS) Server needs %ld files\n\n", server_requires_mc);
    send_header_len(sockfd, &server_requires_mc);

    result = send_headers_and_files(sockfd, server_requires_m);
    if (result) {
        flog("(ERROR) Error in sending %s\n", result->data.filename);
        goto finish;
    }

    flog("Finished dealing with Server\n");

finish:
    if (errno) flog("(ERRINFO) %s\n\n", strerror(errno));

    cleanup();
    close(logfd);
    close(sockfd);
}

void check(int val, const char *msg) {
    if (val < 0) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}
