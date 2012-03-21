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
    if (!sock) {
        fprintf(stderr, "Got write callback (req=%p) without socket\n", req);
        return;
    }
    if (status) {
        sock->evstate[LCB_LUV_EV_WRITE].err =
                (uv_last_error(sock->parent->loop)).code;
    }

    yolog_err("Wrote all our data: status=%d", status);
    sock->write.pos = 0;
    sock->write.nb = 0;
    sock->evstate[LCB_LUV_EV_WRITE].flags |= LCB_LUV_EVf_PENDING;
    sock->evstate[LCB_LUV_EV_WRITE].flags &= ~(LCB_LUV_EVf_FLUSHING);
    if (sock->event->lcb_events & LIBCOUCHBASE_WRITE_EVENT) {
        sock->event->lcb_cb(sock->idx, LIBCOUCHBASE_WRITE_EVENT,
                sock->event->lcb_arg);
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

    if (sock->write.nb == 0) {
        yolog_warn("%d: Nothing to flush..", sock->idx);
        return;
    }

    if (sock->evstate[LCB_LUV_EV_WRITE].flags & LCB_LUV_EVf_FLUSHING) {
        yolog_warn("Not flushing because we are in the middle of a flush");
        return;
    }

    sock->write.buf.base = sock->write.data;
    sock->write.buf.len = sock->write.nb;

    status = uv_write(&sock->u_req.write,
                        (uv_stream_t*)&sock->tcp,
                        &sock->write.buf, 1, write_cb);
    lcb_luv_socket_ref(sock);

    yolog_warn("%d: Requested to flush %d bytes", sock->idx, sock->write.buf.len);

    if (status) {
        sock->evstate[LCB_LUV_EV_WRITE].err =
                (uv_last_error(sock->parent->loop)).code;
    }
    sock->evstate[LCB_LUV_EV_WRITE].flags |= LCB_LUV_EVf_FLUSHING;
}


static libcouchbase_ssize_t
write_common(lcb_luv_socket_t sock, const void *buf, size_t len, int *errno_out)
{
    libcouchbase_ssize_t ret;
    struct lcb_luv_evstate_st *evstate = sock->evstate + LCB_LUV_EV_WRITE;
    yolog_warn("%d: Requested to write %d bytes from %p", sock->idx, len, buf);

    if (evstate->err) {
        *errno_out = evstate->err;
        evstate->err = 0;
        return -1;
    }

    if (evstate->flags & LCB_LUV_EVf_FLUSHING) {
        *errno_out = EWOULDBLOCK;
        return -1;
    }

    ret = MINIMUM(len, sizeof(sock->write.data) - sock->write.nb);
    if (ret == 0) {
        *errno_out = EWOULDBLOCK;
        return -1;
    }

    memcpy(sock->write.data + sock->write.pos, buf, ret);
    sock->write.pos += ret;
    sock->write.nb += ret;
    yolog_warn("Returning successfuly (nb=%d)", sock->write.nb);
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
