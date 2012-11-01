#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include "filedata.h"
#include "wrapsock.h"
#include <errno.h>

ssize_t Readn(int fd, void *ptr, size_t nbytes);
void Writen(int fd, void *ptr, size_t nbytes);
char *rootdir = "clientdir/";

/*Send file to client specified by soc*/
void sendfile(int soc, char *filepath, int filesize){
    fprintf(stdout, "ready to send %d bytes\n", filesize);
    char buf[CHUNKSIZE];
    FILE *file;
    if((file = fopen(filepath, "r" )) == NULL){
        perror("open file");
    }

    if(filesize < CHUNKSIZE){
        fread(&buf,filesize, 1, file);
        Writen(soc, &buf, filesize);
    }
    else{
        int n = 0;
        while(fread(&buf,CHUNKSIZE, 1, file) != 0){
            Writen(soc, &buf, CHUNKSIZE);
            n += CHUNKSIZE;
        }
        fprintf(stderr, "%d\n", n);
        Writen(soc, &buf, filesize - n);
    }
    
    if(fclose(file) == -1){
        perror("close file");
    }
}

/*Store new client's login information*/
void login(struct client_info *client){
    struct login_message login;
    Readn(client->sock, &login, sizeof(struct login_message));
    strncpy(client->userid, login.userid, MAXNAME);
    strncpy(client->dirname, rootdir, MAXNAME);
    strncat(client->dirname, login.dir, strlen(login.dir));
    if(mkdir(client->dirname, S_IRWXU) == -1){
        perror("mkdir");
    }
    client->STATE = SYNC;
    printf("userid: %s \ndir: %s\n", client->userid, client->dirname);
}

/*Send any file that is on the server, but not on the client
*to the client*/
void send_new_file(struct client_info *client){
    DIR *dirstream;

    if((dirstream = opendir(client->dirname)) == NULL){
        perror("Open Directory");
        exit(1);
    }

    struct dirent *dir_file;
    //Process all files in current dir
    while((dir_file = readdir(dirstream)) != NULL){
        if(dir_file->d_type == 4) continue; //do not process directories
        char filepath[2 * MAXNAME + 1];
        strncpy(filepath, client->dirname, MAXNAME);
        strcat(filepath, "/");
        strncat(filepath, dir_file->d_name, MAXNAME);

        if(check_file(client->files, dir_file->d_name) == 1){
            //send sync message to inform of new file to be sent, send file and
            //read next request
            struct stat filestat;
            stat(filepath, &filestat);
            int filesize = htonl(filestat.st_size);
            struct sync_message sync;
            strncpy(sync.filename, dir_file->d_name, MAXNAME);
            sync.size = filesize;
            sync.mtime = filestat.st_mtime;
            Writen(client->sock, &sync, sizeof(struct sync_message));
            sendfile(client->sock, filepath, filestat.st_size);
            struct sync_message response;
            Readn(client->sock, &response, sizeof(struct sync_message));

            //and the file's mtime to the client's files array
            int fileind = get_file(client->files, dir_file->d_name);
            client->files[fileind].mtime = filestat.st_mtime;
        }
    }

    //send empty sync_message when there are no more files to be sent
    struct sync_message sync;
    sync.filename[0] = '\0';
    sync.mtime = 0;
    sync.size = 0;
    Writen(client->sock, &sync, sizeof(struct sync_message));
    
    if(closedir(dirstream) == -1){
        perror("Close Directory");
    }
}
/*Process any SYNC message received by the server from a client*/
void process_sync(struct client_info *client){
    struct sync_message syncmsg;
    Readn(client->sock, &syncmsg, sizeof(struct sync_message));
    if(is_sync_empty(&syncmsg)){
        fprintf(stdout, "SYNC: Checking for new files to send to the client\n");
        send_new_file(client);
    }
    else{
        //write reponse
        fprintf(stdout, "SYNC: checking file %s\n", syncmsg.filename);
        struct sync_message response;
        if(check_file(client->files, syncmsg.filename) == 0){
            struct stat filestat;
            char filepath[2 * MAXNAME + 1];
            strncpy(filepath, client->dirname, MAXNAME);
            strcat(filepath, "/");
            strncat(filepath, syncmsg.filename, MAXNAME);
            if(stat(filepath, &filestat) == -1){
            perror("stat");
            }
            strncpy(response.filename,syncmsg.filename, MAXNAME);
            response.size = htonl(filestat.st_size);
            int i = get_file(client->files, syncmsg.filename);
            response.mtime = client->files[i].mtime;
            Writen(client->sock, &response, sizeof(struct sync_message));

            //prepare server to receive file from client
            if(response.mtime < syncmsg.mtime){
                fprintf(stdout,"Client is more recent, need to get\n");
                client->files[i].mtime = syncmsg.mtime;
                update_mtime(client->dirname, syncmsg.filename, syncmsg.mtime);
                client->STATE = GETFILE;
                strncpy(client->currFilename, syncmsg.filename, MAXNAME);
                client->expected_size = ntohl(syncmsg.size);
                client->received_so_far = 0;
                client->mtime = syncmsg.mtime;
            }
            //send file to client
            else if(response.mtime > syncmsg.mtime){
                fprintf(stdout,"Server is more recent, need to send\n");
                sendfile(client->sock, filepath, filestat.st_size);
            }
            else{
                fprintf(stdout,"Server and client are synchronized on this file\n");
            }
        }
        //prepare server to receive file from client
        else{
            //set last mtime to be mtime just sent by client
            int i = get_file(client->files, syncmsg.filename);
            client->files[i].mtime = syncmsg.mtime;

            //set the client to GETFILE state
            client->STATE = GETFILE;
            strncpy(client->currFilename, syncmsg.filename, MAXNAME);
            client->expected_size = ntohl(syncmsg.size);
            client->received_so_far = 0;
            client->mtime = syncmsg.mtime;

            //send the client a dummy message to let him know he has to send a file
            response.mtime = -1;
            Writen(client->sock, &response, sizeof(struct sync_message));
        }
    }
}

