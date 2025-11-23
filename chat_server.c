
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include "udp.h"
#define MAX_NAME_LEN 256

// This is a structure representing a single client in our chat system
// This is a node in our linked list 
typedef struct client_node { 
    char client_name[MAX_NAME_LEN];
    struct sockaddr_in client_address; //Stores IP and port needed to send back to the client

    struct client_node *next; //Builds the linked list

    time_t last_active_time;

    int is_admin;

} client_node_t; // I'll add mute tracking later

typedef struct {
    client_node_t *head;

    pthread_rwlock_t lock;

} client_list_t;

//Global client list - shared by all threads
//This is where we store all the connected clients
client_list_t client_list; 

//Initialize the client list
void init_client_list() {
    client_list.head = NULL;

    //Initialize the read-write lock
    int rc = pthread_rwlock_init(&client_list.lock, NULL);

    if (rc != 0) {
        fprintf(stderr, "Failed to initialize reader-writer lock\n");
        exit(1);
    }
    printf("Client list initialized\n");
}

// Function to clean up the client list (Called on server shutdown)
void destroy_client_list() {
    // Acquire the write lock
    pthread_rwlock_wrlock(&client_list.lock);

    //Free all the client nodes
    client_node_t *current = client_list.head;
    while (current != NULL) {
        client_node_t *temp = current->next;
        free(current);
        current = temp;
    }

    client_list.head = NULL;

    // Release the write lock
    pthread_rwlock_unlock(&client_list.lock);

    // Destroy the lock itself
    pthread_rwlock_destroy(&client_list.lock);

    printf("Client list destroyed\n");
}

// Function to add a new client to the list
client_node_t *add_client(const char *client_name, struct sockaddr_in *client_address, int is_admin) {
    // Allocate memory for the new client node
    client_node_t *new_node = (client_node_t *)malloc(sizeof(client_node_t));

    if (new_node == NULL) {
        fprintf(stderr, "Failed to allocate memory for new client\n");
        return NULL;
    }

    // Copy the client name into the node
    // Use strncpy instead of strcpy to avoid buffer overflow
    strncpy(new_node->client_name, client_name, MAX_NAME_LEN - 1);
    new_node->client_name[MAX_NAME_LEN - 1] = '\0'; // Ensure null termination (for safety)

    // Copy the client address into the node
    new_node->client_address = *client_address;

    // Set admin flag
    new_node->is_admin = is_admin;

    // Set the last active time to the current time
    new_node->last_active_time = time(NULL);


    // Acquire the write lock
    pthread_rwlock_wrlock(&client_list.lock);

    // Add node to the FRONT of the list
    new_node->next = client_list.head;

    client_list.head = new_node;

    pthread_rwlock_unlock(&client_list.lock);

    printf("Client %s added to the list\n", client_name);
    return new_node;
}

int main(int argc, char *argv[])
{

    // This function opens a UDP socket,
    // binding it to all IP interfaces of this machine,
    // and port number SERVER_PORT
    // (See details of the function in udp.h)
    int sd = udp_socket_open(SERVER_PORT);

    assert(sd > -1);

    // Server main loop
    while (1) 
    {
        // Storage for request and response messages
        char client_request[BUFFER_SIZE], server_response[BUFFER_SIZE];

        // Demo code (remove later)
        printf("Server is listening on port %d\n", SERVER_PORT);

        // Variable to store incoming client's IP address and port
        struct sockaddr_in client_address;
    
        // This function reads incoming client request from
        // the socket at sd.
        // (See details of the function in udp.h)
        int rc = udp_socket_read(sd, &client_address, client_request, BUFFER_SIZE);

        // Successfully received an incoming request
        if (rc > 0)
        {
            // Demo code (remove later)
            strcpy(server_response, "Hi, the server has received: ");
            strcat(server_response, client_request);
            strcat(server_response, "\n");

            // This function writes back to the incoming client,
            // whose address is now available in client_address, 
            // through the socket at sd.
            // (See details of the function in udp.h)
            rc = udp_socket_write(sd, &client_address, server_response, BUFFER_SIZE);

            // Demo code (remove later)
            printf("Request served...\n");
        }
    }

    return 0;
}