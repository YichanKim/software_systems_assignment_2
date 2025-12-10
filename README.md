# Software Systems Assignment 2: Multithreaded Chat Application

## Student Information

**Student 1:**
- CID: 02327531
- Name: Yichan Kim

**Student 2:**
- CID: [fill in CID]
- Name: [fill in Name]
---

## Overview

This project implements a multithreaded UDP-based chat application as specified in Assignment 2. The server uses a listener thread to receive incoming requests and spawns worker threads to handle each client request. Each client has a sender thread for reading user input and a listener thread for receiving server responses.

---

## Implemented Features

#### 1. **Connection Management**
- **`conn$` Command**: Connection to server by client
  - Server validates name (uniqueness, length)
  - Admin detection based on port number
  - Sends connection confirmation with command prefix: `conn$ Hi [NAME], you have successfully connected to the chat`
  - Sends chat history on client connection

#### 2. **Message Broadcasting**
- **`say$` Command**: Broadcast messages to all clients
  - Messages formatted as: `say$ [sender_name]: [message]`
  - Respects mute settings
  - Updates sender's activity time and adds messages to chat history

#### 3. **Private Messaging**
- **`sayto$` Command**: Send private messages to a client
  - Format: `sayto$ [recipient_name] [message]`
  - Validates recipient existence
  - Sends message to both recipient and sender

#### 4. **Disconnection**
- **`disconn$` Command**: Disconnect from server
  - Removes client from server's client list
  - Sends confirmation: `disconn$ Disconnected. Bye!`
  - Client terminates cleanly upon receiving confirmation

#### 5. **Mute/Unmute Functionality**
- **`mute$` Command**: Mute a specific client
  - Per-client mute lists (linked list implementation)
  - Effect seen in future broadcasts
  
- **`unmute$` Command**: Unmute a previously muted client
  - Removes client from mute list
  - Effect seen in future broadcasts

#### 6. **Name Management**
- **`rename$` Command**: Change client's chat name
  - Validates new name uniqueness
  - Updates name in client list
  - Client updates local name upon server confirmation

#### 7. **Admin Functionality**
- **`kick$` Command**: Admin-only command to forcibly remove clients
  - Admin detection: clients connecting from port 6666
  - Validates admin status before allowing kick
  - Sends removal message to kicked client: `kick$ You have been removed from the chat`
  - Broadcasts removal notification to all remaining clients
  - Prevents self-kick

### User Interface (Fully Implemented)

- **Two-Terminal Approach**: We implemented the alternative file-based UI option from the assignment
  - Client writes incoming messages to `iChat_<PID>.txt` file (using process ID to make filenames unique). Users can view messages in real-time using `tail -f iChat_<PID>.txt` in a separate terminal
  - Input handled in main terminal, output written to file
  - File automatically flushed after each write for real-time updates

### Proposed Extensions (Fully Implemented)

#### PE1: Chat History at Connection
- **Implementation Details**:
  - Circular buffer data structure storing last 15 broadcast messages
  - Thread-safe with mutex protection
  - Automatically sent to newly connected clients after connection confirmation

#### PE2: Remove Inactive Clients
- **Implementation Details**:
  - Monitoring thread runs every 30 seconds
  - Maintains `last_active_time` for each client (updated on every request)
  - Inactivity threshold: 5 minutes (300 seconds) as suggested in the assignment
  - Ping mechanism: Server sends `ping$` message to inactive clients
  - Clients respond with `ret-ping$` to indicate they're alive
  - Ping timeout: 10 seconds
  - Clients that don't respond are automatically removed from the chat
  - Removal broadcast: `say$ System: [name] has been removed due to inactivity`
  - Ping tracking list prevents duplicate pings to the same client
  - Note: We used a linked list for tracking pinged clients rather than a min-heap, as it was simpler to implement while still meeting the requirements

### Extra Features

