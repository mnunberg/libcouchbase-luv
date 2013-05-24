#include "plugin.h"

/**
 * TODO: Delete this before commit. This is to make Eclipse not complain
 * for some odd reason.
 */
#ifndef stderr
#define stderr NULL
#endif

typedef void (*v0_callback_t)(lcb_socket_t,short,void*);

/**
 * Wrapper for lcb_sockdata_t
 */
typedef struct {
    lcb_sockdata_t base;

    /* UV tcp handle. This is also a uv_stream_t */
    uv_tcp_t tcp;

    /** Connection request handle */
    uv_connect_t connreq;

    /** Callback for connect. Would be nice if we could discard this */
    lcb_io_connect_cb conncb;

    /** Callback for reads. */
    lcb_io_read_cb readcb;

    /** Callback for errors */
    lcb_io_error_cb errcb;

    /** Current iov index in the read buffer */
    unsigned char cur_iov;

    /** Flag indicating whether uv_read_stop should be called on the next call */
    unsigned char read_done;

} my_sockdata_t;

typedef struct {
    lcb_io_writebuf_t base;

    /** Write handle */
    uv_write_t write;

    /** Buffer structures corresponding to buf_info */
    uv_buf_t uvbuf[2];

    /** Write callback */
    lcb_io_write_cb callback;

    /** Parent socket structure */
    my_sockdata_t *sock;

} my_writebuf_t;


typedef struct {
    struct lcb_io_opt_st base;
    uv_loop_t *loop;

    /** Refcount. When this hits zero we free this */
    unsigned int refcount;

    /** Whether using a user-initiated loop. In this case we don't wait/stop */
    int external_loop;
} my_iops_t;

typedef struct {
    uv_timer_t uvt;
    v0_callback_t callback;
    void *cb_arg;
    my_iops_t *parent;
} my_timer_t;


#define PTR_FROM_FIELD(t, p, fld) \
    ((t*)((char*)p-(offsetof(t, fld))))


#define dCOMMON_VARS(iobase, sockbase) \
    my_iops_t *io = (my_iops_t*)iobase; \
    my_sockdata_t *sock = (my_sockdata_t*)sockbase;


static void decref_iops(lcb_io_opt_t iobase)
{
    my_iops_t *io = (my_iops_t*)iobase;
    if (io->refcount--) {
        return;
    }
    memset(io, 0xff, sizeof(*io));
}


static void set_last_error(my_iops_t *io, int error)
{
    if (!error) {
        io->base.v.v1.error = 0;
        return;
    }
    io->base.v.v1.error = lcbuv_errno_map(uv_last_error(io->loop).code);
}

/******************************************************************************
 ******************************************************************************
 ** Socket Functions                                                         **
 ******************************************************************************
 ******************************************************************************/
static lcb_sockdata_t *create_socket(lcb_io_opt_t iobase,
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

    ret->base.refcount = 1;

    set_last_error(io, 0);

    (void)domain;
    (void)type;
    (void)protocol;

    return (lcb_sockdata_t*)ret;
}


static void close_callback(uv_handle_t *handle)
{
    my_sockdata_t *sock = PTR_FROM_FIELD(my_sockdata_t, handle, tcp);
    my_iops_t *io = (my_iops_t*)sock->base.parent;

    assert(sock->base.refcount == 0);

    free(sock->base.read_buffer.ringbuffer);
    free(sock->base.read_buffer.root);
    uv_unref(handle);
    memset(sock, 0xEE, sizeof(*sock));
    free(sock);
    decref_iops(&io->base);
}

static int decref_socket(lcb_io_opt_t iobase, lcb_sockdata_t *sockbase)
{
    dCOMMON_VARS(iobase, sockbase);
    assert(sockbase->refcount);
    sockbase->refcount--;

    if (sockbase->refcount) {
        return 0;
    }

    uv_close((uv_handle_t*)&sock->tcp, close_callback);

    (void)io;
    return 0;
}


