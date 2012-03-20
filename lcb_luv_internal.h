#ifndef LCB_LUV_INTERNAL_H_
#define LCB_LUV_INTERNAL_H_

#include "libcouchbase-libuv.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "yolog.h"

#define EVSTATE_IS(p, b) \
    ( ( (p)->flags ) & LCB_LUV_EVf_ ## b)

#define IOPS_COOKIE(iops) \
    ((struct lcb_luv_cookie_st*)(iops->cookie))

#define MINIMUM(a,b) \
    ((a < b) ? a : b)

void *
lcb_luv_create_event(struct libcouchbase_io_opt_st *iops);

int
lcb_luv_update_event(struct libcouchbase_io_opt_st *iops,
                     libcouchbase_socket_t sock_i,
                     void *event_opaque,
                     short flags,
                     void *cb_data,
                     lcb_luv_callback_t cb);

void
lcb_luv_delete_event(struct libcouchbase_io_opt_st *iops,
                     libcouchbase_socket_t sock_i,
                     void *event_opaque);

void
lcb_luv_destroy_event(struct libcouchbase_io_opt_st *iops,
                      void *event_opaque);

libcouchbase_socket_t
lcb_luv_socket(struct libcouchbase_io_opt_st *iops,
               int domain, int type, int protocol);

int
lcb_luv_connect(struct libcouchbase_io_opt_st *iops,
                libcouchbase_socket_t sock_i,
                const struct sockaddr *saddr,
                unsigned int saddr_len);


void
lcb_luv_close(struct libcouchbase_io_opt_st *iops, libcouchbase_socket_t sock_i);


/* READ */
libcouchbase_ssize_t
lcb_luv_recv(struct libcouchbase_io_opt_st *iops,
             libcouchbase_socket_t sock_i,
             void *buffer,
             libcouchbase_size_t len,
             int flags);
libcouchbase_ssize_t
lcb_luv_recvv(struct libcouchbase_io_opt_st *iops,
              libcouchbase_socket_t sock_i,
              struct libcouchbase_iovec_st *iov,
              libcouchbase_size_t niov);


/* WRITE */
libcouchbase_ssize_t
lcb_luv_send(struct libcouchbase_io_opt_st *iops,
             libcouchbase_socket_t sock_i,
             const void *msg,
             libcouchbase_size_t len,
             int flags);

libcouchbase_ssize_t
lcb_luv_sendv(struct libcouchbase_io_opt_st *iops,
              libcouchbase_socket_t sock_i,
              struct libcouchbase_iovec_st *iov,
              libcouchbase_size_t niov);


/* TIMER */
void *
lcb_luv_create_timer(struct libcouchbase_io_opt_st *iops);

int
lcb_luv_update_timer(struct libcouchbase_io_opt_st *iops,
                     void *timer_opaque,
                     libcouchbase_uint32_t usecs,
                     void *cbdata,
                     lcb_luv_callback_t callback);

void
lcb_luv_delete_timer(struct libcouchbase_io_opt_st *iops,
                     void *timer_opaque);

void
lcb_luv_destroy_timer(struct libcouchbase_io_opt_st *iops,
                      void *timer_opaque);

/**
 * These are functions private to lcb-luv. They are not iops functions
 */


/**
 * This will get a socket structure from an index
 */
lcb_luv_socket_t
lcb_luv_sock_from_idx(struct libcouchbase_io_opt_st *iops, libcouchbase_socket_t idx);

/**
 * Will try and let us get read events on this socket
 */
void
lcb_luv_read_nudge(lcb_luv_socket_t sock);

/**
 * Will enable our per-iteration callback, unless already enabled
 */
void
lcb_luv_schedule_enable(lcb_luv_socket_t sock);

/**
 * Will flush data to libuv. This is called when the event loop has control
 */
void
lcb_luv_flush(lcb_luv_socket_t sock);

#endif /* LCB_LUV_INTERNAL_H_ */
