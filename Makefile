CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -g

SRCS = main.c memory.c
HDRS = memory.h
OBJS = $(SRCS:.c=.o)
TARGET = pm_manager

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
