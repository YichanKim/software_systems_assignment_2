
#include <stdio.h>
#include "udp.h"
#include <pthread.h>
#include <string.h>
#include <ctype.h> //for isspace
#include <unistd.h> //for getpid

#define CLIENT_PORT 10000
#define MAX_NAME_LEN 256

//Make sure name cannot have '$'


/**
 * We may need locks for:
 *  1. running flag (Both listener and writer threads manipulate variable)
 *  2. is_connected (written by listener, read by writer)
 *  3. client_name (written by listener, read by writer)
 *  4. chat_write_file (written by listener, closed by main)
 */

typedef struct{
    int socket_descriptor; //socket descriptor
    struct sockaddr_in server_addr; //sever address
    int running; //running flag

    char client_name[MAX_NAME_LEN];
    int is_connected; //connected to server flag

    FILE *chat_write_file; //where we will be storing the output of incoming messages from server

    pthread_mutex_t lock; //Used for locks
} client_info;

// Parse request string into command type and content (format: "command$content")
int parse_acknowledge(const char *request, char *command_type, char *content) {
    const char *dollar_sign = strchr(request, '$');
    if (dollar_sign == NULL) {
        printf("Invalid acknowledge format: no '$' delimiter found\n");
        return -1;
    }
    size_t command_len = dollar_sign - request;
    if (command_len == 0 || command_len >= BUFFER_SIZE) {
        printf("Invalid acknowledge format: command type invalid\n");
        return -1;
    }
    strncpy(command_type, request, command_len);
    command_type[command_len] = '\0';
    size_t content_len = strlen(dollar_sign + 1);
    if (content_len >= BUFFER_SIZE) {
        printf("Invalid acknowledge format: content too long\n");
        return -1;
    }
    strncpy(content, dollar_sign + 1, BUFFER_SIZE - 1);
    content[BUFFER_SIZE - 1] = '\0';
    return 0;
}

void route_acknowledge(const char*request, void *arg) {
    client_info *state = (client_info *)arg;
    char command_type[BUFFER_SIZE];
    char content[BUFFER_SIZE];

    int parse_rc = parse_acknowledge(request, command_type, content);

    if(parse_rc != 0) {
        fprintf(stderr, "Error$ Invalid acknowledge format. Expected 'command$content' from server\n");
        return;
    }

    if(strcmp(command_type, "conn") == 0) {
        const char *start = content + 3;
        const char *end = strchr(start, ',');

        if (end != NULL && end > start){
            int name_len = end - start;
            if (name_len >= MAX_NAME_LEN){
                //cut the name to fit the max name len
                name_len = MAX_NAME_LEN - 1;
            }

            printf("%s", content);
            pthread_mutex_lock(&state->lock);
            strncpy(state->client_name, start, name_len);
            state->client_name[name_len] = '\0';
            state->is_connected = 1;
            pthread_mutex_unlock(&state->lock);

        }
    } else if (strcmp(command_type, "rename") == 0) {
        // Server confirmed rename - update local name
        // Format: "rename$ You are now known as NewName\n"
        // Extract new name from content
        const char *prefix = "You are now known as ";
        const char *name_start = strstr(content, prefix);
        
        if (name_start != NULL) {
            name_start += strlen(prefix);  // Move past the prefix
            
            // Find end of name (newline or end of string)
            size_t name_len = 0;
            while (name_start[name_len] != '\n' && name_start[name_len] != '\0' && name_len < MAX_NAME_LEN - 1) {
                name_len++;
            }
            
            // Update client name
            pthread_mutex_lock(&state->lock);
            strncpy(state->client_name, name_start, name_len);
            state->client_name[name_len] = '\0';
            pthread_mutex_unlock(&state->lock);
            
            // Display confirmation
            printf("%s", content);
        } else {
            // Fallback - just display the message
            printf("%s", content);
        }
    } else if (strcmp(command_type, "sayto") == 0){
        pthread_mutex_lock(&state->lock);
        // After checking if file has been successfuly opened
        // Writes server response to file
        // Flusehes file buffer so that the write to file happens instantly    
        if (state->chat_write_file){
                fprintf(state->chat_write_file, "%s", content);
                fflush(state->chat_write_file);
            }
        pthread_mutex_unlock(&state->lock);
    } else if(strcmp(command_type, "say") == 0){
        pthread_mutex_lock(&state->lock);
        // After checking if file has been successfuly opened
        // Writes server response to file
        // Flusehes file buffer so that the write to file happens instantly    
        if (state->chat_write_file){
                fprintf(state->chat_write_file, "%s", content);
                fflush(state->chat_write_file);
            }
        pthread_mutex_unlock(&state->lock);
    } else if (strcmp(command_type, "disconn") == 0) {
        printf("%s\n", content);
        pthread_mutex_lock(&state->lock);
        state->running = 0;
        pthread_mutex_unlock(&state->lock);
        //break;
    } else if (strcmp(command_type, "kick") == 0) {
        // Server kicked this client - display message and disconnect
        printf("%s", content);  // Display kick message
        pthread_mutex_lock(&state->lock);
        state->running = 0;  // Stop client
        pthread_mutex_unlock(&state->lock);
    } else if (strcmp(command_type, "ping") == 0) {
        // Server is pinging us - respond immediately with ret-ping
        char ret_ping_msg[BUFFER_SIZE];
        snprintf(ret_ping_msg, BUFFER_SIZE, "ret-ping$\n");
        
        pthread_mutex_lock(&state->lock);
        udp_socket_write(state->socket_descriptor, &state->server_addr, ret_ping_msg, strlen(ret_ping_msg));
        pthread_mutex_unlock(&state->lock);
        
        printf("[DEBUG] Responded to server ping\n");
    } else if(strcmp(command_type, "history") == 0) {
        pthread_mutex_lock(&state->lock);
        // After checking if file has been successfuly opened
        // Writes server response to file
        // Flusehes file buffer so that the write to file happens instantly    
        if (state->chat_write_file){
                fprintf(state->chat_write_file, "%s", content);
                fflush(state->chat_write_file);
            }
        pthread_mutex_unlock(&state->lock);
    } else {
        fprintf(stderr, "Error$ Error from Server. Please make appropriate changes.\n");
        return;
    }
}

