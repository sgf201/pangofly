import socket

def send_file_over_tcp(filename, host, port):
    """Send a file over TCP to the specified host and port"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        print(f"Connecting to {host}:{port}...")
        sock.connect((host, port))
        
        # Send filename length and filename
        fname = filename.encode()
        sock.sendall(len(fname).to_bytes(4, 'big'))
        sock.sendall(fname)
        
        # Send file size
        with open(filename, 'rb') as f:
            content = f.read()
        
        print(f"Sending {filename} ({len(content)} bytes)...")
        sock.sendall(len(content).to_bytes(8, 'big'))
        
        # Send file content in chunks
        chunk_size = 4096
        for i in range(0, len(content), chunk_size):
            sock.sendall(content[i:i+chunk_size])
        
        print(f"Successfully sent {filename}")
        return True
    except Exception as e:
        print(f"Error sending {filename}: {e}")
        return False
    finally:
        sock.close()

if __name__ == '__main__':
    host = '192.168.58.5'
    port = 12345
    
    # Send the files
    send_file_over_tcp('gesture_publisher_pod', host, port)
    send_file_over_tcp('gesture_subscriber_pod', host, port)
    send_file_over_tcp('../build_k230/libpangofly.so', host, port)
