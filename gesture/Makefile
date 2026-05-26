# Makefile for Pangofly Gesture Application - K230 Linux platform
# Using Kendryte toolchain

# Toolchain path
CROSS_COMPILE=/home/sgf/ws/.kendryte/k230_toolchains/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0/bin/riscv64-unknown-linux-gnu-
CC=$(CROSS_COMPILE)g++

# Include paths
CFLAGS=-std=c++14 -Wall -O2 \
       -I../ \
       -I../pangofly \
       -I/home/sgf/ws/.kendryte/k230_toolchains/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0/riscv64-unknown-linux-gnu/sysroot/usr/include

# Link flags
LDFLAGS=-L../build_k230 \
        -lpangofly \
        -lpthread \
        -lrt

all: gesture_publisher_pod gesture_subscriber_pod

gesture_publisher_pod: gesture_publisher_pod.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

gesture_subscriber_pod: gesture_subscriber_pod.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f gesture_publisher_pod gesture_subscriber_pod

.PHONY: all clean
