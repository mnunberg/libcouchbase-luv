#include "lcb_luv_internal.h"

YOLOG_STATIC_INIT("socket", YOLOG_DEBUG);

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

    if (sock == NULL || ev == NULL) {
        return;
    }

    if (sock) {
        if (sock->prep_active) {
            uv_prepare_stop(&sock->prep);
            sock->prep_active = 0;
        }

        if (sock->read.readhead_active) {
            uv_read_stop((uv_stream_t*)&sock->tcp);
        }

        /* clear all the state flags */
        for (ii = 0; ii < LCB_LUV_EV_RDWR_MAX; ii++) {
            sock->evstate[ii].flags = 0;
        }
        sock->event = NULL;
    }

    if (ev) {
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
            yolog_debug("Setting pending flags because write requested and write buffer is free");
            sock->evstate[LCB_LUV_EV_WRITE].flags |= LCB_LUV_EVf_PENDING;
        }

        lcb_luv_schedule_enable(sock);
    } else {
        /* we might need to disable scheduling, unless we have data inside
         * the read (read events are pending)
         */
        if ((sock->evstate[LCB_LUV_EV_READ].flags & LCB_LUV_EVf_PENDING) == 0) {

        }
    }

    return 1;
}


libcouchbase_socket_t
lcb_luv_socket(struct libcouchbase_io_opt_st *iops,
               int domain,
               int type,
               int protocol)
{
    lcb_luv_socket_t newsock;
    libcouchbase_socket_t idx;
    iops->error = EINVAL;
    yolog_debug("Socket called!");

    if ( (domain != AF_INET && domain != AF_INET6) ||
            type != SOCK_STREAM || protocol != IPPROTO_TCP)  {
        yolog_err("Bad arguments: domain=%d, type=%d, protocol=%d",
                domain, type, protocol);
        return -1;
    }

    /* Find the next 'file descriptor' */
    idx = find_free_idx(IOPS_COOKIE(iops));
    if (idx == -1) {
        iops->error = ENFILE;
        return -1;
    }

    newsock = calloc(1, sizeof(struct lcb_luv_socket_st));
    newsock->idx = idx;
    newsock->parent = IOPS_COOKIE(iops);

    uv_prepare_init(newsock->parent->loop, &newsock->prep);
    newsock->prep_active = 0;

    newsock->prep.data = newsock;
    newsock->u_req.req.data = newsock;

    uv_tcp_init(IOPS_COOKIE(iops)->loop, &newsock->tcp);

    IOPS_COOKIE(iops)->socktable[idx] = newsock;
    iops->error = 0;
    return idx;
}

static void lcb_luv_cb_connect(uv_connect_t* req, int status)
{
    lcb_luv_socket_t sock = (lcb_luv_socket_t)req->handle;
    struct lcb_luv_evstate_st *evstate = sock->evstate + LCB_LUV_EV_CONNECT;
    evstate->flags |= LCB_LUV_EVf_PENDING;
    yolog_debug("Connection callback: status=%d", status);

    if (status) {
        /* Error */
        evstate->err = (uv_last_error(sock->parent->loop)).code;
    } else {
        evstate->err = 0;
    }

    /* Since this is a write event on a socket, see if we should send the
     * callback
     */
    if (sock->event && (sock->event->lcb_events & LIBCOUCHBASE_WRITE_EVENT)) {
        yolog_debug("Invoking libcouchbase write callback...");
        sock->event->lcb_cb(sock->idx, LIBCOUCHBASE_WRITE_EVENT, sock->event->lcb_arg);
    }
}

int
lcb_luv_connect(struct libcouchbase_io_opt_st *iops,
                libcouchbase_socket_t sock_i,
                const struct sockaddr *saddr,
                unsigned int saddr_len)
{
    int status, retval;
    lcb_luv_socket_t sock = lcb_luv_sock_from_idx(iops, sock_i);
    struct lcb_luv_evstate_st *evstate;

    if (sock == NULL) {
        iops->error = EBADF;
        return -1;
    }


    evstate = sock->evstate + LCB_LUV_EV_CONNECT;

    /* Subsequent calls to connect() */
    if (EVSTATE_IS(evstate, ACTIVE)) {
        yolog_debug("We were called again for connect()");
        if (EVSTATE_IS(evstate, PENDING)) {
            retval = evstate->err;
            if (retval) {
                iops->error = retval;
                retval = -1;
            } else {
                evstate->flags |= LCB_LUV_EVf_CONNECTED;
                iops->error = 0;
            }
            evstate->flags &= ~(LCB_LUV_EVf_PENDING);
        } else {
            retval = -1;
            if (EVSTATE_IS(evstate, CONNECTED)) {
                iops->error = EISCONN;
            } else {
                iops->error = EINPROGRESS;
            }
        }
        yolog_debug("Returning %d for status", retval);
        return retval;
    }

    retval = -1;
    /* Else, first call to connect() */
    if (saddr_len == sizeof(struct sockaddr_in)) {
        status = uv_tcp_connect(&sock->u_req.connect, &sock->tcp,
                *(struct sockaddr_in*)saddr, lcb_luv_cb_connect);
    } else if (saddr_len == sizeof(struct sockaddr_in6)) {
        status = uv_tcp_connect6(&sock->u_req.connect, &sock->tcp,
                *(struct sockaddr_in6*)saddr, lcb_luv_cb_connect);
    } else {
        /* Neither AF_INET or AF_INET6 */
        iops->error = EAFNOSUPPORT;
        return -1;
    }

    if (status == 0) {
        iops->error = EINPROGRESS;
        evstate->flags |= LCB_LUV_EVf_ACTIVE;

    } else {
        iops->error = (uv_last_error(IOPS_COOKIE(iops)->loop)).code;
    }
    return retval;
}

void
lcb_luv_close(struct libcouchbase_io_opt_st *iops, libcouchbase_socket_t sock_i)
{
    /* Free a socket */
    lcb_luv_socket_t sock = lcb_luv_sock_from_idx(iops, sock_i);
    if (!sock) {
        iops->error = EBADF;
        return;
    }

    if (sock->event) {
        sock->event->handle = NULL;
    }

    if (EVSTATE_IS(&sock->evstate[LCB_LUV_EV_READ], ACTIVE)) {
        uv_read_stop((uv_stream_t*)&sock->tcp);
    }

    sock->parent->socktable[sock_i] = NULL;
    free(sock);
    return;
}
