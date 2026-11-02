obj-m += char_echo.o

KDIR ?= /lib/modules/$(shell uname -r)/build
CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -O2

.PHONY: all modules test clean

all: modules test

modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

test: char_echo_test

char_echo_test: char_echo_test.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	$(RM) char_echo_test
