import subprocess
import sys

def copy_file(source, destination, host, password):
    # Create a script that handles the password
    script = f'''
    import subprocess
    import pty
    import os
    
    fd, name = pty.openpty()
    p = subprocess.Popen(
        ['ssh', '-o', 'StrictHostKeyChecking=no', '{host}', 'cat > {destination}'],
        stdin=fd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    os.close(fd)
    
    # Send password
    with open(name, 'w') as f:
        f.write('{password}\\n')
    
    # Send file content
    with open(source, 'rb') as f:
        content = f.read()
    
    # Use another approach with expect-like behavior
    '''
    
    # Use a simpler approach - write file to temp and use bash
    cmd = f'''
    sshpass -p "{password}" scp "{source}" "{host}:{destination}"
    '''
    
    # Try using expect if available
    try:
        import pexpect
        child = pexpect.spawn(f'scp {source} {host}:{destination}')
        child.expect('password:')
        child.sendline(password)
        child.expect(pexpect.EOF)
        print(f'Copied {source} successfully')
        return True
    except ImportError:
        pass
    
    # Try paramiko
    try:
        import paramiko
        ssh = paramiko.SSHClient()
        ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        ssh.connect(host.split('@')[1], username=host.split('@')[0], password=password)
        sftp = ssh.open_sftp()
        sftp.put(source, destination)
        sftp.close()
        ssh.close()
        print(f'Copied {source} successfully')
        return True
    except ImportError:
        pass
    except Exception as e:
        print(f'Paramiko error: {e}')
    
    print('No method available to copy files')
    return False

if __name__ == '__main__':
    host = 'root@192.168.58.5'
    password = 'sss'
    
    copy_file('gesture_publisher_pod', '/root/gesture_publisher_pod', host, password)
    copy_file('gesture_subscriber_pod', '/root/gesture_subscriber_pod', host, password)
    copy_file('../build_k230/libpangofly.so', '/usr/lib/libpangofly.so', host, password)
