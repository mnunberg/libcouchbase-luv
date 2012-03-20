all: libcouchbase_libuv.so

check: test-main
	./test-main

LIBCOUCHBASE_DIR=/sources/libcouchbase/.libs
LIBCOUCHBASE_INCLUDE=/sources/libcouchbase/include
LIBUV_INCLUDE=/sources/libuv/include
LIBUV_A=/sources/libuv/uv.a

CFLAGS=-O0 -ggdb3 -Wall -I$(LIBUV_INCLUDE) -I$(LIBCOUCHBASE_INCLUDE) -I$(shell pwd)

LDFLAGS=-L$(LIBCOUCHBASE_DIR) \
		-Wl,-rpath=$(LIBCOUCHBASE_DIR) \
		-Wl,-rpath=$(shell pwd) \
		-lcouchbase -lcurses

OBJECTS=read.o write.o socket.o common.o plugin-libuv.o timer.o yolog.o

%.o: %.c
	$(CC) -c $(CFLAGS) -fPIC -fpic -o $@ $^


libcouchbase_libuv.so: $(OBJECTS)
	$(CC) -shared -o $@ $(OBJECTS) $(LDFLAGS) $(LIBUV_A)

test-main: test/test.c test/simple_1.c libcouchbase_libuv.so
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $^

clean:
	rm -f $(OBJECTS) *.o *.so test-main