1. **Error Handling with Command Prefixes**: Server sends error messages with `Error$` prefix for consistent parsing
2. **Request Validation**: Client-side validation of request format before sending to server
3. **Thread-Safe Operations**: All shared data structures protected with appropriate locks (read-write locks for client list, mutexes for history and ping tracking)
4. **Graceful Cleanup**: Proper cleanup of muted lists, ping trackers, and client nodes on disconnect
5. **Dynamic Port Allocation**: Client uses OS-assigned port (port 0) instead of hardcoded port
6. **Input Sanitisation**: Trimming of whitespace from user input and server responses

---
## System Design Choices

### Command Prefix System for Server Responses

One design choice we made was to use command prefixes in server responses, paired with the message content. We did this for a few reasons:

**Format**: `[command]$ [content]`

**Examples**:
- Success: `conn$ Hi Alice, you have successfully connected to the chat`
- Error: `Error$ Name already taken. Please choose another name`
- Message: `say$ Bob: Hello everyone!`
- History: `history$ Alice: Previous message`

**Why we did this**:

1. **Easier Parsing**: The client can quickly figure out what type of message it is by looking for the first `$` delimiter. The part before `$` tells us how to handle the message, and everything after `$` is what is actually displayed.

2. **Prevents Command Injection**: By only printing the content after the first `$`, we avoid command injection attacks. Even if someone sends malicious content, it won't be executed because:
   - We only extract and display the content part (after `$`)
   - The command prefix is just used for routing/parsing
   - We never directly execute user-provided strings

3. **Type Safety**: The command prefix tells us what type of message it is, so we can route it correctly (like `conn$` for connection confirmations, `say$` for broadcasts, `Error$` for errors).

**How it works**:
- The client uses `parse_acknowledge()` to split the command and content
- `route_acknowledge()` routes based on the command type
- Only the content after `$` gets displayed/written to file, never executed

We think this makes the code more secure and easier to maintain.

### Synchronisation
- **Client List**: Uses `pthread_rwlock_t` (reader-writer lock) as required by the assignment
  - Read locks (`pthread_rwlock_rdlock`) for reader threads (broadcasting, looking up clients, reading muted lists)
  - Write locks (`pthread_rwlock_wrlock`) for writer threads (adding/removing clients, renaming, kicking, updating mute lists)
- **Chat History**: Uses `pthread_mutex_t` for mutual exclusion
- **Ping Tracking List**: Uses `pthread_mutex_t` for mutual exclusion
- **Client State**: Uses `pthread_mutex_t` in the client for thread-safe access to shared state

### Data Structures
- **Client List**: Linked list of `client_node_t` structures
- **Mute Lists**: Linked list of `muted_node_t` structures (each client has their own)
- **Chat History**: Circular buffer array (holds 15 messages)
- **Ping Tracking**: Linked list of `ping_tracker_t` structures

---

## Compilation and Execution

### Compilation
```bash
gcc chat_server.c -o chat_server
gcc chat_client.c -o chat_client
```

### Execution
1. Start the server and first client:
   ```bash
   ./init_chat.sh
   ```
   Note: The client will display a message like `[DEBUG] tail -f iChat_<PID>.txt`

2. Start a client and connect to server (in a separate terminal):
   ```bash
   ./run_chat.sh
   ```
   Note: The client will display a message like `[DEBUG] tail -f iChat_<PID>.txt`

3. View messages (in a split terminal). Copy the DEBUG message that appears when starting client:
   ```bash
   tail -f iChat_<PID>.txt
   ```

### Admin Client
To run a client with admin privileges (so you can use `kick$`), you need to modify the client to bind to port 6666.

---
## Notes

- The server runs on port 12000 (defined in `udp.h` as `SERVER_PORT`)
- Clients use OS-assigned ports (port 0)
- Admin clients must bind to port 6666
- Buffer size: 1024 bytes (`BUFFER_SIZE`)
- Chat history stores last 15 broadcast messages only
- Inactivity timeout: 5 minutes (300 seconds)
- Ping timeout: 10 seconds
- Monitor thread checks every 30 seconds

---

## Limitations and Future Improvements

1. **Admin Port**: Right now admin functionality needs you to manually bind to port 6666. It would be better to add command-line arguments for this.

2. **File Cleanup**: The chat files (`iChat_<PID>.txt`) don't get cleaned up automatically when the client exits.