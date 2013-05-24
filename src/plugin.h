#ifndef LCB_PLUGIN_UV_H
#define LCB_PLUGIN_UV_H
#ifdef __cplusplus
extern "C" {
#endif

#include <libcouchbase/couchbase.h>
#include <uv.h>


/**
 * Use this if using an existing uv_loop_t
 * @param io a pointer to an io pointer. Will be populated on success
 * @param loop a uv_loop_t. You may use 'NULL', in which case the default
 * loop is used and the plugin takes ownership of it.
 *
 * If your own loop is passed, the run_event_loop and stop_event_loop functions
 * are no-ops
 */
LIBCOUCHBASE_API
lcb_error_t lcbuv_new_iops(lcb_io_opt_t *io, uv_loop_t *loop);

LIBCOUCHBASE_API
lcb_error_t lcb_create_libuv_io_opts(int version, lcb_io_opt_t *io, void *arg);

#ifdef __cplusplus
}
#endif
#endif
