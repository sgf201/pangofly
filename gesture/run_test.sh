#!/bin/sh
echo "=== Pangofly Gesture Image Transfer Test ==="
echo ""
echo "Starting publisher in background..."
/lib/ld-linux-riscv64-lp64d.so.1 --library-path /usr/lib /root/gesture_publisher_simple &
PUBLISHER_PID=$!
echo "Publisher PID: $PUBLISHER_PID"

echo ""
echo "Waiting 2 seconds for publisher..."
sleep 2

echo ""
echo "Starting subscriber..."
/lib/ld-linux-riscv64-lp64d.so.1 --library-path /usr/lib /root/gesture_subscriber_simple

echo ""
echo "Waiting for publisher..."
wait $PUBLISHER_PID

echo ""
echo "=== Test Complete ==="
