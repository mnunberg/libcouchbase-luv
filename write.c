#include "lcb_luv_internal.h"

/* Writing is a bit more complex than reading, since an event-based write
 * mechanism needs us to let it known when it can write.
 * In this case we will utilize a uv_prepare_t structure, which will trigger
 * the lcb write callback in cases where we have previously pretended that a write
 * will not block.
 */

YOLOG_STATIC_INIT("write", YOLOG_DEBUG);

static void
write_cb(uv_write_t *req, int status)
{
    lcb_luv_socket_t sock = (lcb_luv_socket_t)req->data;
    struct lcb_luv_evstate_st *evstate;

    if (!sock) {
        fprintf(stderr, "Got write callback (req=%p) without socket\n", req);
        return;
    }

    evstate = EVSTATE_FIND(sock, WRITE);

    if (status) {
        evstate->err =
                lcb_luv_errno_map((uv_last_error(sock->parent->loop)).code);
    }
    yolog_debug("Flush done. Flushed %d bytes", sock->write.buf.len);
    sock->write.pos = 0;
    sock->write.nb = 0;
    evstate->flags |= LCB_LUV_EVf_PENDING;
    evstate->flags &= ~(LCB_LUV_EVf_FLUSHING);

    if (SOCK_EV_ENABLED(sock, WRITE)) {
        sock->event->lcb_cb(sock->idx, LIBCOUCHBASE_WRITE_EVENT, sock->event->lcb_arg);

        if (sock->write.nb) {
            evstate->flags &= ~(LCB_LUV_EVf_PENDING);
            lcb_luv_flush(sock);
        }
    }

    lcb_luv_socket_unref(sock);
}


/**
 * Flush the write buffers
 */
void
lcb_luv_flush(lcb_luv_socket_t sock)
{
    int status;
    struct lcb_luv_evstate_st *evstate;
    if (sock->write.nb == 0) {
        return;
    }

    evstate = EVSTATE_FIND(sock, WRITE);
    if(EVSTATE_IS(evstate, FLUSHING)) {
        yolog_warn("Not flushing because we are in the middle of a flush");
        return;
    }

    sock->write.buf.base = sock->write.data;
    sock->write.buf.len = sock->write.nb;
    yolog_debug("Will flush");
    status = uv_write(&sock->u_req.write,
                        (uv_stream_t*)&sock->tcp,
                        &sock->write.buf, 1, write_cb);
    lcb_luv_socket_ref(sock);

    if (status) {
        evstate->err =
                lcb_luv_errno_map((uv_last_error(sock->parent->loop)).code);
    }
    evstate->flags |= LCB_LUV_EVf_FLUSHING;
}


static libcouchbase_ssize_t
write_common(lcb_luv_socket_t sock, const void *buf, size_t len, int *errno_out)
{
    libcouchbase_ssize_t ret;
    struct lcb_luv_evstate_st *evstate = EVSTATE_FIND(sock, WRITE);

    yolog_debug("%d: Requested to write %d bytes from %p", sock->idx, len, buf);

    if (evstate->err) {
        yolog_warn("Socket has pending error %d", evstate->err);
        *errno_out = evstate->err;
        evstate->err = 0;
        return -1;
    }

    if (EVSTATE_IS(evstate, FLUSHING)) {
        yolog_warn("Will not write because we are inside a flush");
        *errno_out = EWOULDBLOCK;
        return -1;
    }

    ret = MINIMUM(len, sizeof(sock->write.data) - sock->write.nb);
    if (ret == 0) {
        yolog_warn("We have no more space inside the buffer");
        *errno_out = EWOULDBLOCK;
        return -1;
    }



    memcpy(sock->write.data + sock->write.pos, buf, ret);
//    lcb_luv_hexdump(sock->write.data + sock->write.pos, ret);
    sock->write.pos += ret;
    sock->write.nb += ret;
    yolog_debug("Returning %d", ret);
    return ret;
}

libcouchbase_ssize_t
lcb_luv_send(struct libcouchbase_io_opt_st *iops,
             libcouchbase_socket_t sock_i,
             const void *msg,
             libcouchbase_size_t len,
             int flags)
{
    lcb_luv_socket_t sock = lcb_luv_sock_from_idx(iops, sock_i);
    libcouchbase_ssize_t ret;
    if (sock == NULL) {
        iops->error = EBADF;
        return -1;
    }
    ret = write_common(sock, msg, len, &iops->error);
    if (ret > 0) {
        lcb_luv_schedule_enable(sock);
    }
    return ret;
}

libcouchbase_ssize_t
lcb_luv_sendv(struct libcouchbase_io_opt_st *iops,
              libcouchbase_socket_t sock_i,
              struct libcouchbase_iovec_st *iov,
              libcouchbase_size_t niov)
{
    libcouchbase_ssize_t nr = 0, iret;
    int ii, my_errno = 0;
    lcb_luv_socket_t sock = lcb_luv_sock_from_idx(iops, sock_i);
    if (sock == NULL) {
        iops->error = EBADF;
        return -1;
    }
    for (ii = 0; ii < niov; ii++) {

        if (iov[ii].iov_len == 0) {
            break;
        }

        iret = write_common(sock, iov[ii].iov_base, iov[ii].iov_len, &my_errno);
        if (iret > 0) {
            nr += iret;
        } else {
            break;
        }
    }
    if (my_errno) {
        iops->error = my_errno;
    }
    if (nr > 0) {
        lcb_luv_schedule_enable(sock);
        return nr;
    } else {
        return -1;
    }
}
