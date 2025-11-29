# Run 'chmod +x run_chat.sh' initially for setup
# After that, type './run_chat.sh' for compile + run
# Note that server must be killed when done
gcc chat_client.c -o chat_client

gcc chat_server.c -o chat_server

./chat_server &
SERVER_PID=$!
echo "server running with pid $SERVER_PID"

./chat_client