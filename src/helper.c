#include "../include/helper.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <time.h>

const char* get_time(void)
{
    static char time_now[25]; // Buffer to return
    const time_t t = time(NULL);
    struct tm* b_tm = localtime(&t); // Broken down time

    strftime(time_now, 25, "%T", b_tm);

    return time_now;
}

// Check if filename is in a linked list
int exists(char* filename, struct node* metadata)
{
    // metadata is a passed copy, can be modified
    for (; metadata; metadata = metadata->next) {
        if (!strncmp(filename, metadata->data.filename, NAME_MAX))
            return 1;
    }
    return 0;
}

int set_logfile(char* filename)
{
    logfd = open(filename, O_WRONLY | O_CREAT | O_APPEND | O_SYNC, PERMS);
    if (logfd == -1)
        cleanup();
    return 0;
}

int flog(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    dprintf(logfd, "[%s] ", get_time()); // Time stamp
    vdprintf(logfd, format, args); // Write to log
    va_end(args);
    return logfd;
}

// Beej's guide to network programming
int recvall(int s, char* buf, size_t* len)
{
    size_t total = 0; // how many bytes we've sent
    size_t bytesleft = *len; // how many we have left to send
    size_t n;

    while (total < *len) {
        n = recv(s, buf + total, bytesleft, 0);
        if (n == -1 || n == 0) {
            break;
        }
        total += n;
        bytesleft -= n;
    }

    *len = total; // return number actually sent here

    return n == -1 ? -1 : 0; // return -1 onm failure, 0 on success
}

// Beej's guide to network programming
int sendall(int s, char* buf, size_t* len)
{
    size_t total = 0; // how many bytes we've sent
    size_t bytesleft = *len; // how many we have left to send
    size_t n;

    while (total < *len) {
        n = send(s, buf + total, bytesleft, 0);
        if (n == -1 || n == 0) {
            break;
        }
        total += n;
        bytesleft -= n;
    }

    *len = total; // return number actually sent here

    return n == -1 ? -1 : 0; // return -1 onm failure, 0 on success
}

/* Receive a file completely, returns number of bytes left to receive if there
 was an error in receiving. ending in `all` to be consistent with sendfile_all.
 Try to only this in other functions in this file only*/
size_t recvfile_all(int sock, const struct filedata* data)
{
    size_t to_recv = data->st.st_size; // total data to receive for a file
    size_t recv; // length of data to receive once
    size_t chunk_s = data->st.st_blksize; // chunk size to receive file in
    char* buffer = malloc(chunk_s); // buffer to hold chunk
    size_t supp_to_recv;

    int filed = open(data->filename, O_WRONLY | O_CREAT | O_TRUNC,
        data->st.st_mode); // create file with same permissions
    if (filed == -1)
        return to_recv;

    while (to_recv) {
        // receive a chunk if we have more to receive than the size of a
        // single chunk, otherwise receive whatever is left
        recv = to_recv > chunk_s ? chunk_s : to_recv;
        supp_to_recv = recv;
        if (recvall(sock, (char*)buffer, &recv) == -1)
            return to_recv;

        if (recv < supp_to_recv)
            return to_recv - recv; // return how much is left to receive

        write(filed, buffer, recv);
        to_recv -= recv;
    }
    free(buffer);
    close(filed);
    return to_recv;
}

// try to send a file completely with the sendfile function
int sendfile_all(int outfd, int infd, size_t* len)
{
    ssize_t total = 0; // how many bytes we've sent
    size_t bytesleft = *len; // how many we have left to send
    size_t n;

    while (total < *len) {
        n = sendfile(outfd, infd, &total, bytesleft);
        if (n == -1 || n == 0) {
            break;
        }
        bytesleft -= n;
    }

    *len = total; // return number actually sent here

    return n == -1 ? -1 : 0; // return -1 onm failure, 0 on success
}

void cleanup(void)
{
    // Doesn't matter if anything is NULL
    free_list(&client_files_m);
    free_list(&server_files_m);
    free_list(&client_requires_m);
    free_list(&server_requires_m);
}

int send_header_len(int sock, size_t* num)
{
    return write(sock, num, sizeof(*num));
}

int recv_header_len(int sock, size_t* num)
{
    return read(sock, num, sizeof(*num));
}

/* send headers from the linked list `metadata` filled with
 metadata. Return NULL if no eror, or the metadata of the
 file in which the error occured*/
struct node* send_headers(int sock, struct node* metadata)
{
    struct node* tmp = metadata;
    size_t msl = fdata_size; // metadata send length

    while (tmp) {
        flog("(HEADER) Sending headers of %s\n", tmp->data.filename);
        sendall(sock, (char*)&(tmp->data), &msl);
        if (msl < fdata_size) {
            flog("(ERROR) Error in sending headers of %s\n",
                tmp->data.filename);
            return tmp;
        }
        tmp = tmp->next;
    }
    dprintf(logfd, "\n");
    return NULL;
}

