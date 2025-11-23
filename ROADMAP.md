# Chat Application Implementation Roadmap

## Table of Contents
1. [Overview](#overview)
2. [Phase 1: Server Foundation](#phase-1-server-foundation)
3. [Phase 2: Client Foundation](#phase-2-client-foundation)
4. [Phase 3: Core Communication](#phase-3-core-communication)
5. [Phase 4: Advanced Features](#phase-4-advanced-features)
6. [Phase 5: User Interface](#phase-5-user-interface)
7. [Phase 6: Proposed Extensions](#phase-6-proposed-extensions)
8. [Testing Checklist](#testing-checklist)

---

## Overview

This roadmap breaks down the implementation of the multithreaded chat application into logical, sequential phases. Each phase builds upon the previous one. Follow this order to ensure dependencies are met.

**Current Status**: Client has basic thread structure, Server has demo code only.

---

## Phase 1: Server Foundation

### Step 1.1: Define Client Data Structure

**Objective**: Create a linked list node structure to store client information.

**Tasks**:
- Create a `client_node` structure containing:
  - `char client_name[MAX_NAME_LEN]` (e.g., "Alice")
  - `struct sockaddr_in client_address` (IP and port)
  - `struct client_node *next` (for linked list)
  - `time_t last_active_time` (for PE2 later, but good to add now)
  - `int is_admin` (boolean flag - true if port is 6666)
- Optionally add `muted_by` list (linked list or array of client names who muted this client)
- Define `MAX_NAME_LEN` constant (e.g., 256)

**Files to Modify**: `chat_server.c`

**Code Structure Hint**:
```c
#define MAX_NAME_LEN 256

typedef struct client_node {
    char client_name[MAX_NAME_LEN];
    struct sockaddr_in client_address;
    struct client_node *next;
    time_t last_active_time;
    int is_admin;
} client_node_t;

typedef struct {
    client_node_t *head;
    pthread_rwlock_t lock;
} client_list_t;
```

**Dependencies**: None

---

### Step 1.2: Initialize Client List and Synchronization

**Objective**: Set up the shared client list with proper synchronization primitives.

**Tasks**:
- Declare a global `client_list_t` variable for the shared list
- Initialize the list (head = NULL)
- Initialize a pthread reader-writer lock (`pthread_rwlock_init`)
- Create helper functions:
  - `init_client_list()` - initialize the list structure
  - `destroy_client_list()` - cleanup function (called on server shutdown)

**Files to Modify**: `chat_server.c`

**Code Structure Hint**:
```c
client_list_t client_list;

void init_client_list() {
    client_list.head = NULL;
    pthread_rwlock_init(&client_list.lock, NULL);
}
```

**Dependencies**: Step 1.1

---

### Step 1.3: Implement Client List Operations

**Objective**: Create thread-safe functions to manipulate the client list.

**Tasks**:
- **`add_client(client_name, client_address, is_admin)`**
  - Write lock the list
  - Create new client_node
  - Add to head or tail of linked list
  - Update last_active_time to current time
  - Unlock
  - Return pointer to new node or NULL on failure

- **`remove_client(client_name)`** or **`remove_client_by_address(address)`**
  - Write lock the list
  - Search for client by name or address
  - Remove from linked list (handle head case, middle case, tail case)
  - Free the node memory
  - Unlock
  - Return 0 on success, -1 if not found

- **`find_client_by_name(client_name)`**
  - Read lock the list
  - Search for client by name
  - Return pointer to node or NULL
  - Unlock
  - Note: Caller must read lock again if they want to use the returned pointer safely

- **`find_client_by_address(address)`**
  - Same as above but search by IP and port
  - Use `memcmp` or compare individual fields of sockaddr_in

- **`update_client_active_time(client_name)`**
  - Write lock the list
  - Find client and update last_active_time
  - Unlock

- **`get_all_clients()`** (helper for broadcasting)
  - Read lock the list
  - Copy client addresses into an array
  - Return count
  - Note: Array should be allocated and caller responsible for freeing

**Files to Modify**: `chat_server.c`

**Dependencies**: Steps 1.1, 1.2

**Important Notes**:
- Always use write lock for add/remove/update operations
- Always use read lock for search operations
- Be careful with memory management (malloc/free)
- Handle edge cases: empty list, removing head node, duplicate names

---

### Step 1.4: Convert Server to Multithreaded Architecture

**Objective**: Replace the simple while loop with a listener thread that spawns worker threads.

**Tasks**:
- Create a `listener_thread` function:
  - Opens UDP socket (already done in main)
  - Infinite loop that calls `udp_socket_read`
  - When request received, spawn a worker thread to handle it
  - Pass client request and client_address to worker thread

- Create a `request_handler` structure to pass data to worker threads:
  ```c
  typedef struct {
      char request[BUFFER_SIZE];
      struct sockaddr_in client_address;
      int socket_descriptor;
  } request_handler_t;
  ```

- Create a `handle_request` function (thread function):
  - Parse the request to determine type
  - Route to appropriate handler function
  - Free the request_handler_t structure before thread exits

- Modify `main()` function:
  - Initialize client list (call init_client_list)
  - Create listener thread
  - Wait for listener thread (or run indefinitely)
  - Add cleanup code for graceful shutdown

**Files to Modify**: `chat_server.c`

**Code Structure Hint**:
```c
void *listener_thread(void *arg) {
    int sd = *(int *)arg;
    while (1) {
        char request[BUFFER_SIZE];
        struct sockaddr_in client_address;
        int rc = udp_socket_read(sd, &client_address, request, BUFFER_SIZE);
        if (rc > 0) {
            request[rc] = '\0';
            // Spawn worker thread
            pthread_t worker_tid;
            request_handler_t *handler_data = malloc(sizeof(request_handler_t));
            // Fill handler_data...
            pthread_create(&worker_tid, NULL, handle_request, handler_data);
            pthread_detach(worker_tid); // Don't wait for it
        }
    }
}
```

**Dependencies**: Step 1.3

---

### Step 1.5: Implement Request Parsing

**Objective**: Parse incoming requests to extract command type and content.

**Tasks**:
- Create `parse_request(request, command_type, content)` function:
  - Search for `$` delimiter in request
  - Extract part before `$` as command_type
  - Extract part after `$` as content
  - Handle cases where `$` doesn't exist (invalid request)
  - Return 0 on success, -1 on parse error

- Create `route_request(request, client_address, socket_descriptor)` function:
  - Calls parse_request
  - Based on command_type, calls appropriate handler:
    - `conn$` → handle_conn
    - `say$` → handle_say
    - `sayto$` → handle_sayto
    - `disconn$` → handle_disconn
    - `mute$` → handle_mute
    - `unmute$` → handle_unmute
    - `rename$` → handle_rename
    - `kick$` → handle_kick
  - For unknown commands, send error response

**Files to Modify**: `chat_server.c`

**Dependencies**: Step 1.4

---

## Phase 2: Client Foundation

### Step 2.1: Implement Dynamic Port Allocation

**Objective**: Client should bind to an available port, not hardcoded 10000.

**Tasks**:
- Modify client to find an available port:
  - Start from a base port (e.g., 10000)
  - Try to bind to each port in sequence
  - If bind fails (port in use), try next port
  - Continue until successful bind
  - Or use port 0 to let OS assign available port

- Alternative simpler approach:
  - Use `udp_socket_open(0)` - OS assigns available port automatically
  - You may need to query the assigned port if needed

- Update `client_info` structure if needed

**Files to Modify**: `chat_client.c`, possibly `udp.h`

**Dependencies**: None (can be done independently)

**Code Hint**:
```c
// Simple approach - let OS assign port
int sd = udp_socket_open(0); // 0 means OS chooses port

// If you need the assigned port number:
struct sockaddr_in addr;
socklen_t len = sizeof(addr);
getsockname(sd, (struct sockaddr *)&addr, &len);
int assigned_port = ntohs(addr.sin_port);
```

---

### Step 2.2: Implement Client Name Tracking

**Objective**: Client should track its own name and connection status.

**Tasks**:
- Add to `client_info` structure:
  - `char client_name[MAX_NAME_LEN]`
  - `int is_connected` (boolean flag)
  - Initialize `is_connected = 0`

- Create function to set client name after successful connection

**Files to Modify**: `chat_client.c`

**Dependencies**: Step 2.1 (optional)

---

### Step 2.3: Implement Request Parsing on Client

**Objective**: Client should validate user input format before sending.

**Tasks**:
- In `writer_thread`, before sending request:
  - Check if input contains `$` delimiter
  - Validate command format
  - Optionally provide helpful error messages for invalid format
  - Still send to server (let server also validate), but client-side validation improves UX

- Create helper function `validate_request_format(request)`:
  - Returns 1 if valid format (contains `$`)
  - Returns 0 if invalid
  - Can print error message to user

**Files to Modify**: `chat_client.c`

**Dependencies**: None (can be done independently)

---

## Phase 3: Core Communication

### Step 3.1: Implement `conn$` Command (Server)

**Objective**: Allow clients to connect and register with the server.

**Tasks**:
- Create `handle_conn(request, client_address, socket_descriptor)` function:
  - Parse client name from request (after `conn$`)
  - Check if name already exists in list
  - If exists, send error: "Name already in use. Please choose another name."
  - If not exists, call `add_client(name, client_address, is_admin)`
    - Determine `is_admin` by checking if port == 6666
  - Send success response: "Hi [name], you have successfully connected to the chat"
  - Update client's last_active_time

**Files to Modify**: `chat_server.c`

**Dependencies**: Steps 1.3, 1.5

**Response Format**: "Hi Alice, you have successfully connected to the chat\n"

---

### Step 3.2: Implement `conn$` Command (Client)

**Objective**: Client sends connection request and handles response.

**Tasks**:
- In `writer_thread`, when user types `conn$ [name]`:
  - Send request to server (already implemented)
  - In `listener_thread`, when response received:
    - Check if response indicates success
    - If success, update `client_info.is_connected = 1`
    - Store client name in `client_info.client_name`
    - Display connection confirmation to user
    - If error, display error message

**Files to Modify**: `chat_client.c`

**Dependencies**: Step 3.1, Step 2.2

**Note**: Client should require connection before allowing other commands (optional validation).

---

### Step 3.3: Implement `say$` Command (Server) - Broadcast

**Objective**: Broadcast messages to all connected clients.

**Tasks**:
- Create `handle_say(request, client_address, socket_descriptor)` function:
  - Parse message content (after `say$`)
  - Find sender client in list by address
  - If client not found, send error: "You are not connected. Please connect first."
  - Get all clients from list (read lock)
  - Format message: "[sender_name]: [message]\n"
  - Iterate through all clients:
    - Skip sender (don't send message back to sender)
    - Skip if recipient has sender muted (see mute implementation later)
    - Send formatted message to each client using `udp_socket_write`
  - Update sender's last_active_time
  - Optionally send confirmation to sender

- Create helper function `broadcast_message(message, sender_address, exclude_muted)`:
  - Takes formatted message string
  - Broadcasts to all clients except sender
  - Handles mute logic

**Files to Modify**: `chat_server.c`

**Dependencies**: Steps 1.3, 1.5, 3.1

**Important Notes**:
- Use read lock when reading the list
- Handle errors gracefully (some sends may fail, continue to others)
- Consider message formatting consistency

---

### Step 3.4: Implement `say$` Command (Client)

**Objective**: Client sends broadcast request and displays received broadcasts.

**Tasks**:
- In `writer_thread`, user can type `say$ [message]`
- Request is sent to server (already implemented)
- In `listener_thread`, when broadcast message received:
  - Display message (format: "[sender_name]: [message]")
  - Handle display based on chosen UI approach (see Phase 5)

**Files to Modify**: `chat_client.c`

**Dependencies**: Step 3.3

**Note**: Client display logic depends on UI choice (Phase 5).

---

### Step 3.5: Implement `sayto$` Command (Server) - Private Message

**Objective**: Send private messages to specific clients.

**Tasks**:
- Create `handle_sayto(request, client_address, socket_descriptor)` function:
  - Parse request: `sayto$ recipient_name message`
    - Extract recipient_name (first word after `sayto$`)
    - Extract message (rest of content)
  - Find sender client by address
  - If sender not found, send error
  - Find recipient client by name (`find_client_by_name`)
  - If recipient not found, send error: "User [name] not found."
  - Format message: "[sender_name]: [message]\n"
  - Send formatted message ONLY to recipient (not broadcast)
  - Update sender's last_active_time
  - Optionally send confirmation to sender: "Message sent to [recipient_name]"

**Files to Modify**: `chat_server.c`

**Dependencies**: Steps 1.3, 1.5, 3.1

**Parsing Note**: You need to split `sayto$ recipient_name message` properly. The recipient name is the first token after `sayto$`, and the message is everything after that.

---

### Step 3.6: Implement `sayto$` Command (Client)

**Objective**: Client sends private message request.

**Tasks**:
- User types `sayto$ [recipient] [message]`
- Request sent to server
- Client may display confirmation when server responds (if server sends confirmation)

**Files to Modify**: `chat_client.c`

**Dependencies**: Step 3.5

---

### Step 3.7: Implement `disconn$` Command (Server)

**Objective**: Remove client from chat when they disconnect.

**Tasks**:
- Create `handle_disconn(request, client_address, socket_descriptor)` function:
  - Find sender client by address
  - If client not found, ignore (already disconnected)
  - Remove client from list (`remove_client_by_address`)
  - Send confirmation: "Disconnected. Bye!"
  - Note: Client may have already disconnected, so handle errors gracefully

**Files to Modify**: `chat_server.c`

**Dependencies**: Steps 1.3, 1.5, 3.1

---

### Step 3.8: Implement `disconn$` Command (Client)

**Objective**: Client handles disconnection gracefully.

**Tasks**:
- Already partially implemented (checks for "Disconnected. Bye!" message)
- Ensure client exits cleanly when disconnecting
- Close socket properly
- Terminate threads cleanly

**Files to Modify**: `chat_client.c`

**Dependencies**: Step 3.7

**Status**: Already implemented, but verify it works correctly.

---

## Phase 4: Advanced Features

### Step 4.1: Implement Mute Tracking Data Structure

**Objective**: Track which clients each client has muted.

**Tasks**:
- Add to `client_node` structure:
  - `char muted_clients[MAX_MUTED][MAX_NAME_LEN]` (array approach)
  - OR `struct muted_list *muted_head` (linked list approach - more flexible)
  
- Create muted list node structure (if using linked list):
  ```c
  typedef struct muted_node {
      char client_name[MAX_NAME_LEN];
      struct muted_node *next;
  } muted_node_t;
  ```

- Add helper functions:
  - `add_muted_client(client_node, muted_name)` - add to muted list
  - `remove_muted_client(client_node, muted_name)` - remove from muted list
  - `is_client_muted(client_node, muted_name)` - check if muted

**Files to Modify**: `chat_server.c`

**Dependencies**: Step 1.1

**Note**: Mute is per-client, meaning Client A can mute Client B, and Client B's messages won't be shown to Client A, but will still be shown to others.

---

### Step 4.2: Implement `mute$` Command (Server)

**Objective**: Record that a client wants to mute another client.

**Tasks**:
- Create `handle_mute(request, client_address, socket_descriptor)` function:
  - Parse muted client name (after `mute$`)
  - Find requester client by address
  - Find client to mute by name
  - If muted client not found, send error: "User [name] not found."
  - If requester tries to mute themselves, send error (optional validation)
  - Add muted client to requester's muted list
  - Update requester's last_active_time
  - Send no direct response (as per requirements)
  - Effect seen in future broadcasts (modify `handle_say` to check mute list)

**Files to Modify**: `chat_server.c`

**Dependencies**: Steps 1.5, 4.1, 3.1

**Important**: Modify `handle_say` (Step 3.3) to check mute list before sending.

---

### Step 4.3: Implement `mute$` Command (Client)

**Objective**: Client sends mute request.

**Tasks**:
- User types `mute$ [client_name]`
- Request sent to server
- No immediate feedback (server doesn't send response)
- User will notice effect when muted client's messages stop appearing

**Files to Modify**: `chat_client.c`

**Dependencies**: Step 4.2

---

### Step 4.4: Implement `unmute$` Command (Server)

**Objective**: Remove mute setting so messages are received again.

**Tasks**:
- Create `handle_unmute(request, client_address, socket_descriptor)` function:
  - Parse client name to unmute (after `unmute$`)
  - Find requester client by address
  - Remove client from requester's muted list
  - Update requester's last_active_time
  - Send no direct response
  - Effect seen in future broadcasts

**Files to Modify**: `chat_server.c`

**Dependencies**: Steps 1.5, 4.1, 3.1

---

### Step 4.5: Implement `unmute$` Command (Client)

**Objective**: Client sends unmute request.

**Tasks**:
- User types `unmute$ [client_name]`
- Request sent to server

**Files to Modify**: `chat_client.c`

**Dependencies**: Step 4.4

---

### Step 4.6: Update `handle_say` to Respect Mute Settings

**Objective**: Muted clients' messages are not sent to clients who muted them.

**Tasks**:
- Modify `handle_say` function:
  - When broadcasting to each recipient, check if sender is in recipient's muted list
  - If muted, skip sending message to that recipient
  - Otherwise, send message normally

**Files to Modify**: `chat_server.c`

**Dependencies**: Step 3.3, 4.1

**Code Hint**:
```c
// In handle_say, when iterating through recipients:
for each recipient_client in client_list {
    if (recipient_client != sender) {
        if (!is_client_muted(recipient_client, sender_name)) {
            udp_socket_write(sd, &recipient_client->client_address, message, strlen(message));
        }
    }
}
```

---

### Step 4.7: Implement `rename$` Command (Server)

**Objective**: Allow clients to change their chat name.

**Tasks**:
- Create `handle_rename(request, client_address, socket_descriptor)` function:
  - Parse new name (after `rename$`)
  - Find requester client by address
  - Check if new name already exists in list
  - If exists, send error: "Name already in use."
  - If not exists:
    - Update client's name in the list (write lock required)
    - Update last_active_time
    - Send confirmation: "You are now known as [new_name]\n"

**Files to Modify**: `chat_server.c`

**Dependencies**: Steps 1.3, 1.5, 3.1

**Note**: This requires modifying the client_node's name field. Ensure proper locking.

---

### Step 4.8: Implement `rename$` Command (Client)

**Objective**: Client sends rename request and updates local name.

**Tasks**:
- User types `rename$ [new_name]`
- Request sent to server
- In `listener_thread`, when confirmation received:
  - Update local `client_info.client_name`
  - Display confirmation to user

**Files to Modify**: `chat_client.c`

**Dependencies**: Step 4.7, Step 2.2

---

### Step 4.9: Implement `kick$` Command (Server)

**Objective**: Admin clients can forcibly remove other clients.

**Tasks**:
- Create `handle_kick(request, client_address, socket_descriptor)` function:
  - Parse client name to kick (after `kick$`)
  - Find requester client by address
  - Check if requester is admin (check `is_admin` flag or port == 6666)
  - If not admin, send error: "Only admin can kick users."
  - Find client to kick by name
  - If client not found, send error: "User [name] not found."
  - If admin tries to kick themselves, send error (optional)
  - Send removal message to kicked client: "You have been removed from the chat\n"
  - Remove kicked client from list
  - Broadcast removal message to all remaining clients: "[name] has been removed from the chat\n"
  - Update requester's last_active_time

**Files to Modify**: `chat_server.c`

**Dependencies**: Steps 1.1, 1.3, 1.5, 3.1

**Admin Detection**: Check `is_admin` flag set during `conn$`, or check if port == 6666 when request is received.

---

### Step 4.10: Implement `kick$` Command (Client)

**Objective**: Admin client can send kick request.

**Tasks**:
- User types `kick$ [client_name]`
- Request sent to server
- Client handles error messages if not admin

**Files to Modify**: `chat_client.c`

**Dependencies**: Step 4.9

**Note**: Admin client must bind to port 6666. Update client to accept port as command-line argument or environment variable.

---

## Phase 5: User Interface

### Step 5.1: Choose UI Approach

**Decision Point**: Choose one of two approaches:

**Option A: Single Terminal with Split Screen**
- Use ANSI escape codes or ncurses library
- Divide terminal into input area and chat display area
- Messages appear above while user types below
- More complex implementation

**Option B: Two Terminal / File-based (Simpler)**
- Input in one terminal
- Output written to `iChat.txt` file
- Second terminal runs `tail -f iChat.txt`
- Simpler to implement

**Recommendation**: Start with Option B for faster development, implement Option A later if time permits.

---

### Step 5.2A: Implement Two-Terminal UI (Option B)

**Objective**: Write incoming messages to file while reading input from terminal.

**Tasks**:
- Create file `iChat.txt` at client startup
- Open file in append mode
- In `listener_thread`:
  - When message received, write to file
  - Flush file buffer after each write (`fflush`)
  - Optionally also print to stdout for debugging

- In `writer_thread`:
  - Continue reading from stdin as normal
  - Print prompt or status to stdout (not file)

**Files to Modify**: `chat_client.c`

**Dependencies**: None (can be implemented at any time)

**Code Hint**:
```c
FILE *chat_file;

// In main:
chat_file = fopen("iChat.txt", "w"); // "w" truncates, "a" appends
if (!chat_file) {
    perror("Failed to open iChat.txt");
}

// In listener_thread:
fprintf(chat_file, "%s", server_response);
fflush(chat_file);
```

**User Instructions**: 
- Run client in one terminal
- In another terminal: `tail -f iChat.txt`

---

### Step 5.2B: Implement Single Terminal UI (Option A) - Advanced

**Objective**: Create split-screen interface in single terminal.

**Tasks**:
- Use ANSI escape codes for cursor positioning
- On startup, clear screen and set up layout
- Create display area (top 80% of terminal)
- Create input area (bottom 20% of terminal)
- In `listener_thread`:
  - When message received, move cursor to display area
  - Print message
  - Return cursor to input area
- In `writer_thread`:
  - Keep cursor in input area
  - Print prompt in input area

**Libraries to Consider**: ncurses (provides better terminal control)

**Files to Modify**: `chat_client.c`

**Dependencies**: None

**Note**: This is more complex and optional. Option B is sufficient for the assignment.

---

## Phase 6: Proposed Extensions

### Step 6.1: PE 1 - History at Connection

**Objective**: Send last 15 broadcast messages to new clients when they connect.

**Tasks**:
- Create circular buffer data structure:
  - Array of 15 message strings
  - Current index pointer
  - Count of messages stored
  - pthread mutex for synchronization

- Create functions:
  - `init_history_buffer()` - initialize buffer
  - `add_to_history(sender_name, message)` - add new broadcast message
    - Use circular buffer logic (wrap around when full)
  - `get_history()` - return array of last 15 messages
    - Return formatted string or array of strings

- Modify `handle_say`:
  - After broadcasting, add message to history buffer

- Modify `handle_conn`:
  - After successful connection, send history to new client
  - Format: "=== Chat History ===\n[message1]\n[message2]\n...\n==================\n"

**Files to Modify**: `chat_server.c`

**Dependencies**: Step 3.3 (handle_say must be implemented)

**Data Structure**:
```c
#define HISTORY_SIZE 15

typedef struct {
    char messages[HISTORY_SIZE][BUFFER_SIZE];
    int current_index;
    int count;
    pthread_mutex_t lock;
} history_buffer_t;
```

---

### Step 6.2: PE 2 - Remove Inactive Clients

**Objective**: Monitor client activity and remove inactive clients.

**Tasks**:
- Create min-heap data structure (or use simpler ordered list):
  - Store (last_active_time, client_node*) pairs
  - Ordered by timestamp (minimum at top)
  - Thread-safe with mutex

- Create monitoring thread:
  - Runs periodically (every 30 seconds or 1 minute)
  - Checks least recently active client
  - If inactivity > threshold (5 minutes), send `ping$` message
  - Wait for `ret-ping$` response with timeout
  - If no response, remove client and broadcast removal

- Modify client activity updates:
  - Every time client sends request, update last_active_time in heap

- Implement `handle_ping` and `handle_ret_ping`:
  - Client receives `ping$`, responds with `ret-ping$`
  - Server receives `ret-ping$`, updates client's active time

**Files to Modify**: `chat_server.c`, `chat_client.c`

**Dependencies**: Steps 1.3, 3.1

**Simplified Approach** (if min-heap is too complex):
- Use ordered linked list sorted by timestamp
- Monitor thread checks head of list
- More inefficient but simpler to implement

**Data Structure** (simplified):
```c
typedef struct activity_node {
    time_t last_active_time;
    client_node_t *client;
    struct activity_node *next; // sorted by timestamp
} activity_node_t;
```

---

## Testing Checklist

### Unit Testing (Test Individual Functions)
- [ ] Client list add/remove operations
- [ ] Client list search operations
- [ ] Request parsing
- [ ] Mute list operations

### Integration Testing (Test Full Workflows)
- [ ] Client connects with unique name
- [ ] Client connects with duplicate name (should fail)
- [ ] Multiple clients connect simultaneously
- [ ] Broadcast message reaches all clients
- [ ] Private message reaches only recipient
- [ ] Mute prevents messages from being received
- [ ] Unmute restores message reception
- [ ] Rename updates name for future messages
- [ ] Admin kick removes client and broadcasts removal
- [ ] Non-admin kick attempt fails
- [ ] Disconnect removes client from list
- [ ] Server handles concurrent requests

### Stress Testing
- [ ] 10+ clients connected simultaneously
- [ ] Rapid message sending
- [ ] Multiple clients connecting/disconnecting rapidly
- [ ] Long message handling

### Edge Cases
- [ ] Empty message
- [ ] Very long message (buffer overflow handling)
- [ ] Special characters in names/messages
- [ ] Client disconnects without sending disconn$
- [ ] Server handles client that already disconnected
- [ ] Invalid request formats

### PE Testing (if implemented)
- [ ] New client receives last 15 messages on connection
- [ ] History buffer wraps around correctly (16th message)
- [ ] Inactive client receives ping after 5 minutes
- [ ] Inactive client responds to ping (stays connected)
- [ ] Inactive client doesn't respond (gets removed)

---

## Implementation Order Summary

**Quick Start Path** (Minimum Viable Product):
1. Steps 1.1-1.5 (Server foundation)
2. Step 3.1-3.4 (conn$ and say$)
3. Step 3.7-3.8 (disconn$)
4. Step 5.2A (Simple UI)
5. Test basic chat functionality

**Full Feature Path**:
- Follow phases 1-5 sequentially
- Add PE features in Phase 6 if time permits

**Priority Order**:
1. **Critical**: Steps 1.1-1.5, 3.1-3.4, 3.7-3.8 (Basic chat)
2. **Important**: Steps 3.5-3.6 (Private messages), 4.1-4.6 (Mute), 4.7-4.8 (Rename)
3. **Advanced**: Step 4.9-4.10 (Kick), Step 5.2B (Advanced UI)
4. **Bonus**: Phase 6 (PE 1 and PE 2)

---

## Notes and Tips

1. **Memory Management**: Be careful with malloc/free. Every malloc should have a corresponding free.

2. **Thread Safety**: Always use appropriate locks. Read locks for reads, write locks for writes. Don't forget to unlock.

3. **Error Handling**: Check return values of all system calls and library functions. Handle errors gracefully.

4. **Message Formatting**: Be consistent with newlines (`\n`) and message formats across server responses.

5. **Testing**: Test incrementally. After each step, test that feature before moving to the next.

6. **Debugging**: Use `printf` for debugging. Consider adding debug flags to enable/disable verbose output.

7. **Port Conflicts**: If port is in use, the program will fail. Handle this gracefully or use dynamic port allocation.

8. **Name Collisions**: Server should prevent duplicate names. Client should handle "name already in use" error.

9. **Graceful Shutdown**: Consider implementing signal handlers (SIGINT) for clean shutdown.

10. **Code Organization**: Consider splitting server code into multiple files if it gets too long (e.g., `server_list.c`, `server_handlers.c`).

---

## Current Status Tracking

**Completed**:
- ✅ Client thread structure (sender/listener)
- ✅ Client basic socket setup
- ✅ Client disconn$ detection

**In Progress**:
- ⏳ Server foundation (Steps 1.1-1.5)

**Not Started**:
- ⬜ All command implementations
- ⬜ UI implementation
- ⬜ PE features

---

**Last Updated**: [Update this date as you progress]

**Next Steps**: [Write what you're working on next]

