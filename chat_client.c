
#include <stdio.h>
#include "udp.h"
#include <pthread.h>
#include <string.h>
#include <ctype.h> //for isspace

#define CLIENT_PORT 10000
#define MAX_NAME_LEN 10

typedef struct{
    int socket_descriptor; //socket descriptor
    struct sockaddr_in server_addr; //sever address
    int running; //running flag

    char client_name[MAX_NAME_LEN];
    int is_connected; //connected to server flag

    FILE *chat_write_file; //where we will be storing the output of incoming messages from server
} client_info;

/// @brief Validates if a client request is in proper format
/// @param request Input processed request wer are trying to validate
/// @return 1 if format is valid, else 0
int validate_request_format(const char *request){
    //Check for $
    const char *dollar_sign = strchr(request, '$');
    if (dollar_sign == NULL){
        fprintf(stderr, "$ Error: missing '$' sign in input\n");
        return 0;
    }

    size_t cmd_len = dollar_sign - request;

    if (cmd_len == 0){
        fprintf(stderr, "Command Error: No command detected\n");
        return 0;
    }

    //check for content after $
    if (*(dollar_sign + 1) == '\0') {
        fprintf(stderr, "Input Error: No content after $\n");
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

    //check if state of client is running and on success of writing stdin to client_request buffer
    while (state->running && fgets(client_request, sizeof(client_request), stdin) != NULL)
    {
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
        if (strncmp(processed_request, "disconn$", 8) == 0){
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
            state->running = 0;
            break;
        }
    }
    state->running = 0;
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

    //check if state of client is running
    while (state->running){
        // This function reads the response from the server
        // through the socket at sd.
        // In our case, responder_addr will simply be
        // the same as server_addr.
        // (See details of the function in udp.h)
        int rc = udp_socket_read(state->socket_descriptor, &responder_addr, server_response, (BUFFER_SIZE-1));
    
        //rc is the number of bytes retrieved
        if (rc > 0){
            server_response[rc] = '\0';
            
            // After checking if file has been successfuly opened
            // Writes server response to file
            // Flusehes file buffer so that the write to file happens instantly
            if (state->chat_write_file){
                fprintf(state->chat_write_file, "%s", server_response);
                fflush(state->chat_write_file);
            }
            
            printf("%s", server_response);

            if (strcmp(server_response, "Disconnected. Bye!") == 0) {
                state->running = 0;
                break;
            }

            //If the first 3 characters from server_response is "Hi ", set is_connected to high and set the name
            //we MUST make sure that there are no spaces in names AND no commas
            if (strncmp(server_response, "Hi ", 3) == 0) {
                const char *start = server_response + 3;
                const char *end = strchr(start, ',');

                if (end != NULL){
                     int name_len = end - start;
                    if (name_len >= MAX_NAME_LEN){
                        //cut the name to fit the max_name_len
                        name_len = MAX_NAME_LEN - 1;
                    }

                    strncpy(state->client_name, start, name_len);
                    state->client_name[name_len] = '\0';
                    state->is_connected = 1;
                }
            }
        }
        //error case
        else if (rc < 0)
        {
            fprintf(stderr, "udp socket read error\n");
            break;
        }
    }
    //if break from prev while loop, terminate thread
    state->running = 0;
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

    //fopen iChat.txt, which would be the text file that we store incoming messages
    state.chat_write_file = fopen("iChat.txt", "w"); //we give it write permission only as file will be read with tail command

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
    state.running = 0;
    //wait for listener thread terimination
    pthread_join(listener_tid, NULL);

    close(sd);

    //close ichat.txt
    fclose(state.chat_write_file);

    printf("exiting client");
    return 0;
}