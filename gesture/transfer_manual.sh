#!/bin/bash

# Simple file transfer using base64 encoding

# Create base64 encoded versions of the files
cd /home/sgf/ws/pangofly/gesture

echo "Creating base64 encoded files..."
base64 gesture_publisher_pod > gesture_publisher_pod.b64
base64 gesture_subscriber_pod > gesture_subscriber_pod.b64
base64 ../build_k230/libpangofly.so > libpangofly.so.b64

echo "Files encoded. Now transfer them to K230:"
echo ""
echo "1. On K230, run:"
echo "   cd /root"
echo ""
echo "2. Download the encoded files:"
echo "   wget http://192.168.98.32:8000/gesture_publisher_pod.b64"
echo "   wget http://192.168.98.32:8000/gesture_subscriber_pod.b64"
echo "   wget http://192.168.98.32:8000/libpangofly.so.b64"
echo ""
echo "3. Decode them:"
echo "   base64 -d gesture_publisher_pod.b64 > gesture_publisher_pod"
echo "   base64 -d gesture_subscriber_pod.b64 > gesture_subscriber_pod"
echo "   base64 -d libpangofly.so.b64 > /usr/lib/libpangofly.so"
echo ""
echo "4. Make executable:"
echo "   chmod +x gesture_publisher_pod gesture_subscriber_pod"
echo ""
echo "5. Run the test:"
echo "   LD_LIBRARY_PATH=/usr/lib ./gesture_publisher_pod &"
echo "   ./gesture_subscriber_pod"

echo ""
echo "HTTP server is running at http://192.168.98.32:8000"
