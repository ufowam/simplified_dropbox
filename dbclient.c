#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <utime.h>
#include <netinet/in.h>
#include <netdb.h>
#include "filedata.h"
#include "wrapsock.h"
#include <libgen.h>

ssize_t Readn(int fd, void *ptr, size_t nbytes);
void Writen(int fd, void *ptr, size_t nbytes);

/*Send file to server specified by soc*/
void sendfile(int soc, char *filepath, int filesize){
    fprintf(stdout, "ready to send %d bytes\n", filesize);
    char buf[CHUNKSIZE];
    FILE *file;
    if((file = fopen(filepath, "r" )) == NULL){
        perror("open file");
    }

    if(filesize < CHUNKSIZE){
        fread(&buf,filesize, 1, file);
        write(soc, "", 1);
        Writen(soc, &buf, filesize);
    }
    else{
        int n = 0;
        while(fread(&buf,CHUNKSIZE, 1, file) != 0){
            write(soc, "", 1);
            Writen(soc, &buf, CHUNKSIZE);
            n += CHUNKSIZE;
        }
        fprintf(stderr, "%d\n", n);
        write(soc, "", 1);
        Writen(soc, &buf, filesize - n);
    }
    
    if(fclose(file) == -1){
        perror("close file");
    }
}

/*Get file from the server and write it to disk*/
void receivefile(int soc, char *filepath, int filesize){
    fprintf(stdout, "ready to receive %d bytes\n", filesize);

    char buf[CHUNKSIZE];
    FILE *file;
    if((file = fopen(filepath, "w+")) == NULL){
        perror("open file to write");
    }

    if(filesize < CHUNKSIZE){
        Readn(soc, &buf, filesize);
        fwrite(&buf, filesize, 1, file);
    }
    else{
        size_t n;
        while(filesize - n > CHUNKSIZE){
            Readn(soc, &buf, CHUNKSIZE);
            fwrite(&buf, CHUNKSIZE, 1, file);
            n += CHUNKSIZE;
        }
            Readn(soc, &buf, filesize - n);
            fwrite(&buf, filesize - n, 1, file);
    }

    if(fclose(file) == -1){
        perror("close file");
    }
}

/*Get files from server that the client doesn't have*/
void get_new_files(char *path, int soc){

    //send empty message
    struct sync_message sync;
    sync.filename[0] = '\0';
    sync.mtime = 0;
    sync.size = 0;
    write(soc, "", 1);
    Writen(soc, &sync, sizeof(struct sync_message)); 
    
    //read response
    struct sync_message response;
    Readn(soc, &response, sizeof(struct sync_message)); 
    int filesize = ntohl(response.size);
    char filepath[2 * MAXNAME + 1];
    strncpy(filepath, path, MAXNAME);
    strcat(filepath, "/");
    strncat(filepath, response.filename, MAXNAME);

    //get any new file sent by the server, then send new request
    while(!is_sync_empty(&response)){ 
        receivefile(soc, filepath, filesize);
        //set file's and mtime to be those of the server
        struct stat filestat;
        stat(filepath, &filestat);
        struct utimbuf new_time;
        new_time.modtime = response.mtime; 
        new_time.actime = filestat.st_atime;
        utime(filepath, &new_time);
 
        Writen(soc, &sync, sizeof(struct sync_message));
        Readn(soc, &response, sizeof(struct sync_message));
        filesize = ntohl(response.size);
        strncpy(filepath, path, MAXNAME);
        strcat(filepath, "/");
        strncat(filepath, response.filename, MAXNAME);        
    }
}

/*Synchronize the client's and server's directories*/
void synchronize(char *path, int soc){

    DIR *dirstream;
    if((dirstream = opendir(path)) == NULL){
        perror("Open Directory");
        exit(1);
    }

    char name[MAXNAME]; strncpy(name, basename(path), MAXNAME);
    strcat(name, "/");
    struct dirent *dir_file;

    //Process all files in current dir
    while((dir_file = readdir(dirstream)) != NULL){
        if(dir_file->d_type == 4) continue; //do not process directories
        fprintf(stdout,"FILE: %s\n", dir_file->d_name);

        //get file's information
        struct stat filestat;
        char filepath[MAXNAME]; 
        strncpy(filepath, name, MAXNAME);
        strncat(filepath, dir_file->d_name, strlen(dir_file->d_name));
        if(stat(filepath, &filestat) == -1){
            perror("stat");
            exit(1);
        }
        else{
            //Send the server a sync message for the current file
            struct sync_message sync;
            strncpy(sync.filename, dir_file->d_name, strlen(dir_file->d_name) + 1);
            sync.mtime = filestat.st_mtime;
            sync.size = htonl((int)filestat.st_size);
            write(soc, "", 1);
            Writen(soc, &sync, sizeof(struct sync_message));

            //Read the server's reponse to that message
            struct sync_message response;
            Readn(soc, &response, sizeof(struct sync_message));
            int server_mtime = response.mtime;
            if(server_mtime < filestat.st_mtime){
                fprintf(stdout, "File is newer on client. Need to send\n");
                sendfile(soc, filepath, filestat.st_size);
            }
            else if(server_mtime > filestat.st_mtime){
                fprintf(stdout, "File is newer on server. Need to get\n");
                int filesize = ntohl(response.size);
                receivefile(soc, filepath, filesize); 

                //set file's and mtime to be those of the server
                struct utimbuf new_time;
                new_time.modtime = server_mtime; 
                new_time.actime = filestat.st_atime;
                utime(filepath, &new_time);             
            }
            else{
                fprintf(stdout, "Server and client are synchronized on this file\n");
            }
        }
    }

    //check for new files in the server and get them
    fprintf(stdout, "trying to get new file\n");
    get_new_files(path, soc);

    if(closedir(dirstream) == -1){
        perror("Close Directory");
    }
}

int main (int argc, char **argv){

    int soc;
    struct hostent *hp;
    struct sockaddr_in peer;
    char name[MAXNAME];

    if(argc != 4){
        fprintf(stderr, "Usage: ./dbclient host userid directory\n");
        exit(1);
    }

    //check if directory is valid and get it's name
    strncpy(name, basename(argv[3]), MAXNAME);
    fprintf(stderr, "%s\n", name);
    hp = gethostbyname(argv[1]);
    if(hp == NULL){
        fprintf(stderr, "%s: %s unknown host\n", argv[0], argv[1]);
        exit(1);
    }

    /* Set up the struct to initialize the socket */
    peer.sin_family = PF_INET;
    peer.sin_port = htons(PORT);

    printf("PORT = %d\n", PORT);
    peer.sin_addr = *((struct in_addr *)hp->h_addr);

    /* create socket */
    soc = Socket(PF_INET, SOCK_STREAM, 0);

    /* request connection to server */
    if(Connect(soc, (struct sockaddr *)&peer, sizeof(peer)) == -1) {
        Close(soc);
        exit(1);
    }

    //send login message
    struct login_message login;
    fprintf(stderr,"dir: %s\n", argv[3]);
    strncpy(login.userid, argv[2], MAXNAME);
    strncpy(login.dir, name, MAXNAME);
    Writen(soc, &login, sizeof(struct login_message));

    while(1){
        fprintf(stdout, "----start sync----\n");
        synchronize(argv[3], soc);
        fprintf(stdout, "----done sync----\n");
        sleep(15);
    }
    Close(soc);

    return(0);
}
