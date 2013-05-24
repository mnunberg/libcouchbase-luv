LCB_HEADERS=/sources/libcouchbase/include
UV_ROOT=/sources/libuv
CPPFLAGS=-Wall -Wextra -Werror -O0 -ggdb3

all: plugin-libuv.so

plugin-libuv.so: src/plugin.c src/util.c
	$(CC) $(CPPFLAGS) \
		-shared -fPIC \
		-o $@ \
		-I$(UV_ROOT)/include \
		-I$(LCB_HEADERS) \
		$^ \
		-L$(UV_ROOT) -Wl,-rpath=$(UV_ROOT) -luv
