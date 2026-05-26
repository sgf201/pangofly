import socket

# Simple file transfer using TCP socket

def send_file(filename, host, port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((host, port))
        # Send filename length and filename
        fname = filename.encode()
        sock.sendall(len(fname).to_bytes(4, 'big'))
        sock.sendall(fname)
        
        # Send file size and content
        with open(filename, 'rb') as f:
            content = f.read()
        sock.sendall(len(content).to_bytes(8, 'big'))
        sock.sendall(content)
        print(f'Sent {filename}')
    finally:
        sock.close()

if __name__ == '__main__':
    import sys
    if len(sys.argv) != 4:
        print(f'Usage: {sys.argv[0]} <filename> <host> <port>')
        sys.exit(1)
    
    send_file(sys.argv[1], sys.argv[2], int(sys.argv[3]))
