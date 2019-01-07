#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hcq.h"

#ifndef PORT
  #define PORT 56832
#endif
#define MAX_BACKLOG 5
#define BUF_SIZE 30

// Use global variables so we can have exactly one TA list and one student list 
Ta *ta_list = NULL;                                                             
Student *stu_list = NULL;                                                       
                                                                                 
Course *courses;                                                                
int num_courses = 3;   

// This Struct contatins everything about a client
struct sockname {
    int sock_fd;
    char username[BUF_SIZE+1];
    char ta_or_s[2];
    char course[7];
    char buf[BUF_SIZE +3]; // Buffer for partial reads
    int inbuf;      // for partial read
    int room;       // for partial read
    char *after;   // for partial read
    int state; // 0 = nothing, 1 = name saved, 2 = ta_or_s saved, 3 = Course saved
    struct sockname *next;
};


/* Accept a connection. Note that a new file descriptor is created for
 * communication with the client. The initial socket descriptor is used
 * to accept connections, but the new socket is used to communicate.
 * Return the new client's file descriptor or -1 on error.
 *
 * Add a new struct to the linked list and ask for name.
 */
int accept_connection(int fd, struct sockname **all_clients) { 
    // create a new client, set its 'next' to the all_clients list
    struct sockname *new_client = malloc(sizeof(struct sockname));
    // add the nex client to the front of the list
    new_client->next = *all_clients;
    // set the head to be the new client
    *all_clients = new_client;

    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) {
        perror("server: accept");
        close(fd);
        exit(1);
    }
    
    char msg[] = "Welcome to the Help Centre, what is your name?\r\n";
    if (write(client_fd, msg, strlen(msg)) != strlen(msg)) {
        close(client_fd);
        client_fd = -1;
    }
    // Set the defaults for the client
    new_client->sock_fd = client_fd;
    new_client->state = 0;
    new_client->buf[0] = '\0';
    new_client->inbuf = 0;
    new_client->after = new_client->buf;
    new_client->room = sizeof(new_client->buf);
    // strcpy(new_client->ta_or_s, 'N'); 
    return client_fd;
}
/* free and delete clients from the linked list that are no longer connected
*/
void garbage_disposal(struct sockname **ll_head, int delete_everything) {
    struct sockname *head = *ll_head;
   
    // deal with the head being deleted
    while(((head->sock_fd == -1) || delete_everything) && (head->next != NULL)) {
        struct sockname *temp = head;
        head = head->next;
        free(temp);
    }
    // head is definitely an active client and can be a reliable previous node
    struct sockname *prev = head;
    head = head->next;  
    *ll_head = prev;
    if (head == NULL) {
        return;
    }

    // check nodes after the head and remove them if fd = -1
    while (head->next != NULL) {
        if (head->sock_fd == -1) {
            struct sockname *temp;
            temp = prev->next;
            prev->next = temp->next;
            free(temp);
        } else {
            prev = head;
        }
        
        head = prev->next;
    }
}

int find_network_newline(const char *buf, int n);                               
int read_from(struct sockname *current_struct);
  
/* Read from the current structs file descriptor. If it is a complete command,
 * send the command to be processed. If it is a partial command, store the 
 * command in the struct until further use. 
 */
int buffer_input(struct sockname *curr){                           
    int fd = curr->sock_fd;                          
    int to_return = 0;                                                                 
    int nbytes;                   
     
    (nbytes = read(fd, curr->after, curr->room)); 
    if (nbytes == 0) {
        close(curr->sock_fd);
        return curr->sock_fd;
    }
    if (nbytes == 33) {
        curr->after[31] = '\r';
        curr->after[32] = '\n';
    }
    curr->inbuf += nbytes;                                                            
    int where;                                               
    while ((where = find_network_newline(curr->buf, curr->inbuf)) > 0) { 
        curr->buf[where - 2] = '\0';
        // Truncate the message if it is too long
        if (strlen(curr->buf) > 30) {
            //close(curr->sock_fd);
            //return curr->sock_fd;
            curr->buf[30] = '\0';
        }
        // full command
        to_return = read_from(curr);
        curr->inbuf-= where;
        memmove(curr->buf, curr->buf + where, curr->inbuf);
    }
    curr->after = curr->buf + curr->inbuf;
    curr->room = sizeof(curr->buf) - curr->inbuf;
    return to_return;
 }

