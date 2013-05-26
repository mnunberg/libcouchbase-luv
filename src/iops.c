#include "plugin-internal.h"

void lcbuv_decref_iops(lcb_io_opt_t iobase)
{
    my_iops_t *io = (my_iops_t*)iobase;
    assert(io->iops_refcount);
    if (--io->iops_refcount) {
        return;
    }

    memset(io, 0xff, sizeof(*io));
    free(io);
}

static void iops_lcb_dtor(lcb_io_opt_t iobase)
{
    my_iops_t *io = (my_iops_t*)iobase;

    unsigned int refcount = io->iops_refcount;
    lcbuv_decref_iops(iobase);
    if (refcount > 1 && io->external_loop == 0) {
        uv_run(io->loop, UV_RUN_ONCE);
    }
}


/******************************************************************************
 ******************************************************************************
 ** Event Loop Functions                                                     **
 ******************************************************************************
 ******************************************************************************/
static void run_event_loop(lcb_io_opt_t iobase)
{
    my_iops_t *io = (my_iops_t*)iobase;
//    lcbuv_incref_iops(io);

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
//    lcbuv_decref_iops(iobase);
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

    iop->v.v1.create_socket = lcbuv_create_socket;
    iop->v.v1.close_socket = lcbuv_close_socket;

    lcbuv_wire_timer_ops(iop);
    lcbuv_wire_rw_ops(iop);

    iop->v.v1.run_event_loop = run_event_loop;
    iop->v.v1.stop_event_loop = stop_event_loop;

    /* dtor */
    iop->destructor = iops_lcb_dtor;

    ret->iops_refcount = 1;

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
