import pty
import os
import subprocess
import select

# Create a PTY and spawn ssh
master, slave = pty.openpty()

# Start ssh to K230
ssh = subprocess.Popen(
    ['ssh', '-o', 'StrictHostKeyChecking=no', 'root@192.168.58.5'],
    stdin=slave,
    stdout=slave,
    stderr=slave,
    close_fds=True
)

# Close slave in parent
os.close(slave)

# Set master to non-blocking
import fcntl
flags = fcntl.fcntl(master, fcntl.F_GETFL)
fcntl.fcntl(master, fcntl.F_SETFL, flags | os.O_NONBLOCK)

# Wait for password prompt and send password
import time
time.sleep(1)

# Read output and send password
try:
    while True:
        r, w, e = select.select([master], [], [], 0.5)
        if master in r:
            output = os.read(master, 1024).decode()
            print("Received:", output)
            if "password:" in output:
                print("Sending password...")
                os.write(master, b'sss\n')
            elif "#" in output:
                print("Connected!")
                # Send commands to download files
                commands = [
                    'cd /root && wget http://192.168.98.32:8000/gesture_publisher_pod -O gesture_publisher_pod',
                    'wget http://192.168.98.32:8000/gesture_subscriber_pod -O gesture_subscriber_pod',
                    'wget http://192.168.98.32:8000/../build_k230/libpangofly.so -O /usr/lib/libpangofly.so',
                    'chmod +x gesture_publisher_pod gesture_subscriber_pod',
                    'exit'
                ]
                for cmd in commands:
                    os.write(master, (cmd + '\n').encode())
                    time.sleep(1)
                    r, w, e = select.select([master], [], [], 1)
                    if master in r:
                        print(os.read(master, 4096).decode())
                break
        if ssh.poll() is not None:
            break
finally:
    os.close(master)
    ssh.wait()

print("Transfer complete!")
