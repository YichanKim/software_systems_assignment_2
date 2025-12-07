
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include "udp.h"

#define MAX_NAME_LEN 256

//Helper function to do trimming
char* trim(char *str){
    if (!str){
        return str;
    }

    //Move pointer forward if it is just a space
     while (*str == ' '){
        str++;
    }

    char *end = str + strlen(str);
        while (end > str && *(end - 1) == ' ') {
            end--;
        }
    *end = '\0'; // Null-terminate at the trimmed position
    
    return str;
}


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

// Structure to pass data to worker threads
// Contains request message, client address, and socket descriptor
typedef struct {
    char request[BUFFER_SIZE];
    struct sockaddr_in client_address;
    int socket_descriptor;
} request_handler_t;

typedef struct{
    char messages_history [15] [BUFFER_SIZE];
    int current_index_pointer;
    int message_count;
    pthread_mutex_t lock;
} chat_history_t;

//Global client list - shared by all threads
//This is where we store all the connected clients
client_list_t client_list; 

//Global chat history list - shared by all threads
//This is where we store all the chat history
chat_history_t chat_history;

//Initialize the client list
void init_client_list() {
    client_list.head = NULL;

    //Initialize the read-write lock
    int rc = pthread_rwlock_init(&client_list.lock, NULL);

    if (rc != 0) {
        fprintf(stderr, "Failed to initialize reader-writer lock\n");
        exit(1);
    }
    printf("[DEBUG] Client list initialized\n");
}

void init_chat_history() {
    chat_history.current_index_pointer = 0;
    chat_history.message_count = 0;
    //empties chat history
    memset(chat_history.messages_history, 0, sizeof(chat_history.messages_history));
    //initialise the lock
    pthread_mutex_init(&chat_history.lock, NULL);
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

    printf("[DEBUG] Client list destroyed\n");
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

    printf("[DEBUG] Client %s added to the list\n", client_name);
    return new_node;
}

