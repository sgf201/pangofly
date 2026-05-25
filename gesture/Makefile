# Makefile for Pangofly Gesture Application
CC = g++
CFLAGS = -std=c++14 -Wall -O2 -I../ -I../idl -I../pangofly
LDFLAGS = -L../build_linux -lpangofly

all: gesture_publisher gesture_subscriber

gesture_publisher: gesture_publisher.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

gesture_subscriber: gesture_subscriber.cc
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f gesture_publisher gesture_subscriber

.PHONY: all clean
