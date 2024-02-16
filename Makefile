CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lcurl -lm

SRCS = main.c cJSON.c
OBJS = $(SRCS:.c=.o)
EXEC = weather_station

.PHONY: all clean

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(EXEC)