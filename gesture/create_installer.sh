#!/bin/bash

# Self-extracting script to transfer files to K230

echo "Creating self-extracting script..."

# Create the self-extracting script
cat > /tmp/install_gesture.sh << 'SELF_EXTRACT_EOF'
#!/bin/bash
echo "Extracting gesture application files..."

# Extract files from here
sed '1,/^__ARCHIVE_BELOW__$/d' "$0" | base64 -d | tar xz

echo "Files extracted!"
echo "Installing libpangofly.so..."
cp libpangofly.so /usr/lib/
echo "Making executables..."
chmod +x gesture_publisher_pod gesture_subscriber_pod
echo "Done! Files are in /root/"
exit 0

__ARCHIVE_BELOW__
SELF_EXTRACT_EOF

# Add the compressed archive to the script
cd /home/sgf/ws/pangofly/gesture
tar cf - gesture_publisher_pod gesture_subscriber_pod ../build_k230/libpangofly.so | xz | base64 >> /tmp/install_gesture.sh

chmod +x /tmp/install_gesture.sh

echo "Self-extracting script created!"
echo "Size: $(wc -c /tmp/install_gesture.sh | awk '{print $1}') bytes"

# Now we need to transfer this script to K230
echo "Transferring script to K230..."

# Use Python to transfer via HTTP
python3 -c "
import http.server
import socketserver
import threading
import time
import subprocess

# Start HTTP server in background
class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory='/tmp', **kwargs)

server = socketserver.TCPServer(('0.0.0.0', 8080), Handler)
thread = threading.Thread(target=server.serve_forever)
thread.daemon = True
thread.start()

time.sleep(1)
print('HTTP server running on port 8080')

# Now try to download and execute on K230
# This requires SSH access with password
print('Attempting to download script on K230...')

# We'll use a simple approach - just show the command to run
print()
print('Please run this command on K230:')
print('wget http://192.168.98.32:8080/install_gesture.sh -O /tmp/install_gesture.sh && chmod +x /tmp/install_gesture.sh && /tmp/install_gesture.sh')
print()

# Wait for user to complete
input('Press Enter when done...')

server.shutdown()
print('Done!')
"