/*Get the CHUNKSIZE of the file sent by client and write to disk*/
void process_get_file(struct client_info *client){

    FILE *file;
    char filepath[2 * MAXNAME + 1];
    strcpy(filepath, client->dirname);
    strncat(filepath, "/", 2);
    strncat(filepath, client->currFilename, MAXNAME);
    if((file = fopen(filepath, "a" )) == NULL){
        perror("open file");
    }

    char buf[CHUNKSIZE];
    if((client->expected_size - client->received_so_far) > CHUNKSIZE){
        read(client->sock, &buf, CHUNKSIZE); 
        client->received_so_far += CHUNKSIZE;
        fwrite(&buf, CHUNKSIZE, 1, file);
    } 
    //either file is smaller than CHUNKSIZE, or last bit is being sent
    else{
        int towrite = client->expected_size - client->received_so_far;
        Readn(client->sock, &buf, towrite);
        fwrite(&buf, towrite, 1, file);
        client->STATE = SYNC;
    }

    if(fclose(file) == -1){
        perror("close file");
    }
}

int main(){

    int i, maxi, maxfd, listenfd, connfd, sockfd;
    int nready;
    ssize_t n;
    fd_set rset, allset;
    int buf;
    socklen_t clilen;
    struct sockaddr_in cliaddr, servaddr;
    int yes = 1;    

    if(mkdir(rootdir, S_IRWXU) == -1){
        switch(errno){
            case EEXIST : fprintf(stdout, "DBSERVER: clientdir already exists\n");
                          break;
            default : perror("mkdir");
                      exit(1); 
        }
    }

    listenfd = Socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(PORT);

    if((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)))
       == -1) {
        perror("setsockopt");
    }

    Bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));

    Listen(listenfd, LISTENQ);

    maxfd = listenfd;
    maxi = -1;                  /* index into client[] array */
    for (i = 0; i < MAXCLIENTS; i++) {
        clients[i].STATE = LOGIN;  /* -1 indicates available entry */
        clients[i].sock = -1;
        //printf("%d\n", clients[i].STATE); 
    } 

    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);

    for ( ; ; ) {

    rset = allset;                  /* make a copy because rset gets altered */
    nready = Select(maxfd+1, &rset, NULL, NULL, NULL);
    
    /* first check for new client connection */
    if (FD_ISSET(listenfd, &rset)) {    
        clilen = sizeof(cliaddr);
        connfd = Accept(listenfd, (struct sockaddr *) &cliaddr, &clilen);
        printf("accepted a new client\n");

        for (i = 0; i < MAXCLIENTS; i++) {
          if (clients[i].STATE == LOGIN) {
              clients[i].sock = connfd;   /* save descriptor */
              login(&clients[i]);
              break;
          }
        }
        if (i == MAXCLIENTS) {
          printf("too many clients\n");
        }
        
        FD_SET(connfd, &allset);    /* add new descriptor to set */
        if (connfd > maxfd)
          maxfd = connfd;   /* for select */
        if (i > maxi)
          maxi = i; /* max index in client[] array */
        
        if (--nready <= 0)
          continue; /* no more readable descriptors */
    }

    /* then check all clients for data */
    for (i = 0; i <= maxi; i++) {   
        if ( (sockfd = clients[i].sock) < 0)
          continue;
        if (FD_ISSET(sockfd, &rset)) {
            if ( (n = Readn(sockfd, &buf, 1)) <= 0) {
                /* connection closed by client */
                Close(sockfd);
                FD_CLR(sockfd, &allset);
                clients[i].STATE = LOGIN;
                clients[i].sock = -1;
                clear_client(i);
                printf("connection closed by client\n");
            } else {
              switch(clients[i].STATE){
                  case SYNC: fprintf(stdout,"received SYNC\n");
                             process_sync(&clients[i]);
                             break;
                  case GETFILE: fprintf(stdout,"received GETFILE\n");
                             process_get_file(&clients[i]);
                             break;
                  default: fprintf(stderr,"received nothing\n");
                             break;
              }
            }
            
            if (--nready <= 0)
                break;  /* no more readable descriptors */
        }
    }
    }

    return 0;
}
