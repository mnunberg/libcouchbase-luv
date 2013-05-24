LCB_HEADERS=/sources/libcouchbase/include
UV_ROOT=/sources/libuv
CPPFLAGS=-Wall -Wextra -Werror -O0 -ggdb3

all: plugin-libuv.so
SRC=src/plugin.c \
	src/util.c \
	src/socket.c \
	src/iops.c \
	src/timer.c

plugin-libuv.so: $(SRC)
	$(CC) $(CPPFLAGS) \
		-shared -fPIC \
		-o $@ \
		-I$(UV_ROOT)/include \
		-I$(LCB_HEADERS) \
		$^ \
		-L$(UV_ROOT) -Wl,-rpath=$(UV_ROOT) -luv
