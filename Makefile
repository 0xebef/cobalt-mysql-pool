CC := gcc
RM := rm -f

MYSQLCONFIG := mysql_config
#MYSQLCONFIG := mariadb_config

CFLAGS ?= -O2
override CFLAGS += -D_FORTIFY_SOURCE=2 -D_REENTRANT -D_THREAD_SAFE -D_MY_PTHREAD_FASTMUTEX=1 `$(MYSQLCONFIG) --include` -std=gnu11 -pipe -pie -fpie -fno-plt -fexceptions -fasynchronous-unwind-tables -fno-strict-aliasing -fno-strict-overflow -fwrapv -static-libgcc -fvisibility=hidden -fstack-clash-protection -fstack-protector-strong -fcf-protection=full --param ssp-buffer-size=4 -Wall -Wextra -Werror -Wshadow -Wformat -Wformat-security

LDFLAGS ?= -Wl,-pie -Wl,-z,relro -Wl,-z,now -Wl,-z,defs -Wl,-s -Wl,-lrt
override LDFLAGS += -Wl,-lpthread `$(MYSQLCONFIG) --libs`

all: example

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

cobalt-mysql-pool.o:

example.o:

example: cobalt-mysql-pool.o example.o
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	$(RM) example *.o