// Helper function to find a client by name while lock is already held
client_node_t *find_client_by_name_locked(const char *client_name) {
    client_node_t *current = client_list.head;

    while (current != NULL) {
        if (strcmp(current->client_name, client_name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// Function to find a client by their chat name
client_node_t *find_client_by_name(const char *client_name) {
    // Acquire the read lock
    pthread_rwlock_rdlock(&client_list.lock);

    // Find the client by name
    client_node_t *result = find_client_by_name_locked(client_name);

    pthread_rwlock_unlock(&client_list.lock);

    return result;
}

// Function to find a client by their IP address and port number
client_node_t *find_client_by_address(struct sockaddr_in *client_address) {
    pthread_rwlock_rdlock(&client_list.lock);

    client_node_t *current = client_list.head;
    while (current != NULL) {
        if (current->client_address.sin_addr.s_addr == client_address->sin_addr.s_addr && current->client_address.sin_port == client_address->sin_port) {
            pthread_rwlock_unlock(&client_list.lock);
            return current;
        }
        current = current->next;
    }
    // Not found - release lock and return NULL
    pthread_rwlock_unlock(&client_list.lock);
    return NULL;
}

// Function to remove a client from the list by their name
int remove_client_by_name(const char *client_name) {
    pthread_rwlock_wrlock(&client_list.lock);
    
    if (client_list.head != NULL && 
        strcmp(client_list.head->client_name, client_name) == 0) {
        // Head node matches - we need to remove it
        
        // Save pointer to the node we're about to remove
        client_node_t *to_remove = client_list.head;
        
        // Move head pointer to the next node (or NULL if it was the only node)
        client_list.head = client_list.head->next;
        
        // Now we can safely free the old head node
        free(to_remove);
        
        // Release lock and return success
        pthread_rwlock_unlock(&client_list.lock);
        printf("[DEBUG] Client '%s' removed from list (was head node)\n", client_name);
        return 0;
    }

    client_node_t *current = client_list.head;

    while (current != NULL && current->next != NULL) {
        // Check if the NEXT node is the one we want to remove
        if (strcmp(current->next->client_name, client_name) == 0) {
            
            client_node_t *to_remove = current->next;
            
            current->next = to_remove->next;

            free(to_remove);
            
            pthread_rwlock_unlock(&client_list.lock);
            printf("[DEBUG] Client '%s' removed from list\n", client_name);
            return 0;
        }
        
        // Move to next node
        current = current->next;
    }
    
    // If we get here, we didn't find the client to remove
    pthread_rwlock_unlock(&client_list.lock);
    printf("[DEBUG] Client '%s' not found for removal\n", client_name);
    return -1;  // Not found
}

// Function to remove a client by their IP address and port
int remove_client_by_address(struct sockaddr_in *client_address) {
    pthread_rwlock_wrlock(&client_list.lock);
    
    // Check if head node matches
    if (client_list.head != NULL) {
        if (client_list.head->client_address.sin_addr.s_addr == client_address->sin_addr.s_addr &&
            client_list.head->client_address.sin_port == client_address->sin_port) {
            // Head node matches - remove it
            client_node_t *to_remove = client_list.head;
            client_list.head = client_list.head->next;
            free(to_remove);
            pthread_rwlock_unlock(&client_list.lock);
            return 0;
        }
    }

    // Check rest of list (same logic as remove_client_by_name)
    client_node_t *current = client_list.head;
    while (current != NULL && current->next != NULL) {
        if (current->next->client_address.sin_addr.s_addr == client_address->sin_addr.s_addr &&
            current->next->client_address.sin_port == client_address->sin_port) {
            // Found it!
            client_node_t *to_remove = current->next;
            current->next = to_remove->next;
            free(to_remove);
            pthread_rwlock_unlock(&client_list.lock);
            return 0;
        }
        current = current->next;
    }

    // Not found
    pthread_rwlock_unlock(&client_list.lock);
    return -1;
}

// Function to update a client's last active time when they send a request
void update_client_active_time(struct sockaddr_in *client_address) {
    pthread_rwlock_wrlock(&client_list.lock);
    client_node_t *current = client_list.head;
    while (current != NULL) {
        if (current->client_address.sin_addr.s_addr == client_address->sin_addr.s_addr &&
            current->client_address.sin_port == client_address->sin_port) {
            current->last_active_time = time(NULL);
            pthread_rwlock_unlock(&client_list.lock);
            return;
        }
        current = current->next;
    }
    pthread_rwlock_unlock(&client_list.lock);
}

void add_to_history(const char *message) {
    
    //we assume that message is less than the buffer size.
    pthread_mutex_lock(&chat_history.lock);
    
    strcpy(chat_history.messages_history[chat_history.current_index_pointer], message);
    chat_history.current_index_pointer = (chat_history.current_index_pointer+1) % 15;
    
    if (chat_history.message_count < 15){
        chat_history.message_count += 1;
    }
    pthread_mutex_unlock(&chat_history.lock);
}

int get_history(char output_buffer[15][BUFFER_SIZE]){
    pthread_mutex_lock(&chat_history.lock);

    int count = chat_history.message_count;

    if (count == 0){
        pthread_mutex_unlock(&chat_history.lock);
        return 0; //return no messages (no history)
    }

    int start_index = 0;

    //here we have looping so then the oldest message is chat_hsitory.current_index_pointer
    //if not looping is done in get history, then the oldest message is at 0
    if (count == 15){
        start_index = chat_history.current_index_pointer;
    }

    //we assume message_history already has history$ prefix with null termination and new line
    for (int i = 0; i < count; i++){
        int index = (start_index + i) % 15; //account for looping
        strcpy(output_buffer[i], chat_history.messages_history[index]);
    }

    pthread_mutex_unlock(&chat_history.lock);
    return count;
}

// Parse request string into command type and content (format: "command$content")
int parse_request(const char *request, char *command_type, char *content) {
    const char *dollar_sign = strchr(request, '$');
    if (dollar_sign == NULL) {
        printf("[DEBUG] Invalid request format: no '$' delimiter found\n");
        return -1;
    }
    size_t command_len = dollar_sign - request;
    if (command_len == 0 || command_len >= BUFFER_SIZE) {
        printf("[DEBUG] Invalid request format: command type invalid\n");
        return -1;
    }
    strncpy(command_type, request, command_len);
    command_type[command_len] = '\0';
    size_t content_len = strlen(dollar_sign + 1);
    if (content_len >= BUFFER_SIZE) {
        printf("[DEBUG] Invalid request format: content too long\n");
        return -1;
    }
    strncpy(content, dollar_sign + 1, BUFFER_SIZE - 1);
    content[BUFFER_SIZE - 1] = '\0';
    return 0;
}

void handle_conn(const char *content, struct sockaddr_in *client_address, int socket_descriptor){
    //trim(content);
    //Assume that content is already trimmed -> could be done in the route request function
    
    size_t len = strlen(content);
    
    if (len == 0 || len >= MAX_NAME_LEN){
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error$ No name or too long of a name. Expected 'conn$ [NAME]'\n");
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
        return;
    }

    //Check if name already exists
    if (find_client_by_name(content) != NULL){
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error$ Name already taken. Please choose another name\n");
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
        return;
    }

    //Determine if the client is admin
    int client_port = ntohs(client_address->sin_port); //undos htons as seen in udp.h. Converts back (network-short-to-host)
    printf("[DEBUG] Client_port: %d\n", client_port);
    int is_admin = 0;

    if (client_port == 6666){
        is_admin = 1;
    }

    client_node_t *added_client_node = add_client(content, client_address, is_admin);

    if(added_client_node == NULL){
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error$ Error adding client to client list\n");
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
        return;
    }

    update_client_active_time(client_address);

    //send connection response
    char response[BUFFER_SIZE];
    snprintf(response, BUFFER_SIZE, "conn$ Hi %s, you have successfully connected to the chat\n", content);
    udp_socket_write(socket_descriptor, client_address, response, strlen(response));

    //send history messages
    char history_messages[15][BUFFER_SIZE];
    int history_message_count = get_history(history_messages);

    for (int i = 0; i < history_message_count; i++){
        udp_socket_write(socket_descriptor, client_address, history_messages[i], strlen(history_messages[i]));

    }

    return;
}

void handle_say(const char *content, struct sockaddr_in *client_address, int socket_descriptor){
    size_t len = strlen(content);
    
    //invalid message
    if (len == 0 || len >= MAX_NAME_LEN){
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error$ No message content or too long of a message. Expected 'say$ [MESSAGE]'\n");
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
        return;
    }
    
    client_node_t *sender_address = find_client_by_address(client_address);
    //We can't find the client in the client list. Therefore, send error to client and ask to connect first
    if (sender_address == NULL){
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error$ You have not connected to server yet. Please connect to server using 'conn$ [NAME].\n");
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
        return;
    }

    //message preparation
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "say$ %s: %s\n", sender_address->client_name, content);
    
    //send message
    //broadcast_message(message, client_address, socket_descriptor);

    pthread_rwlock_rdlock(&client_list.lock);
    //logic from find_client_by_address
    client_node_t *current = client_list.head;
    while (current != NULL) {
        //write to all clients
        udp_socket_write(socket_descriptor, &current->client_address, (char *) message, strlen(message));
        current = current->next;
    }
    pthread_rwlock_unlock(&client_list.lock);

    //add to history
    char history_message[BUFFER_SIZE];
    snprintf(history_message, BUFFER_SIZE, "history$ %s: %s\n", sender_address->client_name, content);
    add_to_history(history_message);

    //housekeeping
    update_client_active_time(client_address);
    return;
}

//parses message content to find recipient and message content (space seperated)
//returns 0 on sucess, 1 on fail
//very similar (practically the same) to parsing for command type with $
int parse_sayto(const char *content, char* recipient_name, char *message_content){
    const char *space = strchr(content, ' ');
    if (space == NULL){
        printf("[DEBUG] Invalid sayto message format: no space delimiter found\n");
        return 1;
    }
    
    size_t name_len = space - content;
    if (name_len == 0 || name_len >= MAX_NAME_LEN) {
        printf("[DEBUG] Invalid request format: recipient name invalid\n");
        return 1;
    }

    strncpy(recipient_name, content, name_len);
    recipient_name[name_len] = '\0';
    size_t content_len = strlen(space + 1);
    if (content_len >= BUFFER_SIZE) {
        printf("[DEBUG] Invalid request format: message content too long\n");
        return 1;
    }
    
    strncpy(message_content, space + 1, BUFFER_SIZE - 1);
    message_content[BUFFER_SIZE - 1] = '\0';
    
    // Trim both recipient name and message content
    char *trimmed_name = trim(recipient_name);
    char *trimmed_msg = trim(message_content);
    
    strncpy(recipient_name, trimmed_name, MAX_NAME_LEN - 1);
    recipient_name[MAX_NAME_LEN - 1] = '\0';
    strncpy(message_content, trimmed_msg, BUFFER_SIZE - 1);
    message_content[BUFFER_SIZE - 1] = '\0';
    
    return 0;
}

void handle_sayto(const char *content, struct sockaddr_in *client_address, int socket_descriptor){
    size_t len = strlen(content);
    
    //invalid message
    if (len == 0 || len >= BUFFER_SIZE){
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error$ No message content or too long of a message. Expected 'sayto$ [RECIPEINT NAME] [MESSAGE]'\n");
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
        return;
    }
    
    client_node_t *sender_address = find_client_by_address(client_address);
    //We can't find the client in the client list. Therefore, send error to client and ask to connect first
    if (sender_address == NULL){
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error$ You have not connected to server yet. Please connect to server using 'conn$ [NAME].\n");
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
        return;
    }

    //parsing to find recipeient name and message content here
    char recipient_name[MAX_NAME_LEN];
    char message_content[BUFFER_SIZE];

    int parse_rc = parse_sayto(content, recipient_name, message_content);
    if (parse_rc != 0) {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error$ Expected 'sayto$ [RECIPIENTNAME] [MESSAGE]'\n");
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
        return;
    }

    client_node_t *recipient_address = find_client_by_name(recipient_name);

    //checks if recipient is valid and is in the client_list. If not, retrun error
    if (recipient_address == NULL){
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error$ Recipient not found, Please double check recipient name. Format: 'sayto$ [NAME] [MSG]'.\n");
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
        return;
    }
    
    //message preparation
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "sayto$ %s: %s\n", sender_address->client_name, message_content);
    
    udp_socket_write(socket_descriptor, &recipient_address->client_address, (char *) message, strlen(message));
    //WARNING: THE BELOW LINES ALSO SENDS MESSAGE TO SENDER
    udp_socket_write(socket_descriptor, client_address, (char *) message, strlen(message));

    //housekeeping
    update_client_active_time(client_address);
    return;
}

void handle_disconn(const char *content, struct sockaddr_in *client_address, int socket_descriptor){
    size_t len = strlen(content);
    
    //invalid command (there should be no content)
    if (len != 0){
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error$ Invalid disconn$ command. Expected 'disconn$'\n");
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
        return;
    }
    
    client_node_t *sender_address = find_client_by_address(client_address);
    //We can't find the client in the client list. Send disconnect anyways
    if (sender_address != NULL){
        //remove client if found
        if (remove_client_by_address(client_address) != 0){
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error$ Erorr encountered during removal of client from server. Please try again.\n");
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
        return;
        }
    }
    
    //message preparation
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "disconn$ Disconnected. Bye!\n");
    
    udp_socket_write(socket_descriptor, client_address, (char *) message, strlen(message));
    
    //no need to update client_active_time as we no longer have that in client list anymore.
    return;
}

// Route parsed request to appropriate handler function based on command type
void route_request(const char *request, struct sockaddr_in *client_address, int socket_descriptor) {
    char command_type[BUFFER_SIZE];
    char content[BUFFER_SIZE];
    
    int parse_rc = parse_request(request, command_type, content);

    if (parse_rc != 0) {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error$ Invalid request format. Expected 'command$content'\n");
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
        return;
    }
    
    // Trim command type and content
    char *trimmed_command = trim(command_type);
    char *trimmed_content = trim(content);
    
    if (strcmp(trimmed_command, "conn") == 0) {
        printf("[DEBUG] Routing to handle_conn\n");
        fflush(stdout);
        handle_conn(trimmed_content, client_address, socket_descriptor);
    } else if (strcmp(trimmed_command, "say") == 0) {
        printf("[DEBUG] Routing to handle_say\n");
        fflush(stdout);
        handle_say(trimmed_content, client_address, socket_descriptor);        
    } else if (strcmp(trimmed_command, "sayto") == 0) {
        printf("[DEBUG] Routing to handle_sayto\n");
        handle_sayto(trimmed_content, client_address, socket_descriptor); 
    } else if (strcmp(trimmed_command, "disconn") == 0) {
        printf("[DEBUG] Routing to handle_disconn");
        handle_disconn(trimmed_content, client_address, socket_descriptor);
    } else if (strcmp(trimmed_command, "mute") == 0) {
        printf("[DEBUG] Routing to handle_mute (not implemented yet)\n");
    } else if (strcmp(trimmed_command, "unmute") == 0) {
        printf("[DEBUG] Routing to handle_unmute (not implemented yet)\n");
    } else if (strcmp(trimmed_command, "rename") == 0) {
        printf("[DEBUG] Routing to handle_rename (not implemented yet)\n");
        char response[BUFFER_SIZE];
        snprintf(response, BUFFER_SIZE, "rename$ handler not yet implemented\n");
        udp_socket_write(socket_descriptor, client_address, response, strlen(response));
    } else if (strcmp(trimmed_command, "kick") == 0) {
        printf("[DEBUG] Routing to handle_kick (not implemented yet)\n");
        char response[BUFFER_SIZE];
        snprintf(response, BUFFER_SIZE, "kick$ handler not yet implemented\n");
        udp_socket_write(socket_descriptor, client_address, response, strlen(response));
    } else {
        printf("[DEBUG] Unknown command type: '%s'\n", trimmed_command);
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, 
                 "Error$ Unknown command '%s'. Supported: conn, say, sayto, disconn, mute, unmute, rename, kick\n", trimmed_command);
        udp_socket_write(socket_descriptor, client_address, error_msg, strlen(error_msg));
    }
}