/*                                                                          
  * Search the first n characters of buf for a network newline (\r\n).           
  * Return one plus the index of the '\n' of the first network newline,          
  * or -1 if no network newline is found.                                        
*/                                                                             
int find_network_newline(const char *buf, int n) {                              
   int i;                                                                        
   for (i = 0; i < (n-1); i++) {                                                 
                                                                                 
     if ((buf[i] == '\r') && (buf[i+1] == '\n'))   {                             
       return i + 2;                                                             
     }                                                                           
                                                                                 
   }                                                                             
   return -1;                                                                    
}                                                                                  

/* Given a client with a complete command, execute that command and write to 
 * the client. If it is an invalid command, say as much.
 *
 * Commands could be:
 *    The clients name
 *    If they are a TA or a student
 *    The course a student is asking about
 *    Stats for a TA
 *    Stats for a Student
 *    TA requesting the next student
 */
int read_from(struct sockname *current_struct) {
    int fd = current_struct->sock_fd;

    // New client, read name and write (if they are TA or S) update state
    if (current_struct->state == 0) {
        strcpy(current_struct->username, current_struct->buf);
        char msg[] = "Are you a TA or a Student (enter T or S)? \r\n";
        if (write(fd, &msg ,strlen(msg)) != strlen(msg)) {
            close(current_struct->sock_fd);
            return fd;
        }
        current_struct->state++;
    }
    
           
    // read TA_or_S write valid commands if TA, ask course if S
    else if (current_struct->state == 1) {
        strcpy(current_struct->ta_or_s, current_struct->buf);
        // TA
        if (strncmp(current_struct->buf,"T",1) == 0) {            
            add_ta(&ta_list, current_struct->username);
            char msg[] = "Valid commands for TA:\n\tstats\n\tnext\n\t(or "
                          "use Ctrl-C to leave)\r\n";
            if (write(fd, &msg ,strlen(msg)) != strlen(msg)) { 
                close(current_struct->sock_fd);
                return fd;
            }
            current_struct->state++;
        }
        // Student
        else if (strncmp(current_struct->buf, "S",1) == 0) {
            char msg[] = "Valid courses: CSC108, CSC148, CSC209\nWhich course "
                          "are you asking about?\r\n";
            if (write(fd, &msg ,strlen(msg)) != strlen(msg)) {
                close(current_struct->sock_fd);
                return fd;
            } 
            current_struct->state++;
        }
        // Incorrect input
        else {
          char msg[] = "Invalid role (enter T or S)\r\n";
          if (write(fd, &msg ,strlen(msg)) != strlen(msg)) {
              close(current_struct->sock_fd);
              return fd; 
          }
          // Don't increment state 
        }
    }
    // A TA's incoming command or a Students Course 
    else if (current_struct->state == 2) {
          // Ta
        if(strncmp(current_struct->ta_or_s, "T", 1)==0){
            // Stats
            if(strncmp(current_struct->buf,"stats",5) == 0){
                char *msg = print_full_queue(stu_list);                             
                if (write(fd, msg ,strlen(msg)) != strlen(msg)) {   
                    close(current_struct->sock_fd);
                    free(msg);               
                    return fd;                                                         
                } else {                      
                    free(msg);  
                }
            }
            // next
            else if(strncmp(current_struct->buf,"next",4) == 0){
                char msg[25];
                if (next_overall(current_struct->username, &ta_list, &stu_list) == 1) {
                    strcpy(msg, "Invalid TA name.\r\n");                                           
                } else{
                    strcpy(msg, "Next student assigned.\r\n");
                }
                 
                if (write(fd, &msg ,strlen(msg)) != strlen(msg)) {
                    close(current_struct->sock_fd);
                    return fd;
                }
            }
        }
        // Student
        else if(strncmp(current_struct->ta_or_s, "S", 1)==0){
            strcpy(current_struct->course, current_struct->buf);
            int result = add_student(&stu_list,current_struct->username,
                current_struct->course, courses, num_courses);            
            
            char msg[140];          
            if (result == 0){
                strcpy(msg, "You have been entered into the queue. While you"
                   " wait, you can use the command stats to see which TAs are"
                   " currntly serving students.\r\n");
                current_struct->state++; 
            } else if (result == 1) {
                strcpy(msg, "This student is already in the queue.\r\n");
                close(current_struct->sock_fd);
                return fd;                
            } else if (result == 2) {                                               
                strcpy(msg, "Invalid Course -- student not added.\r\n");                 
            }
            if (write(fd, &msg ,strlen(msg)) != strlen(msg)) {
                close(current_struct->sock_fd);
                return fd;
            }
            
        }    
    }
    // Student only state for commands
    else if (current_struct->state == 3) {
        if (strcmp(current_struct->buf, "stats") == 0) {
            char *msg = print_currently_serving(ta_list);
            if (write(fd, msg ,strlen(msg)) != strlen(msg)) {
                close(current_struct->sock_fd);                             
                free(msg);                                                  
                return fd;                                                  
            }                                                               
            free(msg);                                                          
        }
        else {
            char msg[] = "Incorrect Syntax.\r\n";
            if (write(fd, msg ,strlen(msg)) != strlen(msg)) {
                close(current_struct->sock_fd);
                return fd;
            }
        }             
    }
    return 0;
}


