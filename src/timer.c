#include "plugin-internal.h"

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
    lcbuv_incref_iops(io);
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
    lcbuv_decref_iops(&timer->parent->base);
    memset(timer, 0xff, sizeof(*timer));
    free(timer);
}

static void destroy_timer(lcb_io_opt_t io, void *timer_opaque)
{
    delete_timer(io, timer_opaque);
    uv_close((uv_handle_t *)timer_opaque, timer_close_cb);
}

void lcbuv_wire_timer_ops(lcb_io_opt_t iop)
{
    /**
     * v0 functions
     */
    iop->v.v1.create_timer = create_timer;
    iop->v.v1.update_timer = update_timer;
    iop->v.v1.delete_timer = delete_timer;
    iop->v.v1.destroy_timer = destroy_timer;
}
