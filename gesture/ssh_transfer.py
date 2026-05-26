import pty
import os
import subprocess
import select
import time

def ssh_with_password(host, username, password, command):
    """Execute command over SSH with password authentication"""
    master, slave = pty.openpty()
    
    ssh = subprocess.Popen(
        ['ssh', '-o', 'StrictHostKeyChecking=no', f'{username}@{host}', command],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True
    )
    
    os.close(slave)
    
    # Make master non-blocking
    import fcntl
    flags = fcntl.fcntl(master, fcntl.F_GETFL)
    fcntl.fcntl(master, fcntl.F_SETFL, flags | os.O_NONBLOCK)
    
    output = b""
    start_time = time.time()
    
    try:
        while time.time() - start_time < 30:  # 30 second timeout
            r, w, e = select.select([master], [], [], 1)
            
            if master in r:
                try:
                    data = os.read(master, 4096)
                    if not data:
                        break
                    output += data
                    
                    if b"password:" in output.lower():
                        os.write(master, (password + "\n").encode())
                        output = b""  # Reset output buffer
                        
                    if ssh.poll() is not None:
                        break
                except OSError:
                    break
            
            if ssh.poll() is not None:
                break
            
            time.sleep(0.1)
    finally:
        os.close(master)
        ssh.wait()
    
    return output.decode('utf-8', errors='ignore')

# Main script
if __name__ == "__main__":
    host = "192.168.58.5"
    username = "root"
    password = "sss"
    
    print("Transferring files to K230...")
    
    # Download files from HTTP server on K230
    commands = [
        f"cd /root && wget -q http://192.168.98.32:8000/gesture_publisher_pod",
        f"wget -q http://192.168.98.32:8000/gesture_subscriber_pod",
        f"wget -q http://192.168.98.32:8000/../build_k230/libpangofly.so -O /usr/lib/libpangofly.so",
        "chmod +x gesture_publisher_pod gesture_subscriber_pod",
        "ls -la /root/gesture_* /usr/lib/libpangofly.so"
    ]
    
    for cmd in commands:
        print(f"Executing: {cmd}")
        result = ssh_with_password(host, username, password, cmd)
        if result:
            print(f"Output: {result.strip()}")
        print()
    
    print("File transfer complete!")
