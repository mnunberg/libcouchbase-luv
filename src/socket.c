#include "plugin-internal.h"


static void sock_dump_pending(my_sockdata_t *sock)
{
    printf("Socket %p:\n", sock);
    printf("\tRead: %d\n",  sock->pending.read);
    printf("\tWrite: %d\n", sock->pending.write);
}

static void socket_closed_callback(uv_handle_t *handle);

static void sock_do_uv_close(my_sockdata_t *sock)
{
    if (!sock->uv_close_called) {
        sock->uv_close_called = 1;
        uv_close((uv_handle_t*)&sock->tcp, socket_closed_callback);
    }
}


void lcbuv_decref_sock(my_sockdata_t* sock)
{
    assert(sock->refcount);

    if (--sock->refcount) {
        return;
    }
    sock_do_uv_close(sock);
}

/******************************************************************************
 ******************************************************************************
 ** Socket Functions                                                         **
 ******************************************************************************
 ******************************************************************************/
lcb_sockdata_t *lcbuv_create_socket(lcb_io_opt_t iobase,
                                    int domain,
                                    int type,
                                    int protocol)
{
    my_sockdata_t *ret;
    my_iops_t *io = (my_iops_t*)iobase;

    ret = calloc(1, sizeof(*ret));
    if (!ret) {
        return NULL;
    }

    uv_tcp_init(io->loop, &ret->tcp);

    lcbuv_incref_iops(io);
    lcbuv_incref_sock(ret);

    lcbuv_set_last_error(io, 0);

    (void)domain;
    (void)type;
    (void)protocol;

    return (lcb_sockdata_t*)ret;
}

/**
 * This one is called from uv_close
 */
static void socket_closed_callback(uv_handle_t *handle)
{
    my_sockdata_t *sock = PTR_FROM_FIELD(my_sockdata_t, handle, tcp);
    my_iops_t *io = (my_iops_t*)sock->base.parent;

    assert(sock->refcount == 0);

    lcbuv_free_bufinfo_common(&sock->base.read_buffer);

    assert(sock->base.read_buffer.root == NULL);
    assert(sock->base.read_buffer.ringbuffer == NULL);

    memset(sock, 0xEE, sizeof(*sock));
    free(sock);

    lcbuv_decref_iops(&io->base);
}


/**
 * This one is asynchronously triggered, so as to ensure we don't have any
 * silly re-entrancy issues.
 */
static void socket_closing_cb(uv_idle_t *idle, int status)
{
    my_sockdata_t *sock = idle->data;

    uv_idle_stop(idle);
    uv_close((uv_handle_t*)idle, lcbuv_generic_close_cb);

    if (sock->pending.read) {
        /**
         * UV doesn't invoke read callbacks once the handle has been closed
         * so we must track this ourselves.
         */
        assert(sock->pending.read == 1);
        uv_read_stop((uv_stream_t*)&sock->tcp);
        sock->pending.read--;
        lcbuv_decref_sock(sock);
    }

    if (sock->pending.read || sock->pending.write) {
        sock_dump_pending(sock);
    }

    lcbuv_decref_sock(sock);
    sock_do_uv_close(sock);

    (void)status;
}

unsigned int lcbuv_close_socket(lcb_io_opt_t iobase, lcb_sockdata_t *sockbase)
{
    dCOMMON_VARS(iobase, sockbase);
    uv_idle_t *idle = calloc(1, sizeof(*idle));

    assert(sock->lcb_close_called == 0);

    sock->lcb_close_called = 1;
    idle->data = sock;
    uv_idle_init(io->loop, idle);
    uv_idle_start(idle, socket_closing_cb);

    (void)io;
    return 0;
}