int main(void) {
    if ((courses = malloc(sizeof(Course) * 3)) == NULL) {                       
        perror("malloc for course list\n");                                     
        exit(1);                                                                
    }

    strcpy(courses[0].code, "CSC108");                                          
    strcpy(courses[1].code, "CSC148");                                          
    strcpy(courses[2].code, "CSC209"); 
    
    struct sockname *head = malloc(sizeof(struct sockname));
    struct sockname **all_clients = &head;
    head->next = NULL;
    strcpy(head->username, "END");
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("server: socket");
        exit(1);
    }
    int on = 1;
    int status = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR,
                            (const char *) &on, sizeof(on));
    if(status == -1) {
        perror("setsockopt -- REUSEADDR");
    }

    // Set information about the port (and IP) we want to be connected to.
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = INADDR_ANY;

    // This should always be zero. On some systems, it won't error if you
    // forget, but on others, you'll get mysterious errors. So zero it.
    memset(&server.sin_zero, 0, 8);


  
      
    // Bind the selected port to the socket.
    if (bind(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("server: bind");
        close(sock_fd);
        exit(1);
    }

    // Announce willingness to accept connections on this socket.
    if (listen(sock_fd, MAX_BACKLOG) < 0) {
        perror("server: listen");
        close(sock_fd);
        exit(1);
    }       
         
     

    // The client accept - message accept loop. First, we prepare to listen to multiple
    // file descriptors by initializing a set of file descriptors.
    int max_fd = sock_fd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(sock_fd, &all_fds);

    while (1) {
           // select updates the fd_set it receives, so we always use a copy and retain the original.
        fd_set listen_fds = all_fds;
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }
        
        // Is it the original socket? Create a new connection ...
        if (FD_ISSET(sock_fd, &listen_fds)) {
            int client_fd = accept_connection(sock_fd, all_clients);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            FD_SET(client_fd, &all_fds);
            printf("Accepted connection\n");
        }
        
        
        struct sockname *curr = *all_clients; 
        while (curr->next != NULL) {
            if (curr->sock_fd > -1 && FD_ISSET(curr->sock_fd, &listen_fds)){
                int client_closed = buffer_input(curr); 
                if (client_closed > 0) {
                    if (strcmp(curr->ta_or_s, "S")==0) {
                      if (!give_up_waiting(&stu_list, curr->username)) {
                          printf("student successfully gave up\n");
                      }  
                    }
                    if (strcmp(curr->ta_or_s, "T")==0){
                        if(!remove_ta(&ta_list, curr->username)){
                            printf("TA successfully removed\n");
                        }
                    }
                    FD_CLR(client_closed, &all_fds);
                    curr->sock_fd = -1;
                    garbage_disposal(all_clients, 0);
                    printf("Client %d disconnected\n", client_closed);
                }else {
                    
                    printf("Responding to client %d\n", curr->sock_fd);
                }
            }
            curr = curr->next;
        }
    }
    // free and delete everything
    garbage_disposal(all_clients, 1);
    // Should never get here.
    return 1;
}