/******************************************************************************
 ******************************************************************************
 ** Connection Functions                                                     **
 ******************************************************************************
 ******************************************************************************/
static void connect_callback(uv_connect_t *req, int status)
{
    my_sockdata_t *sock = PTR_FROM_FIELD(my_sockdata_t, req, connreq);
    if (sock->conncb) {
        sock->conncb(&sock->base, status);
    } else {
        abort();
    }
}

static int start_connect(lcb_io_opt_t iobase,
                         lcb_sockdata_t *sockbase,
                         const struct sockaddr *name,
                         unsigned int namelen,
                         lcb_io_connect_cb callback)
{
    dCOMMON_VARS(iobase, sockbase);
    int ret;

    sock->conncb = callback;

    if (namelen == sizeof(struct sockaddr_in)) {
        ret = uv_tcp_connect(&sock->connreq,
                             &sock->tcp,
                             *(struct sockaddr_in*)name,
                             connect_callback);

    } else if (namelen == sizeof(struct sockaddr_in6)) {
        ret = uv_tcp_connect6(&sock->connreq,
                              &sock->tcp,
                              *(struct sockaddr_in6*)name,
                              connect_callback);

    } else {
        io->base.v.v1.error = EINVAL;
        return -1;
    }

    set_last_error(io, ret);

    return ret;
}

/******************************************************************************
 ******************************************************************************
 ** my_writebuf_t functions                                                     **
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
    free(buf->buffer.ringbuffer);
    free(buf->buffer.root);
    memset(buf, 0xff, sizeof(my_writebuf_t));

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

    if (wbuf->callback) {
        wbuf->callback(&sock->base, &wbuf->base, status);
    } else {
        abort();
    }
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

    wbuf->callback = callback;
    wbuf->sock = sock;

    for (ii = 0; ii < 2; ii++) {
        wbuf->uvbuf[ii].base = wbuf->base.buffer.iov[ii].iov_base;
        wbuf->uvbuf[ii].len = wbuf->base.buffer.iov[ii].iov_len;
    }

    ret = uv_write(&wbuf->write,
                   (uv_stream_t*)&sock->tcp, wbuf->uvbuf, 2, write_callback);

    set_last_error(io, ret);
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
    assert(sock->read_done < 2);

    if (nread < 1 || sock->read_done) {
        sock->read_done++;
        uv_read_stop(stream);
        if (sock->readcb) {
            sock->readcb(&sock->base, nread);
        }
    }

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
    sock->readcb = callback;

    ret = uv_read_start((uv_stream_t*)&sock->tcp, alloc_cb, read_cb);

    set_last_error(io, ret);
    return ret;
}

/******************************************************************************
 ******************************************************************************
 ** Async Errors                                                             **
 ******************************************************************************
 ******************************************************************************/
static void idle_close_cb(uv_handle_t *handle)
{
    free(handle);
}

static void idle_cb(uv_idle_t *idle, int status)
{
    my_sockdata_t *sock = idle->data;
    uv_close((uv_handle_t*)idle, idle_close_cb);

    if (sock->errcb) {
        sock->errcb(&sock->base);
    }
    (void)status;
}


static void send_error(lcb_io_opt_t iobase, lcb_sockdata_t *sockbase,
                       lcb_io_error_cb callback)
{
    dCOMMON_VARS(iobase, sockbase);
    uv_idle_t *idle = calloc(1, sizeof(*idle));
    sock->errcb = callback;
    uv_idle_init(io->loop, idle);
    uv_idle_start(idle, idle_cb);
}


/******************************************************************************
 ******************************************************************************
 ** Timer Functions                                                          **
 ** There are just copied from the old couchnode I/O code                    **
 ******************************************************************************
 ******************************************************************************/
static void timer_cb(uv_timer_t *uvt, int status)
{
    my_timer_t *timer = (my_timer_t *)uvt;
    if (timer->callback) {
        timer->callback(-1, 0, timer->cb_arg);
    }
    (void)status;
}

