/* This is an updated version of the starter code that you may find more
   useful. You are welcome to change this or the original starter code in 
   any way that you like.  Remember that if you use something that you have 
   not seen in lectures, labs, the 209 website or Piazza, you must get 
   instructor permission. 
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "filedata.h"

struct client_info clients[MAXCLIENTS];
void Writen(int fd, void *ptr, size_t nbytes);

/* Used after a full sync operation finishes so next sync operation
   can track which files have been sync'ed */
void clear_files(struct file_info *files) {
    int j;
    for (j = 0; j < MAXFILES; j++) {
        files[j].filename[0] = '\0';
        files[j].mtime = 0;
    }
}

/* Used when client closes a connection */
void clear_client(int i) {
    clients[i].userid[0] = '\0';
    clients[i].dirname[0]= '\0';
    clients[i].sock = -1;
    clients[i].dirname[0] = '\0';
    clear_files(clients[i].files);
}
    

/* initialize dirs and clients */
void init(){
    int i;
    for(i = 0; i < MAXCLIENTS; i++) {
        clear_client(i);
    }
}

/* Adds the client name and dir to the client array based on sockfd.  
   Returns 0 on success. Returns -1 if this sockfd is not in the 
   client array or if this sockfd is in the array with another client name. */
int add_client(struct login_message s, int sockfd) {
    int i;
    for (i = 0; i < MAXCLIENTS; i++) {
        if (clients[i].sock == sockfd)  {
          if(clients[i].userid[0] != '\0') {
            fprintf(stderr, "Error: socket in use for another client\n");
             return -1;
          } else {
            strncpy(clients[i].userid, s.userid, MAXNAME);
            strncpy(clients[i].dirname, s.dir, MAXNAME);
            /* this clearing is important for my algorithm for finding
               server-only files */
            clear_files(clients[i].files);
            clients[i].STATE = SYNC;
            printf("Successfully added %s into client slot %d\n",s.userid, i);
            return 0;
          }
        }
    }
    /* if we reach here we never found this sockfd in the clients array */
    fprintf(stderr, "Error: socket is not properly in clients array\n");
    return -1;
}

/* Return the index of the client with sock sockfd or -1 if no such client. */
int find_client_index(int sockfd) {
    int i;
    for (i = 0; i < MAXCLIENTS; i++) {
        if (clients[i].sock == sockfd)  {
            return i;
        }
    }
    return -1;
}

/* check_file - check if filename is in files list
 *
 * - return 0 if this file is already in the files struct
 * - add filename and return 1 if filename was not found in files
 * - return -1 if there is no more space in contents to add filename
 */

int check_file(struct file_info *files, char *filename) {
    int i;
    for(i = 0; i < MAXFILES; i++) {
        if(strcmp(files[i].filename, filename) == 0) {
            return 0;
        } else if(files[i].filename[0] == '\0') {
            strncpy(files[i].filename, filename, MAXNAME);
            return 1;
        }
    }
    return -1;
}

int get_file(struct file_info *files, char *filename){
    int i;
    for(i = 0; i < MAXFILES; i++) {
        if(strcmp(files[i].filename, filename) == 0) {
            return i;
        }
    }
    return -1;
}

/* found useful for debugging purposes */
void display_clients() {
    int i; 
    for (i = 0; i< MAXCLIENTS; i++) {
        if (clients[i].sock != -1) {
            if (clients[i].userid[0] != '\0') {
                printf("CLIENT %d: %s -  %s\n", i, clients[i].userid, clients[i].dirname);
                int j = 0;
                while(clients[i].files[j].filename[0] != '\0') {
                    printf("    %s %ld\n", clients[i].files[j].filename, 
                        (long int)clients[i].files[j].mtime);
                    j++;
                }
            } else {
                printf("CLIENT %d: login in progress\n", i);
            }
        }
    }
}

/*Update the modified time of the file pointed by filename
*for any client charing the directory pointed by dirname to mtime*/
void update_mtime(char *dirname, char *filename, long int mtime){
    int i;
    for(i = 0; i < MAXCLIENTS; i++){
        if (strcmp(clients[i].dirname, dirname) == 0){
            int file_ind;
            if((file_ind = get_file(clients[i].files, filename)) != -1){
                clients[i].files[file_ind].mtime = mtime; 
            }
        }
    }
}

/*Check if given syn_message sync is empty
*sReturn 1 if it is empty, 0 otherwise*/
int is_sync_empty(struct sync_message *msg){
    if((strncmp(msg->filename, "",1) == 0) && (msg->mtime == 0) && (msg->size == 0)){
        return 1;
    }
    else{
        return 0;
    }
}