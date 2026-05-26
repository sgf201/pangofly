# Makefile for Pangofly Gesture Application - K230 Linux platform

# Toolchain path
CROSS_COMPILE=/home/sgf/ws/.kendryte/k230_toolchains/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0/bin/riscv64-unknown-linux-gnu-
CC=$(CROSS_COMPILE)g++

# Include paths
CFLAGS=-std=c++14 -Wall -O2 \
       -I../ \
       -I../pangofly \
       -I/home/sgf/ws/k230_linux_pangofly/buildroot-overlay/package/k230_assistant/src \
       -I/home/sgf/ws/k230_pangofly/src/rtsmart/mpp/middleware/src/kdmedia/include \
       -I/home/sgf/ws/k230_pangofly/src/rtsmart/libs/kmodel/include \
       -I/home/sgf/ws/k230_pangofly/src/rtsmart/libs/3rd-party/nncase/include

# Link flags
LDFLAGS=-L../build_k230 \
        -L/home/sgf/ws/k230_pangofly/src/rtsmart/libs/kmodel/lib \
        -lpangofly \
        -lkdmedia \
        -lnncase_runtime \
        -lpthread \
        -lrt

all: gesture_publisher gesture_subscriber

gesture_publisher: gesture_publisher.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

gesture_subscriber: gesture_subscriber.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f gesture_publisher gesture_subscriber

.PHONY: all clean
