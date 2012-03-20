#include "lcb_luv_internal.h"

YOLOG_STATIC_INIT("common", YOLOG_DEBUG);

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
        yolog_debug("Invoking callback for %d", sock->idx);
        sock->event->lcb_cb(sock->idx, which, sock->event->lcb_arg);
        yolog_debug("Done invoking callback for %d", sock->idx);
        lcb_luv_flush(sock);
    }
}

static void
prepare_cb(uv_prepare_t *handle, int status)
{
    lcb_luv_socket_t sock = (lcb_luv_socket_t)handle->data;
    if (!sock) {
        fprintf(stderr, "We were called with prepare_t %p, with a missing socket\n",
                handle);
        return;
    }

    maybe_callout(sock);
}

void
lcb_luv_schedule_enable(lcb_luv_socket_t sock)
{
    if (sock->prep_active) {
        yolog_debug("prep is already active for %d", sock->idx);
        return;
    }

    yolog_debug("Will try and schedule prepare callback for %d", sock->idx);
    uv_prepare_start(&sock->prep, prepare_cb);
    sock->prep_active = 1;
}

void
lcb_luv_schedule_disable(lcb_luv_socket_t sock)
{
    yolog_warn("Prepare is being disabled for %d", sock->idx);
    if (sock->prep_active == 0) {
        return;
    }
    uv_prepare_stop(&sock->prep);
    sock->prep_active = 0;
}
