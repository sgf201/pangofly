#!/bin/bash

# File transfer script with password handling

HOST="192.168.58.5"
PASSWORD="sss"

# Function to send command via SSH with password
ssh_with_pass() {
    local cmd="$1"
    exec 3<>/dev/tcp/$HOST/22
    echo -e "ssh-connection" >&3
    
    # Wait for banner
    read -t 5 <&3
    
    # Send SSH handshake (simplified)
    # This is complex, let's use a different approach
    
    # Use bash coprocess
    coproc ssh { ssh -o StrictHostKeyChecking=no root@$HOST /bin/bash; }
    echo "$PASSWORD" >&${ssh[1]}
    echo "$cmd" >&${ssh[1]}
    echo "exit" >&${ssh[1]}
    cat <&${ssh[0]}
}

# Create tar archive and send
echo "Creating archive..."
tar cf - gesture_publisher_pod gesture_subscriber_pod ../build_k230/libpangofly.so > /tmp/files.tar

echo "Transferring files..."
ssh_with_pass "cat > /tmp/files.tar" < /tmp/files.tar

echo "Extracting on K230..."
ssh_with_pass "cd /root && tar xf /tmp/files.tar && mv libpangofly.so /usr/lib/ && chmod +x gesture_publisher_pod gesture_subscriber_pod"

echo "Done!"
