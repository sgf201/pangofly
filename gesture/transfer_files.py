import subprocess
import time

# First, try to start a receiver on K230 using ssh with password
# We'll use a simple approach - write the password to a temp file and use it

# Create a script that will be executed on K230 to receive files
receiver_script = '''
#!/bin/bash
echo "Starting file receiver..."
nc -l -p 12345 > /root/gesture_files.tar 2>/dev/null || python3 -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind(('', 12345))
s.listen(1)
conn, addr = s.accept()
with open('/root/gesture_files.tar', 'wb') as f:
    while True:
        data = conn.recv(4096)
        if not data:
            break
        f.write(data)
conn.close()
s.close()
"
'''

# Write to a temp file
with open('/tmp/receiver.sh', 'w') as f:
    f.write(receiver_script)

# Try to execute on K230
print("Attempting to start receiver on K230...")
result = subprocess.run(
    ['bash', '-c', f'cat /tmp/receiver.sh | ssh -o StrictHostKeyChecking=no root@192.168.58.5'],
    capture_output=True,
    text=True
)
print("SSH result:", result.returncode)
print("STDOUT:", result.stdout)
print("STDERR:", result.stderr)