static void *create_timer(lcb_io_opt_t iobase)
{
    my_iops_t *io = (my_iops_t*)iobase;
    my_timer_t *timer = calloc(1, sizeof(*timer));
    if (!timer) {
        return NULL;
    }

    timer->parent = io;
    io->refcount++;
    uv_timer_init(io->loop, &timer->uvt);

    return timer;
}

static int update_timer(lcb_io_opt_t iobase,
                        void *timer_opaque,
                        lcb_uint32_t usec,
                        void *cbdata,
                        v0_callback_t callback)
{
    my_timer_t *timer = (my_timer_t*)timer_opaque;

    timer->callback = callback;
    timer->cb_arg = cbdata;

    (void)iobase;

    return uv_timer_start(&timer->uvt, timer_cb, usec, 0);
}

static void delete_timer(lcb_io_opt_t iobase, void *timer_opaque)
{
    my_timer_t *timer = (my_timer_t*)timer_opaque;

    uv_timer_stop(&timer->uvt);
    timer->callback = NULL;

    (void)iobase;
}

static void timer_close_cb(uv_handle_t *handle)
{
    my_timer_t *timer = (my_timer_t*)handle;
    decref_iops(&timer->parent->base);
    memset(timer, 0xff, sizeof(*timer));
    free(timer);
}

static void destroy_timer(lcb_io_opt_t io, void *timer_opaque)
{
    delete_timer(io, timer_opaque);
    uv_close((uv_handle_t *)timer_opaque, timer_close_cb);
}

/******************************************************************************
 ******************************************************************************
 ** Event Loop Functions                                                     **
 ******************************************************************************
 ******************************************************************************/
static void run_event_loop(lcb_io_opt_t iobase)
{
    my_iops_t *io = (my_iops_t*)iobase;
    io->refcount++;
    if (!io->external_loop) {
        uv_run(io->loop, UV_RUN_DEFAULT);
    }
}

static void stop_event_loop(lcb_io_opt_t iobase)
{
    my_iops_t *io = (my_iops_t*)iobase;
    if (!io->external_loop) {
        uv_stop(io->loop);
    }
    decref_iops(iobase);
}

LIBCOUCHBASE_API
lcb_error_t lcbuv_new_iops(lcb_io_opt_t *io, uv_loop_t *loop)
{
    my_iops_t *ret = calloc(1, sizeof(*ret));
    lcb_io_opt_t iop;

    if (!ret) {
        return LCB_CLIENT_ENOMEM;
    }

    iop = &ret->base;
    iop->version = 1;

    iop->v.v1.create_socket = create_socket;
    iop->v.v1.destroy_socket = decref_socket;

    iop->v.v1.start_connect = start_connect;

    iop->v.v1.create_writebuf = create_writebuf;
    iop->v.v1.release_writebuf = release_writebuf;

    iop->v.v1.start_write = start_write;

    iop->v.v1.start_read = start_read;

    /**
     * Error handler
     */
    iop->v.v1.send_error = send_error;

    /**
     * v0 functions
     */
    iop->v.v1.create_timer = create_timer;
    iop->v.v1.update_timer = update_timer;
    iop->v.v1.delete_timer = delete_timer;
    iop->v.v1.destroy_timer = destroy_timer;

    iop->v.v1.run_event_loop = run_event_loop;
    iop->v.v1.stop_event_loop = stop_event_loop;

    /* dtor */
    iop->destructor = decref_iops;

    ret->refcount = 1;

    *io = iop;

    if (loop) {
        ret->external_loop = 1;

    } else {
        loop = uv_default_loop();
    }

    ret->loop = loop;

    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
lcb_error_t lcb_create_libuv_io_opts(int version, lcb_io_opt_t *io, void *arg)
{
    (void)arg;

    if (version != 0) {
        return LCB_PLUGIN_VERSION_MISMATCH;
    }

    return lcbuv_new_iops(io, NULL);
}
