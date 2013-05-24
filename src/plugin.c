#include "plugin-internal.h"

/******************************************************************************
 ******************************************************************************
 ** Connection Functions                                                     **
 ******************************************************************************
 ******************************************************************************/
static void connect_callback(uv_connect_t *req, int status)
{
    my_uvreq_t *uvr = (my_uvreq_t*)req;

    if (uvr->cb.conn) {
        uvr->cb.conn(&uvr->socket->base, status);
    }

    lcbuv_decref_sock(uvr->socket);
    free(uvr);
}

static int start_connect(lcb_io_opt_t iobase,
                         lcb_sockdata_t *sockbase,
                         const struct sockaddr *name,
                         unsigned int namelen,
                         lcb_io_connect_cb callback)
{
    dCOMMON_VARS(iobase, sockbase);
    my_uvreq_t *uvr;
    int ret;
    int err_is_set = 0;

    uvr = lcbuv_alloc_uvreq(sock, callback);
    if (!uvr) {
        return -1;
    }

    if (namelen == sizeof(struct sockaddr_in)) {
        ret = uv_tcp_connect(&uvr->uvreq.conn,
                             &sock->tcp,
                             *(struct sockaddr_in*)name,
                             connect_callback);

    } else if (namelen == sizeof(struct sockaddr_in6)) {
        ret = uv_tcp_connect6(&uvr->uvreq.conn,
                              &sock->tcp,
                              *(struct sockaddr_in6*)name,
                              connect_callback);

    } else {
        io->base.v.v1.error = EINVAL;
        ret = -1;
        err_is_set = 1;
    }

    if (ret) {
        if (!err_is_set) {
            lcbuv_set_last_error(io, ret);
        }

        free(uvr);

    } else {
        lcbuv_incref_sock(sock);
    }

    return ret;
}

/******************************************************************************
 ******************************************************************************
 ** my_writebuf_t functions                                                  **
 ******************************************************************************
 ******************************************************************************/
static lcb_io_writebuf_t *create_writebuf(lcb_io_opt_t iobase)
{
    my_writebuf_t *ret = calloc(1, sizeof(*ret));
    ret->base.parent = iobase;
    return (lcb_io_writebuf_t*)ret;
}

static void release_writebuf(lcb_io_opt_t iobase, lcb_io_writebuf_t *buf)
{
    lcbuv_free_bufinfo_common(&buf->buffer);
    memset(buf, 0xff, sizeof(my_writebuf_t));
    free(buf);

    (void)iobase;
}


/******************************************************************************
 ******************************************************************************
 ** Write Functions                                                          **
 ******************************************************************************
 ******************************************************************************/
static void write_callback(uv_write_t *req, int status)
{
    my_writebuf_t *wbuf = PTR_FROM_FIELD(my_writebuf_t, req, write);
    my_sockdata_t *sock = wbuf->sock;
    lcb_io_write_cb callback = req->data;

    if (callback) {
        callback(&sock->base, &wbuf->base, status);
    }

    SOCK_DECR_PENDING(sock, write);
    lcbuv_decref_sock(sock);
}

static int start_write(lcb_io_opt_t iobase,
                       lcb_sockdata_t *sockbase,
                       lcb_io_writebuf_t *wbufbase,
                       lcb_io_write_cb callback)
{
    dCOMMON_VARS(iobase, sockbase);
    my_writebuf_t *wbuf = (my_writebuf_t*)wbufbase;
    int ii;
    int ret;

    wbuf->sock = sock;
    wbuf->write.data = callback;

    for (ii = 0; ii < 2; ii++) {
        wbuf->uvbuf[ii].base = wbuf->base.buffer.iov[ii].iov_base;
        wbuf->uvbuf[ii].len = wbuf->base.buffer.iov[ii].iov_len;
    }

    ret = uv_write(&wbuf->write,
                   (uv_stream_t*)&sock->tcp, wbuf->uvbuf, 2, write_callback);
    lcbuv_set_last_error(io, ret);

    if (ret == 0) {
        lcbuv_incref_sock(sock);
        SOCK_INCR_PENDING(sock, write);
    }

    return ret;
}


