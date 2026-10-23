# Host tests for the protocol logic: make -C tests
CC ?= cc
CFLAGS ?= -O1 -g -Wall -Wextra -Werror -DCBC=0 -DCTR=0

SRCS = test_pgp.c ../pgp_cert.c ../pgp_handshake.c ../pgp_led.c ../aes.c

test: test_pgp
	./test_pgp

test_pgp: $(SRCS) ../*.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f test_pgp

.PHONY: test clean
