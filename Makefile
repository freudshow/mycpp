# Makefile for C TimeWheel

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -pthread
LDFLAGS = -pthread -lrt

TARGET = timewheel_test
SRCS = main.c timewheel.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c timewheel.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)
