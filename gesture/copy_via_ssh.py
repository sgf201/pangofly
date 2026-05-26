import subprocess
import base64

# Read the file content
with open('gesture_publisher_pod', 'rb') as f:
    publisher_data = base64.b64encode(f.read()).decode()

with open('gesture_subscriber_pod', 'rb') as f:
    subscriber_data = base64.b64encode(f.read()).decode()

with open('../build_k230/libpangofly.so', 'rb') as f:
    lib_data = base64.b64encode(f.read()).decode()

# Create bash script to transfer files via SSH
script = f'''
ssh -o StrictHostKeyChecking=no root@192.168.58.5 << 'EOF'
echo "{publisher_data}" | base64 -d > /root/gesture_publisher_pod
echo "{subscriber_data}" | base64 -d > /root/gesture_subscriber_pod
echo "{lib_data}" | base64 -d > /usr/lib/libpangofly.so
chmod +x /root/gesture_publisher_pod /root/gesture_subscriber_pod
EOF
'''

# Run the script
result = subprocess.run(['bash', '-c', script], capture_output=True, text=True)
print("STDOUT:", result.stdout)
print("STDERR:", result.stderr)
print("Return code:", result.returncode)
