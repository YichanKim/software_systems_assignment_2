# Software Systems Assignment 2: Multithreaded Chat Application

## Student Information

**Student 1:**
- CID: 02327531
- Name: Yichan Kim

**Student 2:**
- CID: Sumukh Adiraju
- Name: 02563601
---

## Project Summary

For this assignment we built a UDP-based chat system using multi-threading on both the client and server. The server manages all connected users, and the client runs separate threads for sending and receiving. The system supports:

- Broadcast messages
- Private messages
- Rename and mute commands
- Admin kicking
- Chat history on join
- Inactivity timeout

---

##Implemenntation

### Connecting

- Clients connect using `conn$ <name>`.
- The server checks for:
  - Duplicate names
  - Overly long names
  - Whether the client is an admin (port 6666)
- On successful connection, the server:
  - Sends a confirmation message
  - Sends chat history

### Broadcast Messages (`say$`)

- Broadcasts (sends) message to all users.
- Messages are saved in a chat history buffer.
- Last-active timestamp is updated.
- Muted users do not receive broadcasts from their muted list.

### Private Messages (`sayto$`)

- Syntax: `sayto$ <name> <msg>`
- The server checks the recipient exists.
- The message is sent only to the sender and receiver.

### Disconnect (`disconn$`)

- Removes the user from the server list.
- Client prints a goodbye message and exits.

### Mute / Unmute

- Each client keeps a mute list (linked list).
- No server acknowledgement message.
- The effect is apparent only in later broadcasts.

### Rename (`rename$ <newname>`)

- Updates the client’s name on the server.
- Checks:
  - Length
  - Duplicates
- Server replies with a confirmation message.

### Admin Kick

- Admin clients connect from port 6666.
- They can issue the following command: `kick$ <name>`.
- Server:
  - Notifies the kicked user
  - Broadcasts the removal

### Client UI / Threading

- The client uses two threads:
  - One reads stdin
  - One listens for incoming server messages
- Messages are written to `iChat_<PID>.txt`.
- A second terminal runs `tail -f` on this file.
- Output is manually flushed to avoid delays.

---

## Proposed Extensions Implemented

### 1. Chat History on Connect

- Implemented a circular buffer storing the last 15 broadcast messages.
- New clients receive these immediately after connecting.
- A mutex protects the history from concurrent writes.

### 2. Inactive User Removal

- A server monitor thread runs every 30 seconds.
- Users inactive for more than 5 minutes receive a `ping$`.
- If they don’t respond with `ret-ping$` within 10 seconds, they are removed.
- Required careful lock ordering to prevent race conditions.

---

## Further Extensions we are proud of

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
To run a client with admin privileges (so you can use `kick$`), modify the client to bind to port 6666.

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