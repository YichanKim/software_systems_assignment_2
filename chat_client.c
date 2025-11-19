
#include <stdio.h>
#include "udp.h"
#include <pthread.h>

#define CLIENT_PORT 10000

typedef struct{
    int socket_descriptor; //socket descriptor
    struct sockaddr_in server_addr; //sever address
    int running; //running flag
} client_info;

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
        if (len > 0 && client_request[len-1] == '\n') {
            client_request[len-1] = '\0'; //replace '\n'
            len = len -1; //decrement len
        }

        //ignore empty lines
        if (len == 0) {
            continue;
        }

        //return code
        //udp_socket_write(int sd, struct sockaddr_in *addr, char *buffer, int n)
        // rc < 0 is error
        int rc = udp_socket_write(state->socket_descriptor, &state->server_addr, client_request, len);

        if (rc < 0){
            fprintf(stderr, "udp socket write\n");
            break;
        }
        
        //if the stdin in client_request buffer is disconn$, set the running state to 0
        if(strncmp(client_request, "disconn$", 8) == 0){
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
            
            printf("%s", server_response);

            if (strcmp(server_response, "Disconnected. Bye!") == 0) {
                state->running = 0;
                break;
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
    // and port number CLIENT_PORT.
    // (See details of the function in udp.h)
    int sd = udp_socket_open(CLIENT_PORT);

    // Variable to store the server's IP address and port
    // (i.e. the server we are trying to contact).
    // Generally, it is possible for the responder to be
    // different from the server requested.
    // Although, in our case the responder will
    // always be the same as the server.
    client_info state;
    state.socket_descriptor = sd;
    state.running = 1;


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
    printf("exiting client");
    return 0;
}