/* recv headers into the linked list `metadata`.
 return NULL if no error, or pointer last node
 where no error occured */
struct node* recv_headers(int sock, struct node** headers, size_t count)
{
    size_t hrl = fdata_size; // header receive length
    struct node* tmp;
    struct node* last = tmp;

    for (; count; count--) {
        tmp = create_node();
        recvall(sock, (char*)&(tmp->data), &hrl);

        if (hrl < fdata_size) {
            // Just say error occured in the last file whose header
            // was received properly, can't show file name if header wasn't
            // received correctly
            flog("(ERROR) Error in receiving headers of %s\n",
                last->data.filename);
            return last;
        }
        add_list(headers, tmp);
        flog("(HEADER) Received headers of %s\n", tmp->data.filename);
        last = tmp;
    }
    reverse_list(
        headers); /* Need to reverse linked list, it's received in the right
                     order, but when nodes are added, it reverses it*/
    dprintf(logfd, "\n");
    return NULL;
}

/* Stores files that are uncommon in the linked list `metadata`
 and the directory `dirname` into the linked list
 `to_store_metadata`. return length*/
size_t compute_differences(char* dirname, struct node* metadata,
    struct node** to_store_metadata)
{
    DIR* dir = opendir(dirname);
    struct dirent* de;
    size_t count = 0;

    while (de == readdir(dir)) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        if (exists(de->d_name, metadata)) {
            flog("(INFO) Common %s\n", de->d_name);
        } else {
            flog("(INFO) Uncommon %s\n", de->d_name);

            struct node* n = create_node();
            strncpy(n->data.filename, de->d_name, NAME_MAX);
            stat(de->d_name, &(n->data.st));

            add_list(to_store_metadata, n);
            count++;
        }
    }
    dprintf(logfd, "\n");
    return count;
}

/* Send files to `sock` from linked list `metadata`
 Return NULL if no error, otherwise node of file in which error occured
 check what error occured with perror*/
struct node* send_files_from_headers(int sock, struct node* metadata)
{
    size_t fsl; // file send length
    int filed;
    struct node* tmp = metadata;

    for (; tmp; tmp = tmp->next) {
        filed = open(tmp->data.filename, O_RDONLY);
        if (filed == -1)
            return tmp;

        fsl = tmp->data.st.st_size;
        flog("(FILE) Sending %s\n", tmp->data.filename);
        sendfile_all(sock, filed, &fsl);
        if (fsl < tmp->data.st.st_size)
            return tmp;

        close(filed);
    }
    dprintf(logfd, "\n");
    return NULL;
}

struct node* recv_files_from_headers(int sock, struct node* headers)
{
    size_t recvd;

    for (struct node* tmp = headers; tmp; tmp = tmp->next) {
        recvd = recvfile_all(sock, &(tmp->data));
        if (recvd)
            return tmp; // If there is still data which wasn't received
        flog("(FILE) Received %s\n", tmp->data.filename);
    }
    return NULL;
}

// Sends headers and files alternately to `sock` from headers `headers`.
struct node* send_headers_and_files(int sock, struct node* headers)
{
    size_t msl = fdata_size; // metadata send length
    size_t fsl; // file send length
    int filed;

    for (struct node* tmp = headers; tmp; tmp = tmp->next) {
        filed = open(tmp->data.filename, O_RDONLY);
        if (filed == -1)
            return tmp;

        // send header
        flog("(HEADER) Sending headers of %s\n", tmp->data.filename);
        sendall(sock, (char*)&(tmp->data), &msl);
        if (msl < fdata_size)
            return tmp;

        fsl = tmp->data.st.st_size;
        flog("(FILE) Sending %s\n", tmp->data.filename);
        sendfile_all(sock, filed, &fsl);
        if (fsl < tmp->data.st.st_size)
            return tmp;
        close(filed);
    }
    dprintf(logfd, "\n");
    return NULL;
}

struct node* recv_headers_and_files(int sock, size_t count,
    struct node** headers)
{
    struct node* tmp;
    struct node* last = tmp;
    size_t hrl = fdata_size; // header receive length
    size_t recvd;

    for (; count; count--) {
        tmp = create_node();
        recvall(sock, (char*)&(tmp->data), &hrl);
        if (hrl < fdata_size)
            return last;
        flog("(HEADER) Received headers of %s\n", tmp->data.filename);
        add_list(headers, tmp);

        recvd = recvfile_all(sock, &(tmp->data));
        if (recvd)
            return tmp;
        flog("(FILE) Received %s\n", tmp->data.filename);

        last = tmp;
    }
    dprintf(logfd, "\n");
    return NULL;
}
