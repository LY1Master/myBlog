

CC ?= cc

TARGET := char_echo_test

CFLAGS ?= -Wall -Wextra -Werror -O1

.PHONY: all clean

all: $(TARGET)

$(TARGET):char_echo_test.c 
	$(CC) $(CFLAGS) -o  $@ $<

clean:
	$(RM) $(TARGET)

