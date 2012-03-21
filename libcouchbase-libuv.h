#ifndef LIBCOUCHBASE_LIBUV_H_
#define LIBCOUCHBASE_LIBUV_H_

#include <sys/types.h>
#include <libcouchbase/couchbase.h>
#include <uv.h>

/* The amount of data we should pre-read */
#define LCB_LUV_READAHEAD 0x2000
#define LCB_LUV_WRITEBUFSZ 0x2000

enum {
    LCB_LUV_EV_READ = 0,
    LCB_LUV_EV_WRITE = 1,

    LCB_LUV_EV_RDWR_MAX = 2,

    LCB_LUV_EV_CONNECT = 2,
    LCB_LUV_EV_MAX
};

typedef void (*lcb_luv_callback_t)(libcouchbase_socket_t,short,void*);
struct lcb_luv_socket_st;
typedef struct lcb_luv_socket_st* lcb_luv_socket_t;
struct lcb_luv_cookie_st;

typedef void (*lcb_luv_start_cb_t)(struct lcb_luv_cookie_st *luv_cookie);
typedef void (*lcb_luv_stop_cb_t)(struct lcb_luv_cookie_st *luv_cookie);

/**
 * Structure we place inside the iops. This is usually global per-event loop
 */
struct lcb_luv_cookie_st {

    /* private */
    uv_loop_t *loop;
    lcb_luv_socket_t *socktable;
    uint16_t fd_next;
    uint16_t fd_max;

    int do_stop;

    /* public */
    void *data;
    lcb_luv_start_cb_t start_callback;
    lcb_luv_stop_cb_t stop_callback;
};

/**
 * Structure representing an event
 */
struct lcb_luv_event_st {
    /* socket */
    lcb_luv_socket_t handle;

    /* libcouchbase function to be invoked */
    lcb_luv_callback_t lcb_cb;
    /* argument to pass to callback */
    void *lcb_arg;

    /* which events to monitor */
    short lcb_events;
};

typedef enum {
    LCB_LUV_EVf_CONNECTED = 1 << 0,
    LCB_LUV_EVf_ACTIVE = 1 << 1,

    LCB_LUV_EVf_PENDING = 1 << 2,
    LCB_LUV_EVf_FLUSHING = 1 << 3,
} lcb_luv_evstate_flags_t;




struct lcb_luv_evstate_st {
    lcb_luv_evstate_flags_t flags;
    /* Recorded errno */
    int err;
};

/**
 * Structure representing a TCP network connection.
 */
struct lcb_luv_socket_st {
    /* Should be first */
    uv_tcp_t tcp;

    /* Union for our requests */
    union uv_any_req u_req;

    /* Index into the 'fd' table */
    long idx;

    int eof;

    uv_prepare_t prep;
    int prep_active;

    unsigned long refcount;

    struct {
        /* Readahead buffer*/
        uv_buf_t buf;
        char data[LCB_LUV_READAHEAD];
        size_t pos;
        size_t nb;
        int readhead_active;
    } read;

    struct {
        uv_buf_t buf;
        /* how much data does our buffer have */
        char data[LCB_LUV_WRITEBUFSZ];
        size_t pos;
        size_t nb;
    } write;



    /* various information on different operations */
    struct lcb_luv_evstate_st evstate[LCB_LUV_EV_MAX];

    /* Pointer to libcouchbase opaque event, if any */
    struct lcb_luv_event_st *event;

    /* Pointer to our cookie */
    struct lcb_luv_cookie_st *parent;
};



struct libcouchbase_io_opt_st *
lcb_luv_create_io_opts(uv_loop_t *loop, uint16_t sock_max);
#define lcb_luv_from_iops(iops) \
    (struct lcb_luv_cookie_st *)(iops->cookie)

#endif /* LIBCOUCHBASE_LIBUV_H_ */