/// @brief Validates if a client request is in proper format
/// @param request Input processed request wer are trying to validate
/// @return 1 if format is valid, else 0
int validate_request_format(const char *request){
    //Check for $
    const char *dollar_sign = strchr(request, '$');
    if (dollar_sign == NULL){
        fprintf(stderr, "$ Error$ missing '$' sign in input\n");
        return 0;
    }

    size_t cmd_len = dollar_sign - request;

    if (cmd_len == 0){
        fprintf(stderr, "Command Error$ No command detected\n");
        return 0;
    }

    //check for content after $
    if (*(dollar_sign + 1) == '\0') {
        fprintf(stderr, "Input Error$ No content after $\n");
        return 0;
    }

    return 1; //Individual commands are to be validated in server
}

 
void *writer_thread(void *arg)
{
    //convert arg back to a pointer to client info
    client_info *state = (client_info *)arg;
    
    // Storage for request messages
    char client_request[BUFFER_SIZE];

    //allows constant check of state->running
    while(1) {
        pthread_mutex_lock(&state->lock);
        int running = state->running;
        pthread_mutex_unlock(&state->lock);

        if (!running){
            break;
        }

        if (!(fgets(client_request, sizeof(client_request), stdin) != NULL)){
            break;
        }


        //Might need implementation of is_connected flag and ask user to connect first when sending an invalid req
        //check if state of client is running and on success of writing stdin to client_request buffer
        size_t len = strlen(client_request);
        while (len > 0 && client_request[len-1] == '\n') {
            client_request[len-1] = '\0'; //replace '\n'
            len = len -1; //decrement len
        }

        char *processed_request = client_request;
        //get rid of leading whitespace
        while (len > 0 && isspace((unsigned char) *processed_request)) { //Essentially scrolls through white spcae
            processed_request++;
            len = len -1; // decrement len
        }

        //get rid of trailing whitespace
        while (len > 0 && isspace((unsigned char) processed_request[len-1])) { //Essentially scrolls through white spcae
            processed_request[len-1] = '\0';
            len = len -1; // decrement len
        }
        
        //Empty line error message
        if (len == 0) {
            fprintf(stderr, "Empty input detected. Please enter input.\n");
            continue;
        }
        
        //if the stdin in client_request buffer is disconn$, set disconnect_flag to 1
        int disconnect_flag = 0;
        if (strcmp(processed_request, "disconn$") == 0){
            disconnect_flag = 1;
        }

        if (!disconnect_flag) {
            if (!validate_request_format(processed_request)){
                //don't send if it is invalid, wait for new stdin
                continue;
            }
        }

        //return code
        //udp_socket_write(int sd, struct sockaddr_in *addr, char *buffer, int n)
        // rc < 0 is error
        int rc = udp_socket_write(state->socket_descriptor, &state->server_addr, processed_request, len);

        if (rc < 0){
            fprintf(stderr, "udp socket write\n");
            break;
        }

        if (disconnect_flag) {
            pthread_mutex_lock(&state->lock);
            state->running = 0;
            pthread_mutex_unlock(&state->lock);
            break;
        }

    }
    //If disconnect/socket write error/ safety check
    pthread_mutex_lock(&state->lock);
    state->running = 0;
    pthread_mutex_unlock(&state->lock);
    return NULL;
}

