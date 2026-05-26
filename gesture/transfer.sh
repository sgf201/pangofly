#!/bin/bash

# Simple file transfer script using netcat

HOST="192.168.58.5"
PORT="12345"

echo "Starting file transfer to $HOST:$PORT..."

# Create a tar archive of the files
tar cf - gesture_publisher_pod gesture_subscriber_pod ../build_k230/libpangofly.so | nc -q 1 $HOST $PORT

echo "Transfer complete!"
