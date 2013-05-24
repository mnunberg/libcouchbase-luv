#include "plugin-internal.h"

static int errno_map(int uverr)
{

#ifndef UNKNOWN
#define UNKNOWN -1
#endif

#ifndef EAIFAMNOSUPPORT
#define EAIFAMNOSUPPORT EAI_FAMILY
#endif

#ifndef EAISERVICE
#define EAISERVICE EAI_SERVICE
#endif

#ifndef EAI_SYSTEM
#define EAI_SYSTEM -11
#endif
#ifndef EADDRINFO
#define EADDRINFO EAI_SYSTEM
#endif

#ifndef EAISOCKTYPE
#define EAISOCKTYPE EAI_SOCKTYPE
#endif

#ifndef ECHARSET
#define ECHARSET 0
#endif

#ifndef EOF
#define EOF -1
#endif

#ifndef ENONET
#define ENONET ENETDOWN
#endif

#ifndef ESHUTDOWN
#define ESHUTDOWN WSAESHUTDOWN
#endif

#define OK 0

    int ret = 0;
#define X(errnum,errname,errdesc) \
    if (uverr == UV_##errname) { \
        return errname; \
    }
    UV_ERRNO_MAP(X);

    return ret;

#undef X
}


my_uvreq_t *lcbuv_alloc_uvreq(my_sockdata_t *sock, void *callback)
{
    my_uvreq_t *ret = calloc(1, sizeof(*ret));
    if (!ret) {
        sock->base.parent->v.v1.error = ENOMEM;
        return NULL;
    }
    ret->socket = sock;
    ret->cb.ptr = callback;
    return ret;
}

void lcbuv_free_bufinfo_common(struct lcb_buf_info *bi)
{
    if (bi->root || bi->ringbuffer) {
        assert((void*)bi->root != (void*)bi->ringbuffer);
    }
    assert( (bi->ringbuffer == NULL && bi->root == NULL) ||
            (bi->root && bi->ringbuffer));

    free(bi->root);
    free(bi->ringbuffer);
    bi->root = NULL;
    bi->ringbuffer = NULL;
}

void lcbuv_set_last_error(my_iops_t *io, int error)
{
    if (!error) {
        io->base.v.v1.error = 0;
        return;
    }
    io->base.v.v1.error = errno_map(uv_last_error(io->loop).code);
}

void lcbuv_generic_close_cb(uv_handle_t *handle)
{
    free(handle);
}
