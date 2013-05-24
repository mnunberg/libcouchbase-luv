#ifndef LCBUV_PLUGIN_INTERNAL_H
#define LCBUV_PLUGIN_INTERNAL_H

#include "plugin.h"

#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef void (*v0_callback_t)(lcb_socket_t,short,void*);

/**
 * Wrapper for lcb_sockdata_t
 */
typedef struct {
    lcb_sockdata_t base;

    /**
     * UV tcp handle. This is also a uv_stream_t.
     * ->data field contains the read callback
     */
    uv_tcp_t tcp;

    /** Reference count */
    unsigned int refcount;

    /** Current iov index in the read buffer */
    unsigned char cur_iov;

    /** Flag indicating whether uv_read_stop should be called on the next call */
    unsigned char read_done;

    /** Flag indicating whether uv_close has already been called  on the handle */
    unsigned char uv_close_called;

    unsigned char lcb_close_called;

    struct {
        int read;
        int write;
    } pending;

} my_sockdata_t;

typedef struct {
    lcb_io_writebuf_t base;

    /** Write handle.
     * ->data field contains the callback
     */
    uv_write_t write;

    /** Buffer structures corresponding to buf_info */
    uv_buf_t uvbuf[2];

    /** Parent socket structure */
    my_sockdata_t *sock;

} my_writebuf_t;


typedef struct {
    struct lcb_io_opt_st base;
    uv_loop_t *loop;

    /** Refcount. When this hits zero we free this */
    unsigned int iops_refcount;

    /** Whether using a user-initiated loop. In this case we don't wait/stop */
    int external_loop;
} my_iops_t;

typedef struct {
    uv_timer_t uvt;
    v0_callback_t callback;
    void *cb_arg;
    my_iops_t *parent;
} my_timer_t;

typedef struct {
    union {
        uv_connect_t conn;
        uv_idle_t idle;
    } uvreq;

    union {
        lcb_io_connect_cb conn;
        lcb_io_error_cb err;
        void *ptr;
    } cb;

    my_sockdata_t *socket;
} my_uvreq_t;

/******************************************************************************
 ******************************************************************************
 ** Common Utilities                                                         **
 ******************************************************************************
 ******************************************************************************/

my_uvreq_t *lcbuv_alloc_uvreq(my_sockdata_t *sock, void *callback);

void lcbuv_set_last_error(my_iops_t *io, int error);

void lcbuv_wire_timer_ops(lcb_io_opt_t iop);

void lcbuv_wire_rw_ops(lcb_io_opt_t iop);

/**
 * Generic callback for structures which don't need extensive object freeing.
 * This just calls 'free' on its argument :)
 */
void lcbuv_generic_close_cb(uv_handle_t *handle);

void lcbuv_free_bufinfo_common(struct lcb_buf_info *bi);

void lcbuv_decref_iops(lcb_io_opt_t iobase);

/******************************************************************************
 ******************************************************************************
 ** Socket Functions                                                         **
 ******************************************************************************
 ******************************************************************************/
lcb_sockdata_t *lcbuv_create_socket(lcb_io_opt_t iobase,
                                    int domain,
                                    int type,
                                    int protocol);

unsigned int lcbuv_close_socket(lcb_io_opt_t iobase, lcb_sockdata_t *sockbase);


void lcbuv_decref_sock(my_sockdata_t* sock);


/******************************************************************************
 ******************************************************************************
 ** Common Macros                                                            **
 ******************************************************************************
 ******************************************************************************/
#define PTR_FROM_FIELD(t, p, fld) ((t*)((char*)p-(offsetof(t, fld))))

#define dCOMMON_VARS(iobase, sockbase) \
    my_iops_t *io = (my_iops_t*)iobase; \
    my_sockdata_t *sock = (my_sockdata_t*)sockbase;


#define lcbuv_incref_sock(sd) (sd)->refcount++
#define lcbuv_incref_iops(io) (io)->iops_refcount++

#define SOCK_INCR_PENDING(s, fld) (s)->pending.fld++
#define SOCK_DECR_PENDING(s, fld) (s)->pending.fld--



#endif