// Worker thread function that processes a single client request
void *handle_request(void *arg) {
    request_handler_t *handler_data = (request_handler_t *)arg;
    char *request = handler_data->request;
    struct sockaddr_in client_address = handler_data->client_address;
    int sd = handler_data->socket_descriptor;
    
    printf("[DEBUG] Worker thread handling request: %s\n", request);
    route_request(request, &client_address, sd);
    free(handler_data);
    return NULL;
}

// Listener thread that continuously waits for incoming requests and spawns worker threads
void *listener_thread(void *arg) {
    int sd = *(int *)arg;
    printf("[DEBUG] Listener thread started, waiting for requests on port %d...\n", SERVER_PORT);
    fflush(stdout);
    
    while (1) {
        char client_request[BUFFER_SIZE];
        struct sockaddr_in client_address;
        int rc = udp_socket_read(sd, &client_address, client_request, BUFFER_SIZE-1);
        
        if (rc > 0) {
            client_request[rc] = '\0';
            printf("[DEBUG] Received request (%d bytes) from client\n", rc);
            fflush(stdout);

            request_handler_t *handler_data = (request_handler_t *)malloc(sizeof(request_handler_t));
            if (handler_data == NULL) {
                fprintf(stderr, "Failed to allocate memory for handler data\n");
                continue;
            }

            //use memcpy for cleaner buffer use
            memcpy(handler_data->request, client_request, rc);
            handler_data->request[rc] = '\0';
            handler_data->client_address = client_address;
            handler_data->socket_descriptor = sd;
            
            pthread_t worker_tid;
            int thread_rc = pthread_create(&worker_tid, NULL, handle_request, handler_data);
            if (thread_rc != 0) {
                fprintf(stderr, "Failed to create worker thread\n");
                free(handler_data);
                continue;
            }
            
            pthread_detach(worker_tid);
        } else if (rc < 0) {
            fprintf(stderr, "Error reading from socket\n");
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{

    // This function opens a UDP socket,
    // binding it to all IP interfaces of this machine,
    // and port number SERVER_PORT
    // (See details of the function in udp.h)
    int sd = udp_socket_open(SERVER_PORT);

    assert(sd > -1);

    //client list init
    init_client_list();

    // Demo code (remove later)
    printf("[DEBUG] Server is listening on port %d\n", SERVER_PORT);

    //listener thread init
    pthread_t listener_tid;
    //return 0 on success
    int listener_thread_rc = pthread_create(&listener_tid, NULL, listener_thread, &sd);

    if (listener_thread_rc != 0) {
        fprintf(stderr, "Error$ listener thread creation error\n");
        close(sd);
        destroy_client_list();
        return 1;
    }

    //keep listener thread alive
    pthread_join(listener_tid, NULL);

    //cleanup
    close(sd);
    destroy_client_list();

    return 0;
}