/******************************************************************************
 ******************************************************************************
 ** Read Functions                                                           **
 ******************************************************************************
 ******************************************************************************/
static uv_buf_t alloc_cb(uv_handle_t *handle, size_t suggested_size)
{
    uv_buf_t ret;
    my_sockdata_t *sock = PTR_FROM_FIELD(my_sockdata_t, handle, tcp);
    struct lcb_buf_info *bi = &sock->base.read_buffer;


    assert(sock->cur_iov < 2);

    ret.base = bi->iov[sock->cur_iov].iov_base;
    ret.len = bi->iov[sock->cur_iov].iov_len;

    if (sock->cur_iov == 1 ||
            bi->iov[sock->cur_iov+1].iov_len == 0 ||
            bi->iov[sock->cur_iov+1].iov_base == NULL) {

        sock->read_done = 1;
    }

    sock->cur_iov++;

    (void)suggested_size;

    return ret;
}

static void read_cb(uv_stream_t *stream, ssize_t nread, uv_buf_t buf)
{
    my_sockdata_t *sock = PTR_FROM_FIELD(my_sockdata_t, stream, tcp);
    lcb_io_read_cb callback = stream->data;
    assert(sock->read_done < 2);

    if (nread > 0 && sock->read_done == 0) {
        return;

    }

    sock->read_done++;
    SOCK_DECR_PENDING(sock, read);

    uv_read_stop(stream);
    stream->data = NULL;

    if (callback) {
        callback(&sock->base, nread);
    }

    lcbuv_decref_sock(sock);
    (void)buf;
}

static int start_read(lcb_io_opt_t iobase,
                      lcb_sockdata_t *sockbase,
                      lcb_io_read_cb callback)
{
    dCOMMON_VARS(iobase, sockbase);
    int ret;

    sock->read_done = 0;
    sock->cur_iov = 0;
    sock->tcp.data = callback;

    ret = uv_read_start((uv_stream_t*)&sock->tcp, alloc_cb, read_cb);
    lcbuv_set_last_error(io, ret);

    if (ret == 0) {
        SOCK_INCR_PENDING(sock, read);
        lcbuv_incref_sock(sock);
    }

    return ret;
}

/******************************************************************************
 ******************************************************************************
 ** Async Errors                                                             **
 ******************************************************************************
 ******************************************************************************/
static void err_idle_cb(uv_idle_t *idle, int status)
{
    my_uvreq_t *uvr = (my_uvreq_t*)idle;
    lcb_io_error_cb callback = uvr->cb.err;

    uv_idle_stop(idle);
    uv_close((uv_handle_t*)idle, lcbuv_generic_close_cb);

    if (callback) {
        callback(&uvr->socket->base);
    }

    lcbuv_decref_sock(uvr->socket);
    (void)status;
}


static void send_error(lcb_io_opt_t iobase, lcb_sockdata_t *sockbase,
                       lcb_io_error_cb callback)
{
    dCOMMON_VARS(iobase, sockbase);
    my_uvreq_t *uvr = lcbuv_alloc_uvreq(sock, callback);

    if (!uvr) {
        return;
    }

    uv_idle_init(io->loop, &uvr->uvreq.idle);
    uv_idle_start(&uvr->uvreq.idle, err_idle_cb);
    lcbuv_incref_sock(sock);
}


void lcbuv_wire_rw_ops(lcb_io_opt_t iop)
{
    iop->v.v1.start_connect = start_connect;
    iop->v.v1.create_writebuf = create_writebuf;
    iop->v.v1.release_writebuf = release_writebuf;
    iop->v.v1.start_write = start_write;
    iop->v.v1.start_read = start_read;

    /**
     * Error handler
     */
    iop->v.v1.send_error = send_error;


}
