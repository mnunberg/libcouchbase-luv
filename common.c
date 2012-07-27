#include "lcb_luv_internal.h"

YOLOG_STATIC_INIT("common", YOLOG_INFO);

static void
maybe_callout(lcb_luv_socket_t sock)
{
    short which = 0;
    if (!sock->event) {
        /* !? */
        return;
    }
    lcb_luv_flush(sock);

#define check_if_ready(fld) \
    if (EVSTATE_IS(&(sock->evstate[LCB_LUV_EV_ ## fld]), PENDING) && \
            (sock->event->lcb_events & LIBCOUCHBASE_ ## fld ## _EVENT)) { \
        which |= LIBCOUCHBASE_ ## fld ## _EVENT; \
    }

    check_if_ready(READ);
    check_if_ready(WRITE);
#undef check_if_ready

    yolog_debug("Will determine if we need to call any functions..");
    yolog_debug("which=%x, wait for=%x", which, sock->event->lcb_events);
    if (which) {
        yolog_info(" ==== CB Invoking callback for %d =====", sock->idx);
        sock->event->lcb_cb(sock->idx, which, sock->event->lcb_arg);
        yolog_info("==== CB Done invoking callback for %d =====", sock->idx);
        lcb_luv_flush(sock);
    }

}

static void
prepare_cb(uv_prepare_t *handle, int status)
{
    lcb_luv_socket_t sock = (lcb_luv_socket_t)handle->data;
    yolog_err("prepcb start");
    if (!sock) {
        fprintf(stderr, "We were called with prepare_t %p, with a missing socket\n",
                handle);
        return;
    }

    lcb_luv_socket_ref(sock);
    maybe_callout(sock);
    lcb_luv_socket_unref(sock);
    yolog_err("prepcb stop");
}

void
lcb_luv_schedule_enable(lcb_luv_socket_t sock)
{
    if (sock->prep_active) {
        yolog_debug("prep_active is true");
        return;
    }

    yolog_debug("Will try and schedule prepare callback for %d", sock->idx);
    lcb_luv_socket_ref(sock);
    uv_prepare_start(&sock->prep, prepare_cb);
    sock->prep_active = 1;
}

void
lcb_luv_schedule_disable(lcb_luv_socket_t sock)
{
    if (sock->prep_active == 0) {
        yolog_debug("prep_active is false");
        return;
    }
    yolog_debug("Disabling prepare");
    uv_prepare_stop(&sock->prep);
    lcb_luv_socket_unref(sock);
    sock->prep_active = 0;
}

static inline libcouchbase_socket_t
find_free_idx(struct lcb_luv_cookie_st *cookie)
{
    libcouchbase_socket_t ret = -1;
    unsigned int nchecked = 0;
    while (nchecked < cookie->fd_max && ret == -1) {
        if (cookie->socktable[cookie->fd_next] == NULL) {
            ret = cookie->fd_next;
        }
        cookie->fd_next = ((cookie->fd_next + 1 ) % cookie->fd_max);
        nchecked++;
    }
    return ret;
}


lcb_luv_socket_t
lcb_luv_socket_new(struct libcouchbase_io_opt_st *iops)
{
    /* Find the next 'file descriptor' */
    libcouchbase_socket_t idx;
    lcb_luv_socket_t newsock;
    idx = find_free_idx(IOPS_COOKIE(iops));
    if (idx == -1) {
        iops->error = ENFILE;
        return NULL;
    }

    newsock = calloc(1, sizeof(struct lcb_luv_socket_st));
    newsock->idx = idx;
    newsock->parent = IOPS_COOKIE(iops);

    uv_prepare_init(newsock->parent->loop, &newsock->prep);
    newsock->prep_active = 0;

    newsock->prep.data = newsock;
    newsock->u_req.req.data = newsock;
    newsock->refcount = 1;

    uv_tcp_init(IOPS_COOKIE(iops)->loop, &newsock->tcp);

    IOPS_COOKIE(iops)->socktable[idx] = newsock;
    iops->error = 0;
    return newsock;
}

void lcb_luv_socket_free(lcb_luv_socket_t sock)
{
    assert(sock->event == NULL);
    assert(sock->idx == -1);
    assert(sock->refcount == 0);
    assert(sock->prep_active == 0);
    assert(sock->read.readhead_active == 0);
    free(sock);
}

unsigned long
lcb_luv_socket_unref(lcb_luv_socket_t sock)
{
    unsigned long ret;
    sock->refcount--;
    ret = sock->refcount;
    if (ret == 0) {
        lcb_luv_socket_free(sock);
    }
    return ret;
}

void
lcb_luv_socket_deinit(lcb_luv_socket_t sock)
{
    if (sock->idx == -1) {
        return;
    }

    yolog_warn("Deinitializing socket %d", sock->idx);

    lcb_luv_schedule_disable(sock);

    if (sock->event && sock->event->handle == sock) {
        sock->event->handle = NULL;
        sock->event = NULL;
    }

    if (EVSTATE_IS(&sock->evstate[LCB_LUV_EV_READ], ACTIVE)) {
        uv_read_stop((uv_stream_t*)&sock->tcp);
        sock->evstate[LCB_LUV_EV_READ].flags = 0;
    }

    sock->parent->socktable[sock->idx] = 0;
    sock->idx = -1;
    lcb_luv_socket_unref(sock);
}


lcb_luv_socket_t
lcb_luv_sock_from_idx(struct libcouchbase_io_opt_st *iops, libcouchbase_socket_t idx)
{
    if (idx < 0) {
        return NULL;
    }

    if (idx > IOPS_COOKIE(iops)->fd_max) {
        return NULL;
    }

    return IOPS_COOKIE(iops)->socktable[idx];
}

void *
lcb_luv_create_event(struct libcouchbase_io_opt_st *iops)
{
    struct lcb_luv_event_st *ev = calloc(1, sizeof(struct lcb_luv_event_st));
    return ev;
}

void
lcb_luv_delete_event(struct libcouchbase_io_opt_st *iops,
                     libcouchbase_socket_t sock_i,
                     void *event_opaque)
{
    lcb_luv_socket_t sock = lcb_luv_sock_from_idx(iops, sock_i);
    struct lcb_luv_event_st *ev = (struct lcb_luv_event_st*)event_opaque;
    int ii;

    if (sock == NULL && ev == NULL) {
        return;
    }

    if (sock) {
        lcb_luv_schedule_disable(sock);
        lcb_luv_read_stop(sock);

        /* clear all the state flags */
        for (ii = 0; ii < LCB_LUV_EV_RDWR_MAX; ii++) {
            sock->evstate[ii].flags = 0;
        }
        sock->event = NULL;
    }

    if (ev && (ev->handle == sock || sock == NULL) ) {
        ev->handle = NULL;
        ev->lcb_events = 0;
    }
}

void
lcb_luv_destroy_event(struct libcouchbase_io_opt_st *iops,
                      void *event_opaque)
{
    struct lcb_luv_event_st *ev = (struct lcb_luv_event_st*)event_opaque;
    if (ev->handle) {
        ev->handle->event = NULL;
    }
    free(ev);
}

int
lcb_luv_update_event(struct libcouchbase_io_opt_st *iops,
                     libcouchbase_socket_t sock_i,
                     void *event_opaque,
                     short flags,
                     void *cb_data,
                     lcb_luv_callback_t cb)
{
    struct lcb_luv_event_st *event = (struct lcb_luv_event_st*)event_opaque;
    /* Check to see if our 'socket' is valid */
    lcb_luv_socket_t sock = lcb_luv_sock_from_idx(iops, sock_i);
    if (sock == NULL) {
        yolog_err("Requested update on invalid socket: fd=%d", sock_i);
        return 0;
    }

    yolog_debug("Requested events %x", flags);

    if (sock->event) {
        assert(sock->event == event);
        assert(event->handle == sock);
    } else {
        sock->event = event;
        event->handle = sock;
    }

    event->lcb_cb = cb;
    event->lcb_arg = cb_data;
    event->lcb_events = flags;

    /* trigger a read-ahead */
    if (flags & LIBCOUCHBASE_READ_EVENT) {
        lcb_luv_read_nudge(sock);
    }

    if (flags & LIBCOUCHBASE_WRITE_EVENT) {
        /* for writes, we either have data to be flushed, or we want to wait
         * until we're able to write data. In any event, we schedule for this
         */
        if ((sock->evstate[LCB_LUV_EV_WRITE].flags & LCB_LUV_EVf_FLUSHING) == 0 &&
                sock->write.nb == 0) {
//            yolog_debug("Setting pending flags because write requested and write buffer is free");
            sock->evstate[LCB_LUV_EV_WRITE].flags |= LCB_LUV_EVf_PENDING;
        }

        lcb_luv_schedule_enable(sock);
    } else {
        /* we might need to disable scheduling, unless we have data inside
         * the read (read events are pending)
         */
        if ((sock->evstate[LCB_LUV_EV_READ].flags & LCB_LUV_EVf_PENDING) == 0) {
            /* Do we do anything here? */
        }
    }

    return 1;
}

int
lcb_luv_errno_map(int uverr)
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

#ifndef EADDRINFO
#define EADDRINFO EAI_SYSTEM
#endif

#ifndef EAISOCKTYPE
#define EAISOCKTYPE EAI_SOCKTYPE
#endif

#ifndef ECHARSET
#define ECHARSET 0
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
