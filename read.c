#include "lcb_luv_internal.h"
YOLOG_STATIC_INIT("read", YOLOG_DEBUG);

uv_buf_t
alloc_cb(uv_handle_t *handle, size_t suggested_size)
{
    lcb_luv_socket_t sock = (lcb_luv_socket_t)handle;
    yolog_debug("Alloc_cb called. len=%d", sock->read.buf.len);
    return sock->read.buf;
}

void
read_cb(uv_stream_t *stream, ssize_t nread, uv_buf_t buf)
{
    /* This is the same buffer structure we had before */
    lcb_luv_socket_t sock = (lcb_luv_socket_t)stream;
    yolog_debug("Read callback called: nread=%ld", nread);

    if (nread == -1) {
        uv_err_t last_err = uv_last_error(sock->parent->loop);
        if (last_err.sys_errno_ == UV_EOF) {
            sock->eof = 1;
        } else {
            sock->evstate[LCB_LUV_EV_READ].err = last_err.code;
        }
    } else if (nread) {
        sock->read.buf.len -= nread;
        sock->read.buf.base += (size_t)nread;
        sock->read.nb += nread;

        yolog_err("Have %d bytes remaining in read buffer", sock->read.buf.len);

        /* We don't have any more space */
        if (!sock->read.buf.len) {
            uv_read_stop(stream);
            yolog_debug("No more space. Stopping read.");
            sock->read.buf.base = sock->read.data;
            sock->read.buf.len = LCB_LUV_READAHEAD;
            sock->read.readhead_active = 0;
        }
    } else {
        /* nread == 0 */
        return;
    }

    sock->evstate[LCB_LUV_EV_READ].flags |= LCB_LUV_EVf_PENDING;
    if (sock->event->lcb_events & LIBCOUCHBASE_READ_EVENT) {
        sock->event->lcb_cb(sock->idx, LIBCOUCHBASE_READ_EVENT,
                sock->event->lcb_arg);
    }
}

void
lcb_luv_read_nudge(lcb_luv_socket_t sock)
{
    int status;
    if (sock->read.readhead_active) {
        yolog_debug("readahead is already active..");
        return; /* nothing to do here */
    }

    sock->read.buf.len = LCB_LUV_READAHEAD;
    sock->read.buf.base = sock->read.data;
    status = uv_read_start((uv_stream_t*)&sock->tcp, alloc_cb, read_cb);

    if (status) {
        sock->evstate[LCB_LUV_EV_READ].err =
                (uv_last_error(sock->parent->loop)).code;
        yolog_err("Couldn't start read: %d",
                  sock->evstate[LCB_LUV_EV_READ].err);
    } else {
        yolog_debug("Successfully enqueued a read..");
        sock->read.readhead_active = 1;
    }
}

static libcouchbase_ssize_t
read_common(lcb_luv_socket_t sock, void *buffer, libcouchbase_size_t len,
            int *errno_out)
{
    struct lcb_luv_evstate_st *evstate = sock->evstate + LCB_LUV_EV_READ;
    libcouchbase_ssize_t ret;
    size_t read_offset, toRead;

    yolog_debug("%p: Requested to read %d bytes into %p", sock, len, buffer);


    /* basic error checking */
    if (evstate->err) {
        *errno_out = evstate->err;
        evstate->err = 0;
        return -1;
    }

    if (sock->eof) {
        return 0;
    }

    /* Check how much data we can send back, and where do we read from */
    toRead = MINIMUM(len, sock->read.nb);
    read_offset = sock->read.pos;

    yolog_info("Will read %d, have %d, wanted %d", toRead, sock->read.nb, len);

    /* copy the data */
    if (toRead) {
        memcpy(buffer, sock->read.data + read_offset, toRead);
        ret = toRead;
        *errno_out = 0;
    } else {
        *errno_out = EWOULDBLOCK;
        ret = -1;
    }

    /**
     * Buffer positioning is somewhat complicated. If we are in middle of a partial
     * read (readahead is active), then the next bytes will still happen from within
     * the position of our current buffer, so we want to maintain our position.
     *
     * On the other hand, if readahead is not active, then the next read will begin
     * from the beginning of the buffer
     */
    sock->read.nb -= toRead;
    sock->read.pos += toRead;

    if (sock->read.nb == 0 && sock->read.readhead_active == 0) {
        sock->read.pos = 0;
    }


    if (toRead < len) {
        evstate->flags &= ~(LCB_LUV_EVf_PENDING);
        lcb_luv_read_nudge(sock);
    }

    return ret;
}

libcouchbase_ssize_t
lcb_luv_recv(struct libcouchbase_io_opt_st *iops,
             libcouchbase_socket_t sock_i,
             void *buffer,
             libcouchbase_size_t len,
             int flags)
{
    lcb_luv_socket_t sock = lcb_luv_sock_from_idx(iops, sock_i);
    if (sock == NULL) {
        iops->error = EBADF;
        return -1;
    }

    return read_common(sock, buffer, len, &iops->error);
}

libcouchbase_ssize_t
lcb_luv_recvv(struct libcouchbase_io_opt_st *iops,
              libcouchbase_socket_t sock_i,
              struct libcouchbase_iovec_st *iov,
              libcouchbase_size_t niov)
{
    libcouchbase_ssize_t nr = 0, iret = -1;
    int ii, my_errno;
    lcb_luv_socket_t sock = lcb_luv_sock_from_idx(iops, sock_i);

    if (sock == NULL) {
        iops->error = EBADF;
        return -1;
    }

    for (ii = 0; ii < niov; ii++) {
        if (iov[ii].iov_len == 0) {
            break;
        }
        iret = read_common(sock, iov[ii].iov_base, iov[ii].iov_len, &my_errno);
        if (iret > 0) {
            nr += iret;
        } else {
            break;
        }
    }
    if (!nr) {
        iops->error = my_errno;
        return -1;
    } else {
        return nr;
    }
}