void *listener_thread(void *arg)
{
    //convert arg back to a pointer to client info
    client_info *state = (client_info *)arg;

    // Storage for response messages
    char server_response[BUFFER_SIZE];

    // creates variable to store responder address
    struct sockaddr_in responder_addr;

    //allows constant check of state->running
    while(1) {
        pthread_mutex_lock(&state->lock);
        int running = state->running;
        pthread_mutex_unlock(&state->lock);
        //check if state of client is running
        if (!running){
            break;
        }
        // This function reads the response from the server
        // through the socket at sd.
        // In our case, responder_addr will simply be
        // the same as server_addr.
        // (See details of the function in udp.h)
        int rc = udp_socket_read(state->socket_descriptor, &responder_addr, server_response, (BUFFER_SIZE-1));
    
        //rc is the number of bytes retrieved
        if (rc > 0){
            server_response[rc] = '\0';

            //debugging
            printf("[DEBUG] %s", server_response);

            //If the first 3 characters from server_response is "Hi ", set is_connected to high and set the name
            //we MUST make sure that there are no spaces in names AND no commas

            //REPLACE WITH HANDLE CONN FUNCTION WIP
            route_acknowledge(server_response, arg);
        }
        //error case
        else if (rc < 0)
        {
            fprintf(stderr, "udp socket read error\n");
            break;
        }
    }
    //if break from prev while loop, terminate thread
    //If disconnect/socket read error/ safety check
    pthread_mutex_lock(&state->lock);
    state->running = 0;
    pthread_mutex_unlock(&state->lock);
    return NULL;
}

// client code
int main(int argc, char *argv[])
{
    // This function opens a UDP socket,
    // binding it to all IP interfaces of this machine,
    // and port number ANY FREE PORT CHOSEN BY OS (by passing 0).
    // (See details of the function in udp.h)
    int sd = udp_socket_open(0); 

    // Variable to store the server's IP address and port
    // (i.e. the server we are trying to contact).
    // Generally, it is possible for the responder to be
    // different from the server requested.
    // Although, in our case the responder will
    // always be the same as the server.
    client_info state;
    state.socket_descriptor = sd;
    state.running = 1;
    state.is_connected = 0;
    state.client_name[0] = '\0';

    pthread_mutex_init(&state.lock, NULL);

    char chat_file_name[100];
    int pid = getpid();

    //append file name to chat_file_name and return the character count
    int n = snprintf(chat_file_name, sizeof(chat_file_name), "iChat_%d.txt", pid);

    if (n < 0 || n >= sizeof(chat_file_name)) {
        fprintf(stderr, "[DEBUG] Error creating filename for PID %d\n", pid);
        close(sd);
        return 1;
    }

    //debugging
    printf("[DEBUG] tail -f %s\n", chat_file_name);

    //fopen iChat.txt, which would be the text file that we store incoming messages
    state.chat_write_file = fopen(chat_file_name, "w"); //we give it write permission only as file will be read with tail command

    if (!state.chat_write_file){ //fopen fail error
        fprintf(stderr, "fopen error for ichat.txt");
        close(sd);
        return 1;
    }

    // Initializing the server's address.
    // We are currently running the server on localhost (127.0.0.1).
    // You can change this to a different IP address
    // when running the server on a different machine.
    // (See details of the function in udp.h)
    int rc = set_socket_addr(&state.server_addr, "127.0.0.1", SERVER_PORT);

    if (rc < 0){
        fprintf(stderr, "set socket addr failed\n");
        close(sd);
        return 1;
    }
    
    //setup two process threads ids
    pthread_t writer_tid, listener_tid;

    //create listener thread
    if (pthread_create(&listener_tid, NULL, listener_thread, &state) != 0){
        fprintf(stderr, "pthrread_create for listener failed");
        close(sd);
        return 1;
    }

    //create writer thread
    if (pthread_create(&writer_tid, NULL, writer_thread, &state) != 0) {
        fprintf(stderr, "pthread_create for writer failed");
        close(sd);
        return 1;
    }

    //wait for writer thread termination
    pthread_join(writer_tid,NULL);
    //on writer thread termination, set running state to 0
    pthread_mutex_lock(&state.lock);
    state.running = 0;
    pthread_mutex_unlock(&state.lock);
    //wait for listener thread terimination
    pthread_join(listener_tid, NULL);

    close(sd);
    pthread_mutex_lock(&state.lock);
    //close ichat.txt
    fclose(state.chat_write_file);
    pthread_mutex_unlock(&state.lock);

    pthread_mutex_destroy(&state.lock);
    printf("[DEBUG] exiting client\n");
    return 0;
}