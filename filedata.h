/* This is an updated version of the starter code that you may find more
   useful. You are welcome to change this or the original starter code in 
   any way that you like.  Remember that if you use something that you have    
   not seen in lectures, labs, the 209 website or Piazza, you must get 
   instructor permission. 
*/

#include <time.h>
#include "message.h"

struct file_info {
    char filename[MAXNAME];
    time_t mtime; /* not used in my code */
};

struct client_info {
    int sock;
    char userid[MAXNAME];
    char dirname[MAXNAME];
    struct file_info files[MAXFILES];
    int STATE;
    /* next items for file this client is currently receiving */
    char currFilename[MAXNAME];
    int expected_size;
    int received_so_far;
    time_t mtime;         
};
extern struct client_info clients[MAXCLIENTS];

void init();
int find_client_index(int sockfd); 
int add_client(struct login_message s, int sockfd);
int check_file(struct file_info *files, char *filename);
void clear_files(struct file_info *files);
void clear_client(int client_index);
int is_sync_empty(struct sync_message *msg);
int get_file(struct file_info *files, char *filename);
void update_mtime(char *dirname, char *filename, long int mtime);

void display_clients();

