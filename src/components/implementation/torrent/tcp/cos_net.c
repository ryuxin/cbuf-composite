/**
 * Copyright 2008 by Boston University.  All rights reserved.
 *
 * Redistribution of this file is permitted under the GNU General
 * Public License v2.
 *
 * Initial author:  Gabriel Parmer, gabep1@cs.bu.edu, 2008
 */

/* 
 *  This is a little bit of a mess, but it is a mess with motivation:
 *  The data-structures employed include ring buffers of memory
 *  contained in pages (such that no span of MTU length crosses page
 *  boundries -- motivation being 1) so that the kernel can map and
 *  access this memory easily, 2) so that it can contain user-buffers
 *  also existing in user-components).  Thus exist the buff_page and
 *  rb_meta_t.  The thd_map maps between an upcall id and an
 *  associated ring buffer.  When an upcall is activated it can look
 *  up this mapping to find which ring buffer it should read from. A
 *  thd_map should really be simpler (not a struct, just a simple
 *  small array), but the refactoring to do this needs to be done in
 *  the future.  Next, we need a mechanism to keep track of specific
 *  connections, and how they are associated to threads so that we can
 *  provide (e.g. packet queues for received UDP data) and some sort
 *  of end-point for threads to block/wake on.  This is the struct
 *  intern_connection.  An opaque handle to these connections is
 *  net_connection_t.  There are mapping functions to convert between
 *  the two.  The packet queues in the intern_connections are
 *  implemented in an ugly, but easy and efficient way: the queue is
 *  implemented as a singly linked list, with a per-packet length, and
 *  pointer to the data. This struct packet_queue is actually not
 *  allocated separately, and exists in the ip_hdr region (after all
 *  packet processing is done).  Next there need to be exported
 *  functions for other components to use these mechanisms.  Herein
 *  lies the net_* interface.
 */
#define COS_FMT_PRINT 

#include <cos_component.h>
#include <cos_debug.h>
#include <cos_alloc.h>
#include <cos_list.h>
#include <cos_map.h>
#include <cos_synchronization.h>
#include <cos_net.h>
#include <cbuf.h>

#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/udp.h>
#include <lwip/tcp.h>
#include "lwip/priv/tcp_priv.h"
#include <lwip/stats.h>

#include <string.h>
#include <errno.h>

#define UDP_RCV_MAX (1<<15)
#define MTU 1500
#define MAX_SEND MTU

#include <sched.h>
#include <evt.h>
#include <net_portns.h>
#include <timed_blk.h>

cos_lock_t net_lock;

#define NET_LOCK_TAKE()    \
	do {								\
		if (lock_take(&net_lock)) prints("error taking net lock."); \
	} while(0)

#define NET_LOCK_RELEASE() \
	do {								\
		if (lock_release(&net_lock)) prints("error releasing net lock."); \
	} while (0)


/*********************** Component Interface ************************/

typedef enum {
	FREE,
	UDP,
	TCP,
	TCP_CLOSED
} conn_t;

typedef enum {
	ACTIVE,
	RECVING,
	CONNECTING,
	ACCEPTING,
	SENDING
} conn_thd_t;

//#define TEST_TIMING

#ifdef TEST_TIMING

typedef enum {
	RECV,
	APP_RECV,
	APP_PROC,
	SEND,
	UPCALL_PROC,
	TIMING_MAX
} timing_t;

struct timing_struct {
	char *name;
	unsigned int cnt;
	unsigned long long tot, max, min;
};

#define TIMING_STRUCT_INIT(str)					\
	{.name = str, .cnt = 0, .tot = 0, .max = 0, .min = ~0}

struct timing_struct timing_records[] = {
	TIMING_STRUCT_INIT("tcp_recv"),
	TIMING_STRUCT_INIT("tcp_app_recv"),
	TIMING_STRUCT_INIT("app_proc"),
	TIMING_STRUCT_INIT("tcp_send"),
	TIMING_STRUCT_INIT("upcall_proc")
};

static unsigned long long timing_timestamp(void) {
	unsigned long long ts;

	rdtscll(ts);
	return ts;
}
static unsigned long long timing_record(timing_t type, unsigned long long prev)
{
	unsigned long long curr, elapsed;
	struct timing_struct *ts = &timing_records[type];

	rdtscll(curr);
	elapsed = curr - prev;
	ts->cnt++;
	ts->tot += elapsed;
	if (elapsed > ts->max) ts->max = elapsed;
	if (elapsed < ts->min) ts->min = elapsed;
	
	return curr;
}

static void timing_output(void)
{
	int i;

	for (i = 0 ; i < TIMING_MAX ; i++) {
		struct timing_struct *ts = &timing_records[i];

		if (ts->cnt > 0) {
			printc("%s: @ %ld, avg %lld, max %lld, min %lld (cnt %d)", 
			       ts->name, sched_timestamp(), ts->tot/(unsigned long long)ts->cnt, 
			       ts->max, ts->min, ts->cnt);
			ts->cnt = 0;
			ts->tot = 0;
			ts->max = 0;
			ts->min = ~0;
		}
	}
}
#endif

struct intern_connection {
	u16_t tid;
	spdid_t spdid;
	conn_t conn_type;
	net_connection_t connection_id;
	conn_thd_t thd_status;
	long data;
	union {
		struct udp_pcb *up;
		struct tcp_pcb *tp;
	} conn;

	/* FIXME: should include information for each packet
	 * concerning the source ip and port as it can vary for
	 * UDP. */
	struct pbuf *incoming, *incoming_last;
	/* The size of the queue. */
	int incoming_size;

	/* Accept will create a connection.  A list of connections is
	 * stored here using the next pointer. */
	struct intern_connection *accepted_ic, *accepted_last;

	struct intern_connection *next;
#ifdef TEST_TIMING
	/* Time stamps */
	unsigned long long ts_start; 
#endif
};

COS_MAP_CREATE_STATIC(connections);
CVECT_CREATE_STATIC(cbuf_cnt);

static int net_conn_init(void)
{
	cos_map_init_static(&connections);
	cos_map_add(&connections, (void*)1);

	return 0;
}

/* Return the opaque connection value exported to other components for
 * a given internal_connection */
static inline net_connection_t net_conn_get_opaque(struct intern_connection *ic)
{
	return ic->connection_id;
}

/* Get the internal representation of a connection */
static inline struct intern_connection *net_conn_get_internal(net_connection_t nc)
{
	struct intern_connection *ic;

	ic = cos_map_lookup(&connections, nc);
	return ic;
}

static inline int net_conn_valid(net_connection_t nc)
{
	return 1;
}

static inline struct intern_connection *net_conn_alloc(conn_t conn_type, u16_t tid, long data)
{
	struct intern_connection *ic;
	net_connection_t nc;

	ic = malloc(sizeof(struct intern_connection));
	if (NULL == ic) return NULL;
	nc = cos_map_add(&connections, ic);
	if (-1 == nc) {
		free(ic);
		return NULL;
	}
	memset(ic, 0, sizeof(struct intern_connection));

	ic->connection_id = nc;
	ic->tid = tid;
	ic->thd_status = ACTIVE;
	ic->conn_type = conn_type;
	ic->data = data;

	return ic;
}

static inline void net_conn_free(struct intern_connection *ic)
{
	assert(ic);
	assert(0 == ic->incoming_size);

	cos_map_del(&connections, net_conn_get_opaque(ic));
	free(ic);

	return;
}

static void net_conn_free_packet_data(struct intern_connection *ic)
{
	struct pbuf *pq, *pq_next;

	/* both UDP and TCP need to free data in the packet queue */
	/* assert(ic->conn_type == TCP_CLOSED); */
	pq = ic->incoming;
	while (pq) {
		pq_next = pq->next;
		ic->incoming_size -= pq->len;
		pq->next = NULL;
		assert(pq->ref > 0);
		pbuf_free(pq);
		pq = pq_next;
	}
	ic->incoming = NULL;
	assert(ic->incoming_size == 0);
	/* FIXME: go through the accept queue closing those tcp
	 * connections too */
}

static int cos_packet_dequeue(struct intern_connection *ic, int *off, int *sz)
{
	cbuf_t id;
	struct pbuf *pq;
	pq = ic->incoming;
	assert(pq);
	ic->incoming = pq->next;
	if (ic->incoming_last == pq) {
		assert(NULL == ic->incoming);
		ic->incoming_last = NULL;
	}
	id                 = pq->cb_info.id;
	*off               = (unsigned int)pq->payload & (PAGE_SIZE-1);
	*sz                = pq->len;
	ic->incoming_size -= *sz;
	pq->next           = NULL;
	cbuf_send(id);
	pbuf_free(pq);

	return id;
}

static void cos_packet_enqueue(struct intern_connection *ic, struct pbuf *p)
{
	struct pbuf *q, *r;
	while (p) {
		assert(p->ref == 1);
		assert(p->len > 0);
		assert(p->type == PBUF_ROM || p->type == PBUF_REF);
		assert((NULL == ic->incoming) == (NULL == ic->incoming_last));
		q = p->next;
		r = pbuf_dechain(p);
		assert(q == r);
		assert(NULL != q || p->len == p->tot_len);
		/* Is the queue empty? */
		if (NULL == ic->incoming) ic->incoming =  p;
		else ic->incoming_last->next = p;
		ic->incoming_last = p;
		ic->incoming_size += p->len;
		p = q;
	}
}
/**** COS UDP function ****/

/* 
 * Receive packet from the lwip layer and place 
 * it in a buffer to the cos udp layer. */
static void cos_net_lwip_udp_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p,
				   const ip_addr_t *ip, u16_t port)
{
	struct intern_connection *ic;

	/* We should not receive a list of packets unless it is from
	 * this host to this host (then the headers will be another
	 * packet), but we aren't currently supporting this case. */
	assert(NULL != p);
	assert(NULL == p->next && p->tot_len == p->len);
	ic = (struct intern_connection*)arg;
	assert(UDP == ic->conn_type);

	/* Over our allocation??? */
	if (ic->incoming_size >= UDP_RCV_MAX) {
		assert(ic->thd_status != RECVING);
		assert(p->type == PBUF_ROM);
		//free(net_packet_pq(headers));
		assert(p->ref > 0);
		pbuf_free(p);

		return;
	}
	cos_packet_enqueue(ic, p);
	/* Just make sure lwip is doing what we think its doing */
	assert(p->ref == 1);

	if (-1 != ic->data && evt_trigger(cos_spd_id(), ic->data)) BUG();
	/* /\* If the thread blocked waiting for a packet, wake it up *\/ */
	/* if (RECVING == ic->thd_status) { */
	/* 	ic->thd_status = ACTIVE; */
	/* 	assert(ic->thd_status == ACTIVE); /\* Detect races *\/ */
	/* 	sched_wakeup(cos_spd_id(), ic->tid); */
	/* } */

	return;
}

/* 
 * FIXME: currently we associate a connection with a thread (and an
 * upcall thread), which is restrictive.
 */
net_connection_t net_create_udp_connection(spdid_t spdid, long evt_id)
{
	struct udp_pcb *up;
	struct intern_connection *ic;
	net_connection_t ret;

	up = udp_new();
	if (NULL == up) {
		prints("Could not allocate udp connection");
		ret = -ENOMEM;
		goto err;
	}
	ic = net_conn_alloc(UDP, cos_get_thd_id(), evt_id);
	if (NULL == ic) {
		prints("Could not allocate internal connection");
		ret = -ENOMEM;
		goto udp_err;
	}
	ic->spdid = spdid;
	ic->conn.up = up;
	udp_recv(up, cos_net_lwip_udp_recv, (void*)ic);
	return net_conn_get_opaque(ic);
udp_err:
	udp_remove(up);
err:
	return ret;
}

static int cos_net_udp_recv(struct intern_connection *ic, int *off, int *sz)
{
	cbuf_t id = 0;

	/* If there is data available, get it */
	if (ic->incoming_size > 0) {
		id = cos_packet_dequeue(ic, off, sz);
	}

	return id;
}

/**** COS TCP functions ****/

/* 
 * This should be called every time that a tcp connection is closed.
 */
static void cos_net_lwip_tcp_err(void *arg, err_t err)
{
	struct intern_connection *ic = (struct intern_connection *)arg;
	assert(ic);

	switch(err) {
	case ERR_ABRT:
	case ERR_RST:
		assert(ic->conn_type == TCP);
		assert(ic->conn_type != TCP_CLOSED);
		if (-1 != ic->data && evt_trigger(cos_spd_id(), ic->data)) BUG();
		ic->conn_type = TCP_CLOSED;
		ic->conn.tp = NULL;
		net_conn_free_packet_data(ic);
		break;
	default:
		printc("TCP error #%d: don't really have docs to know what this means.", err);
	}

	return;
}

static void __net_close(struct intern_connection *ic)
{
	switch (ic->conn_type) {
	case UDP:
	{
		struct udp_pcb *up;

		up = ic->conn.up;
		udp_remove(up);
		ic->conn.up = NULL;
		break;
	}
	case TCP:
	{
		struct tcp_pcb *tp;

//		prints("cosnet net_close: closing active TCP connection");
		tp = ic->conn.tp;
		assert(tp);
		tcp_abort(tp);
		assert(NULL == ic->conn.tp && ic->conn_type == TCP_CLOSED);
		break;
	}
	case TCP_CLOSED:
//		prints("cosnet net_close: finishing close of inactive TCP connection");
		assert(NULL == ic->conn.tp);
		break;
	default:
		BUG();
	}
	net_conn_free_packet_data(ic);
	net_conn_free(ic);
}

static err_t cos_net_lwip_tcp_recv(void *arg, struct tcp_pcb *tp, struct pbuf *p, err_t err)
{
	struct intern_connection *ic;
	
	ic = (struct intern_connection*)arg;
	assert(NULL != ic);
	assert(TCP == ic->conn_type);
	if (NULL == p) {
		assert(ic->conn.tp == tp);
		/* 
		 * This should call our registered error function
		 * above with ERR_ABRT, which will make progress
		 * towards closing the connection.
		 *
		 * Later, when the app calls some function in the API,
		 * TCP_CLOSED will be seen and the internal connection
		 * will be deallocated, and the application notified.
		 */
		tcp_abort(tp);
		assert(ic->conn_type == TCP_CLOSED && NULL == ic->conn.tp);
		return ERR_CLSD;
	}
	cos_packet_enqueue(ic, p);
	/* Just make sure lwip is doing what we think its doing */
	assert(p->ref == 1);

	if (-1 != ic->data && evt_trigger(cos_spd_id(), ic->data)) BUG();
/* 	/\* If the thread blocked waiting for a packet, wake it up *\/ */
/* 	if (RECVING == ic->thd_status) { */
/* 		ic->thd_status = ACTIVE; */
/* 		assert(ic->thd_status == ACTIVE); /\* Detect races *\/ */
/* 		if (sched_wakeup(cos_spd_id(), ic->tid)) BUG(); */
/* 	} */

	return ERR_OK;
}

static err_t cos_net_lwip_tcp_sent(void *arg, struct tcp_pcb *tp, u16_t len)
{
	struct intern_connection *ic = arg;
	assert(ic);

	/* I don't know why this is happening, but even when sending
	 * nothing, it says that we send 1 byte on accepts.  There is
	 * no ic->data associated with the connection yet, so we have
	 * a problem. */
	if (-1 == ic->data) return ERR_OK;
	/* FIXME: fix sending so that we can block and everything will work. */
	//if (evt_trigger(cos_spd_id(), ic->data)) BUG();

	return ERR_OK;
}

static err_t cos_net_lwip_tcp_connected(void *arg, struct tcp_pcb *tp, err_t err)
{
	struct intern_connection *ic = arg;
	assert(ic);
	
	assert(CONNECTING == ic->thd_status);
	if (sched_wakeup(cos_spd_id(), ic->tid)) BUG();
	ic->thd_status = ACTIVE;

	return ERR_OK;
}

static err_t cos_net_lwip_tcp_accept(void *arg, struct tcp_pcb *new_tp, err_t err);

/* FIXME: same as for the udp version of this function. */
static net_connection_t 
__net_create_tcp_connection(spdid_t spdid, u16_t tid, struct tcp_pcb *new_tp, long evt_id)
{
	struct tcp_pcb *tp;
	struct intern_connection *ic;
	net_connection_t ret;

	if (NULL == new_tp) {
		tp = tcp_new();	
		if (NULL == tp) {
			prints("Could not allocate tcp connection");
			ret = -ENOMEM;
			goto err;
		}
	} else {
		tp = new_tp;
	}
	ic = net_conn_alloc(TCP, tid, evt_id);
	if (NULL == ic) {
		prints("Could not allocate internal connection");
		ret = -ENOMEM;
		goto tcp_err;
	}
	ic->spdid = spdid;
	ic->conn.tp = tp;
	tcp_arg(tp, (void*)ic);
	tcp_err(tp, cos_net_lwip_tcp_err);
	tcp_recv(tp, cos_net_lwip_tcp_recv);
	tcp_sent(tp, cos_net_lwip_tcp_sent);
	tcp_accept(tp, cos_net_lwip_tcp_accept);

	assert(net_conn_get_internal(net_conn_get_opaque(ic)));

	return net_conn_get_opaque(ic);
tcp_err:
	tcp_abort(tp);
err:
	return ret;
}

net_connection_t net_create_tcp_connection(spdid_t spdid, u16_t tid, long evt_id)
{
	return __net_create_tcp_connection(spdid, tid, NULL, evt_id);
}

static err_t cos_net_lwip_tcp_accept(void *arg, struct tcp_pcb *new_tp, err_t err)
{
	struct intern_connection *ic = arg, *ica;
	net_connection_t nc;
	u16_t new_port;

	assert(ic);

	/* this is here to have the same properties as if we were
	 * calling the portmgr for each accept call.  Really, this
	 * call should be in the lwip stack. */
	new_port = portmgr_new(cos_spd_id());
	
	if (0 > (nc = __net_create_tcp_connection(ic->spdid, ic->tid, new_tp, -1))) BUG();

	ica = net_conn_get_internal(nc);
	if (NULL == ica) BUG();
	ic->next = NULL;
	if (NULL == ic->accepted_ic) {
		assert(NULL == ic->accepted_last);
		ic->accepted_ic = ica;
		ic->accepted_last = ica;
	} else {
		assert(NULL != ic->accepted_last);
		ic->accepted_last->next = ica;
		ic->accepted_last = ica;
	}
	assert(-1 != ic->data);
	if (evt_trigger(cos_spd_id(), ic->data)) BUG();

	return ERR_OK;
}

static int cos_net_tcp_recv(struct intern_connection *ic, int *off, int *sz)
{
	cbuf_t id = 0;
	struct tcp_pcb *tp;

	assert(ic->conn_type == TCP);
	/* If there is data available, get it */
	if (ic->incoming_size > 0) {
		id = cos_packet_dequeue(ic, off, sz);
		tp = ic->conn.tp;
		tcp_recved(tp, *sz);
	}

	return id;
}

void net_send_tcp_seg(struct tcp_pcb *tp, cbuf_t cbid, int off, int sz, int i)
{
	void *data = NULL;
	int ret, ocnt, ncnt=0;

	/* if (tcp_sndbuf(tp) < sz) { */
	/* 	assert(0); */
	/* 	return ; */
	/* } */
	data = cbuf2buf(cbid, sz)+off;
	/* if (!data) { */
	/* 	printc("tcp thd %d cbid %d i %d\n", cos_get_thd_id(), cbid, i); */
	/* 	data = cbuf2buf_p(cbid, sz)+off; */
	/* 	cbuf_debug_cbuf_info(9, 1, 1); */
	/* 	cbuf_debug_cbuf_info(7, 1, 1); */
	/* 	cbuf_debug_cbuf_info(21, 1, 1); */
	/* } */
	assert(data);
	if (ERR_OK != (ret = tcp_write_with_cbuf(tp, data, sz, 0, cbid, off, &ncnt))) {
		printc("tcp_write returned %d (sz %d, tcp_sndbuf %d, ERR_MEM: %d)",
		       ret, sz, tcp_sndbuf(tp), ERR_MEM);
		BUG();
	}
	if (ncnt > 1) {
		ocnt = (int)cvect_lookup(&cbuf_cnt, cbid);
		if (ocnt) __cvect_set(&cbuf_cnt, cbid, (void *)(ocnt+ncnt));
		else cvect_add(&cbuf_cnt, (void *)(ocnt+ncnt), cbid);
	}
}

/**** COS generic networking functions ****/

static struct intern_connection *net_verify_tcp_connection(net_connection_t nc, int *ret)
{
	struct intern_connection *ic;
	u16_t tid = cos_get_thd_id();

	if (!net_conn_valid(nc)) {
		*ret = -EINVAL;
		goto done;
	}
	ic = net_conn_get_internal(nc);
	if (NULL == ic) {
		*ret = -EINVAL;
		goto done;
	}
	if (tid != ic->tid) {
		*ret = -EPERM;
		goto done;
	}
	assert(ACTIVE == ic->thd_status);
	if (TCP != ic->conn_type) {
		*ret = -ENOTSUP;
		goto done;
	}
	/* socket has not been bound */
	if (0 == ic->conn.tp->local_port) {
		*ret = -1;
		goto done;
	}
	return ic;
done:
	return NULL;
}

net_connection_t net_accept(spdid_t spdid, net_connection_t nc)
{
	struct intern_connection *ic, *new_ic;
	net_connection_t ret = -1;

	//NET_LOCK_TAKE();
	ic = net_verify_tcp_connection(nc, &ret);
	if (NULL == ic) {
		ret = -EINVAL;
		goto done;
	}

	/* No accepts are pending on this connection?: block */
	if (NULL == ic->accepted_ic) {
		ret = -EAGAIN;
		goto done;
/* 		assert(ic->tid == cos_get_thd_id()); */
/* 		ic->thd_status = ACCEPTING; */
/* 		NET_LOCK_RELEASE(); */
/* 		prints("net_accept: blocking!"); */
/* 		if (sched_block(cos_spd_id(), 0) < 0) BUG(); */
/* 		NET_LOCK_TAKE(); */
/* 		assert(ACTIVE == ic->thd_status); */
	}

	assert(NULL != ic->accepted_ic && NULL != ic->accepted_last);
	new_ic = ic->accepted_ic;
	ic->accepted_ic = new_ic->next;
	if (NULL == ic->accepted_ic) ic->accepted_last = NULL;
	new_ic->next = NULL;
	assert(ic->conn.tp);
	tcp_accepted(ic->conn.tp);
	ret = net_conn_get_opaque(new_ic);
done:
	//NET_LOCK_RELEASE();
	return ret;
}

/* 
 * After a connection has been accepted, we need to associate it with
 * its "event" data, or the scalar to pass to the event component.
 * That is what this call does.
 */
int net_accept_data(spdid_t spdid, net_connection_t nc, long data)
{
	struct intern_connection *ic;
	int ret;

	//NET_LOCK_TAKE();
	ic = net_verify_tcp_connection(nc, &ret);
	if (NULL == ic || -1 != ic->data) goto err;
	ic->data = data;
	/* If data has already arrived, but couldn't trigger the event
	 * because ->data was not set, trigger the event now. */
	if (0 < ic->incoming_size && 
	    evt_trigger(cos_spd_id(), data)) goto err;

	//NET_LOCK_RELEASE();
	return 0;	
err:
	//NET_LOCK_RELEASE();
	return -1;
}

int net_listen(spdid_t spdid, net_connection_t nc, int queue)
{
	struct tcp_pcb *tp, *new_tp;
	struct intern_connection *ic;
	int ret = 0;
	spdid_t si;

	//NET_LOCK_TAKE();
	ic = net_verify_tcp_connection(nc, &ret);
	if (NULL == ic) {
		ret = -EINVAL;
		goto done;
	}
	tp = ic->conn.tp;
	si = ic->spdid;
	assert(NULL != tp);
	new_tp = tcp_listen_with_backlog(tp, queue);
	if (NULL == new_tp) {
		ret = -ENOMEM;
		goto done;
	}
	ic->conn.tp = new_tp;
	tcp_arg(new_tp, ic);
	tcp_accept(new_tp, cos_net_lwip_tcp_accept);
	//if (0 > __net_create_tcp_connection(si, new_tp)) BUG();
done:
	//NET_LOCK_RELEASE();
	return ret;
}

static int __net_bind(spdid_t spdid, net_connection_t nc, const ip_addr_t *ip, u16_t port)
{
	struct intern_connection *ic;
	u16_t tid = cos_get_thd_id();
	int ret = 0;

	//NET_LOCK_TAKE();
	if (!net_conn_valid(nc)) {
		ret = -EINVAL;
		goto done;
	}
	ic = net_conn_get_internal(nc);
	if (NULL == ic) {
		ret = -EINVAL;
		goto done;
	}
	if (tid != ic->tid) {
		ret = -EPERM;
		goto done;
	}
	assert(ACTIVE == ic->thd_status);

	if (portmgr_bind(cos_spd_id(), port)) {
		ret = -EADDRINUSE;
		goto done;
	}

	switch (ic->conn_type) {
	case UDP:
	{
		struct udp_pcb *up;

		up = ic->conn.up;
		assert(up);
		if (ERR_OK != udp_bind(up, ip, port)) {
			ret = -EPERM;
			goto done;
		}
		break;
	}
	case TCP:
	{
		struct tcp_pcb *tp;
		
		tp = ic->conn.tp;
		assert(tp);
		if (ERR_OK != tcp_bind(tp, ip, port)) {
			ret = -ENOMEM;
			goto done;
		}
		break;
	}
	case TCP_CLOSED:
//		__net_close(ic);
		ret = -EPIPE;
		break;
	default:
		BUG();
	}

done:
	//NET_LOCK_RELEASE();
	return ret;
}

int net_bind(spdid_t spdid, net_connection_t nc, u32_t ip, u16_t port)
{
	struct ip4_addr ipa = *(struct ip4_addr*)&ip;
	return __net_bind(spdid, nc, &ipa, port);
}

static int __net_connect(spdid_t spdid, net_connection_t nc, const ip_addr_t *ip, u16_t port)
{
	struct intern_connection *ic;
	u16_t tid = cos_get_thd_id();	

	if (!net_conn_valid(nc)) goto perm_err;
	ic = net_conn_get_internal(nc);
	if (NULL == ic) goto perm_err;
	if (tid != ic->tid) goto perm_err;
	assert(ACTIVE == ic->thd_status);

	switch (ic->conn_type) {
	case UDP:
	{
		struct udp_pcb *up;

		up = ic->conn.up;
		if (ERR_OK != udp_connect(up, ip, port)) {
			return -EISCONN;
		}
		break;
	}
	case TCP:
	{
		struct tcp_pcb *tp;

		tp = ic->conn.tp;
		ic->thd_status = CONNECTING;
		if (ERR_OK != tcp_connect(tp, ip, port, cos_net_lwip_tcp_connected)) {
			ic->thd_status = ACTIVE;
			return -ENOMEM;
		}
		if (sched_block(cos_spd_id(), 0) < 0) BUG();
		assert(ACTIVE == ic->thd_status);
		/* When we wake up, we should be connected. */
		return 0;
	}
	case TCP_CLOSED:
//		__net_close(ic);
		return -EPIPE;
	default:
		BUG();
	}
	return 0;
perm_err:
	return -EPERM;
}

int net_connect(spdid_t spdid, net_connection_t nc, u32_t ip, u16_t port)
{
	struct ip4_addr ipa;
	ipa.addr = ip;
//	assert(0); 		/* I'm sure this doesn't work... */
	//printc("ip addr %x\n", ipa.addr);
	return __net_connect(spdid, nc, &ipa, port);
}

int net_close(spdid_t spdid, net_connection_t nc)
{
	struct intern_connection *ic;
	u16_t tid = cos_get_thd_id();

	if (!net_conn_valid(nc)) goto perm_err;
	ic = net_conn_get_internal(nc);
	if (NULL == ic) goto perm_err; /* should really be EINVAL */
	if (tid != ic->tid) goto perm_err;
	assert(ACTIVE == ic->thd_status);

	/* This should be called from within lwip, not here, but this
	 * is here to have comparable performance characteristics as
	 * if it were in lwip */
	portmgr_free(cos_spd_id(), /* u16_t port_num */ 0);

	__net_close(ic);
	return 0;
perm_err:
	return -EPERM;
}

int net_recv(spdid_t spdid, net_connection_t nc, int *off, int *sz)
{
//	struct udp_pcb *up;
	struct intern_connection *ic;
	u16_t tid = cos_get_thd_id();
	cbuf_t id;

//	if (!cos_argreg_buff_intern(data, sz)) return -EFAULT;
	if (!net_conn_valid(nc)) return -EINVAL;

//	NET_LOCK_TAKE();
	ic = net_conn_get_internal(nc);
	if (NULL == ic) {
		//NET_LOCK_RELEASE();
		return -EINVAL;
	}
	if (tid != ic->tid) {
		//NET_LOCK_RELEASE();
		return -EPERM;
	}

	switch (ic->conn_type) {
	case UDP:
		id = cos_net_udp_recv(ic, off, sz);
		break;
	case TCP:
		id = cos_net_tcp_recv(ic, off, sz);
		break;
	case TCP_CLOSED:
//		__net_close(ic);
		id = -EPIPE;
		break;
	default:
		printc("net_recv: invalid connection type: %d", ic->conn_type);
		BUG();
	}
	//NET_LOCK_RELEASE();
	return id;
}

int net_send(spdid_t spdid, net_connection_t nc, cbuf_t cbid, int sz)
{
	struct intern_connection *ic;
	u16_t tid = cos_get_thd_id();
	int ret = sz, i;
	void *data;

	if (!net_conn_valid(nc)) return -EINVAL;

//	NET_LOCK_TAKE();
	ic = net_conn_get_internal(nc);
	if (NULL == ic) {
		ret = -EINVAL;
		goto err;
	}
	if (tid != ic->tid) {
		ret = -EPERM;
		goto err;
	}

	switch (ic->conn_type) {
	case UDP:
	{
		struct udp_pcb *up;
		struct pbuf *p;

		assert((int)cbid > 0);
		data = cbuf2buf(cbid, sz);
		/* There's no blocking in the UDP case, so this is simple */
		up = ic->conn.up;
		p = pbuf_alloc(PBUF_TRANSPORT, sz, PBUF_ROM);
		if (NULL == p) {
			ret = -ENOMEM;
			goto err;
		}
		p->payload        = data;
		p->cb_info.id     = cbid;
		p->cb_info.offset = 0;
		p->cb_info.len    = sz;

		if (ERR_OK != udp_send(up, p)) {
			printc("udp send fail %d\n", udp_send(up, p));
			pbuf_free(p);
			/* IP/port must not be set */
			ret = -ENOTCONN;
			goto err;
		}
		pbuf_free(p);
		break;
	}
	case TCP:
	{
		struct cbuf_agg *agg;
		struct tcp_pcb *tp;

		tp = ic->conn.tp;
		if((int)cbid < 0) {
			cbid *= -1;
			agg = (struct cbuf_agg *)cbuf2buf(cbid, sz);
			if (!agg) {
				agg = (struct cbuf_agg *)cbuf2buf_p(cbid, sz);
			}
			for(i=0; i<agg->ncbufs; i++) {
				net_send_tcp_seg(tp, agg->elem[i].id, agg->elem[i].offset, agg->elem[i].len, i);
			}
			cbuf_free(cbid);
		} else {
			net_send_tcp_seg(tp, cbid, 0, sz, -1);
		}
		/* No implementation of nagle's algorithm yet.  Send
		 * out the packet immediately if possible. */
		if (ERR_OK != tcp_output(tp)) {
			printc("tcp_output returned %d, ERR_MEM: %d", ret, ERR_MEM);
			BUG();
		}
		break;
	}
	case TCP_CLOSED:
		ret = -EPIPE;
		break;
	default:
		BUG();
	}
err:
//	NET_LOCK_RELEASE();
	return ret;
}

/************************ LWIP integration: **************************/

struct ip4_addr ip, mask, gw;
struct netif   cos_if;

static void cos_net_interrupt(cbuf_t cb, int sz, int off)
{
	void *d;
	int len;
	struct pbuf *p;
	struct ip_hdr *ih;
#ifdef TEST_TIMING
	unsigned long long ts;
#endif
//	printc(">>> %d\n", net_lock.lock_id);
	NET_LOCK_TAKE();
	char *packet = (char *)cbuf2buf(cb, sz)+off;
	/* printc("<<< %d\n", net_lock.lock_id); */

	assert(packet);
	ih = (struct ip_hdr*)packet;
	if (unlikely(4 != IPH_V(ih))) goto done;
	len = ntohs(IPH_LEN(ih));
	if (unlikely(len != sz || len > MTU)) {
		printc("len %d != %d or > %d", len, sz, MTU);
		cbuf_free(cb);
		goto done;
	}

	p = pbuf_alloc(PBUF_IP, len, PBUF_ROM);
	/* printc("ip recv len %d\n", len); */
	if (unlikely(!p)) {
		prints("OOM in interrupt: allocation of pbuf failed.\n");
		cbuf_free(cb);
		goto done;
	}
	p->payload        = packet;
	p->cb_info.id     = cb;
	p->cb_info.offset = off;
	p->cb_info.len    = sz;
	/* hand off packet ownership here... */
	if (ERR_OK != cos_if.input(p, &cos_if)) {
		prints("net: failure in IP input.");
		pbuf_free(p);
		goto done;
	}

#ifdef TEST_TIMING
	timing_record(UPCALL_PROC, ts);
#endif
done:
	NET_LOCK_RELEASE();
	/* printc("cosnet %d release\n", cos_get_thd_id()); */
	return;
}

#include <stdio.h>
#include <torrent.h>
#include <torlib.h>

extern td_t parent_tsplit(spdid_t spdid, td_t tid, char *param, int len, tor_flags_t tflags, long evtid);
extern int parent_twrite(spdid_t spdid, td_t td, int cbid, int sz);
extern int parent_tread(spdid_t spdid, td_t td, int cbid, int sz);
extern int parent_treadp(spdid_t spdid, td_t td, int *off, int *sz);

static volatile int event_thd = 0;
static td_t ip_td = 0;

static int cos_net_evt_loop(void)
{
	int sz, off;
	cbuf_t cb;

	assert(event_thd > 0);
	ip_td = parent_tsplit(cos_spd_id(), td_root, "", 0, TOR_ALL, -1);
	assert(ip_td > 0);
	printc("network uc %d starting...\n", cos_get_thd_id());
	printc("tcp netif thd %d tcp lock %d\n", cos_get_thd_id(), net_lock.lock_id);
	while (1) {
		cb   = parent_treadp(cos_spd_id(), ip_td, &off, &sz);
		cos_net_interrupt(cb, sz, off);
		assert(lock_contested(&net_lock) != cos_get_thd_id());
	}

	return 0;
}

static err_t cos_net_stack_link_send(struct netif *ni, struct pbuf *p)
{
	/* We don't do arp...this shouldn't be called */
	BUG();
	return ERR_OK;
}

static err_t cos_net_stack_send(struct netif *ni, struct pbuf *p, const ip4_addr_t *ip)
{
	int sz, cnt = 0;
	struct cbuf_agg *agg;
	cbuf_t cb;
	int fat = 0;

	/* assuming the net lock is taken here */

	assert(p && p->ref == 1);
	assert(p->type == PBUF_RAM);
	agg = (struct cbuf_agg *)cbuf_alloc(MTU, &cb);
	assert(agg);
	/* do not free packet cbuf here. UDP packet will be freed by net_send
	 * TCP packet will be freed after ACK */
	while (p) {
		agg->elem[cnt].id = p->cb_info.id;
		agg->elem[cnt].offset = p->cb_info.offset;
		agg->elem[cnt].len = p->len;
		cbuf_send(p->cb_info.id);
		/* struct cbuf_meta *cm; */
		/* cm = cbuf_vect_lookup_addr(agg->elem[cnt].id); */
		/* if (cm->snd_rcv.nsent>240 || agg->elem[cnt].id == 3508 || agg->elem[cnt].id == 3509) { */
		/* 	/\* fat = 1; *\/ */
		/* 	struct tcp_hdr *tcphdr = (char *)p->payload+sizeof(struct ip_hdr); */
		/* 	printc("tcp thd %d cb %d len %d off %d dest %d src %d\n", cos_get_thd_id(), agg->elem[cnt].id, p->len, p->cb_info.offset, ntohs(tcphdr->dest), ntohs(tcphdr->src)); */
		/* } */
		cnt++;

		assert(p->type != PBUF_POOL);
		p = p->next;
	}
	agg->ncbufs = cnt;
	cbuf_send_free(cb);
	/* NET_LOCK_RELEASE(); */
	sz = parent_twrite(cos_spd_id(), ip_td, cb, MTU);
	/* NET_LOCK_TAKE(); */
	if (sz <= 0) {
		printc("<<transmit returns %d\n", sz);
	}
	assert(sz > 0);
	
	/* cannot deallocate packets here as we might need to
	 * retransmit them. */
	return ERR_OK;
}

/* 
 * Called when pbuf_free is invoked on a pbuf that was allocated with
 * PBUF_{ROM|REF}.  Free the cbuf which holds the packet payload.
 */
static void lwip_free_payload(struct pbuf *p)
{
	int ocnt;
	assert(p);
	if (NULL == p->payload) return;
	/* FIXME: if a large cbuf is split into multiple tcp segments, when 
	 * each segment is freed, this will be called. So we have double free*/
	ocnt = (int)cvect_lookup(&cbuf_cnt, p->cb_info.id);
	if (ocnt) {
		ocnt--;
		if (!ocnt) cvect_del(&cbuf_cnt, p->cb_info.id);
		else __cvect_set(&cbuf_cnt, p->cb_info.id, (void *)ocnt);
	}
	if (!ocnt) cbuf_free(p->cb_info.id);
}

/*** Torrent functions ***/

static int 
modify_connection(spdid_t spdid, net_connection_t nc, char *ops, int len)
{
	struct intern_connection *ic;
	char *prop;
	int ret = -EINVAL;

	prop = strstr(ops, "bind");
	if (prop) {
		u32_t ip;
		u32_t port;
		int r;

		NET_LOCK_TAKE();
		ic = net_conn_get_internal(nc);
		//ic = net_verify_tcp_connection(nc, &ret);
		if (NULL == ic) goto release;

		r = sscanf(prop, "bind:%x:%d", &ip, &port);
		printc("bind ip %d port %d r %d\n", ip, port, r);
		if (r != 2) goto release;

		port &= 0xFFFF;
		ret = net_bind(spdid, nc, ip, port);
		NET_LOCK_RELEASE();
	}
	prop = strstr(ops, "listen");
	if (prop) {
		int r;
		unsigned int q;

		NET_LOCK_TAKE();
		ic = net_conn_get_internal(nc);
		//ic = net_verify_tcp_connection(nc, &ret);
		if (NULL == ic) goto release;

		r = sscanf(prop, "listen:%d", &q);
		if (r != 1) goto release;
		ret = net_listen(spdid, nc, q);
		NET_LOCK_RELEASE();
	}
	prop = strstr(ops, "connect");
	if (prop) {
		int r;
		u32_t ip;
		u32_t port;

		NET_LOCK_TAKE();
		ic = net_conn_get_internal(nc);
		//ic = net_verify_tcp_connection(nc, &ret);
		if (NULL == ic) goto release;

		r = sscanf(prop, "connect:%x:%d", &ip, &port);
		printc("connect ip %u port %d r %d\n", ip, port, r);
		if (r != 2) goto release;
		ret = net_connect(spdid, nc, ip, port);
		NET_LOCK_RELEASE();
	}

done:
	return ret;
release:
	NET_LOCK_RELEASE();
	goto done;
}

td_t
tsplit(spdid_t spdid, td_t tid, char *param, int len,
       tor_flags_t tflags, long evtid)
{
	td_t ret = -EINVAL;
	struct torrent *t;
	net_connection_t nc = 0;
	int accept = 0;

	if (tor_isnull(tid)) return -EINVAL;

	NET_LOCK_TAKE();
	/* creating a new connection */
	if (tid == td_root || len == 0 || strstr(param, "accept")) {
		if (tid == td_root) { 	/* new connection */
			printc("tcp split para %s\n", param);
			if (strstr(param, "udp")) nc = net_create_udp_connection(spdid, evtid);
			else nc = net_create_tcp_connection(spdid, cos_get_thd_id(), evtid);
			if (nc <= 0) ERR_THROW(-ENOMEM, done);
		} else { /* len == 0 || strstr(param, "accept"), accept on connection */
			t = tor_lookup(tid);
			if (!t) goto done;
			nc = net_accept(spdid, (net_connection_t)t->data);
			if (nc == -EAGAIN) ERR_THROW(-EAGAIN, done);
			if (nc < 0) ERR_THROW(-EINVAL, done);
			if (0 < net_accept_data(spdid, nc, evtid)) BUG();
			accept = 1;
		}
		t = tor_alloc((void*)nc, tflags);
		if (!t) ERR_THROW(-ENOMEM, free);
		ret = t->td;
	} else { 		/* modifying an existing connection */
		t = tor_lookup(tid);
		if (!t) goto done;
		nc = (net_connection_t)t->data;
	}
	if (!accept && len != 0) {
		int r;

		NET_LOCK_RELEASE();
		r = modify_connection(spdid, nc, param, len);
		if (r < 0) ret = r;
		NET_LOCK_TAKE();
	}
done:
	NET_LOCK_RELEASE();
	assert(lock_contested(&net_lock) != cos_get_thd_id());
	return ret;
free:
	net_close(spdid, nc);
	goto done;
}

void
trelease(spdid_t spdid, td_t td)
{
	struct torrent *t;
	net_connection_t nc;

	if (!tor_is_usrdef(td)) return;

	NET_LOCK_TAKE();
	t = tor_lookup(td);
	if (!t) goto done;
	nc = (net_connection_t)t->data;
	if (nc) net_close(spdid, nc);
	tor_free(t);
done:
	NET_LOCK_RELEASE();
	assert(lock_contested(&net_lock) != cos_get_thd_id());
	return;
}

int
tmerge(spdid_t spdid, td_t td, td_t td_into, char *param, int len)
{
	int ret = 0;

	/* currently only allow deletion */
	if (td_into != td_null) ERR_THROW(-EINVAL, done);
	trelease(spdid, td);
done:
	assert(lock_contested(&net_lock) != cos_get_thd_id());
	return ret;
}

int
twrite(spdid_t spdid, td_t td, int cbid, int sz)
{
	net_connection_t nc;
	struct torrent *t;
	int ret = -1;
	
	if (tor_isnull(td)) return -EINVAL;

	NET_LOCK_TAKE();
	t = tor_lookup(td);
	if (!t) ERR_THROW(-EINVAL, done);
	if (!(t->flags & TOR_WRITE)) ERR_THROW(-EACCES, done);

	assert(t->data);
	nc = (net_connection_t)t->data;
	ret = net_send(spdid, nc, cbid, sz);
done:
	NET_LOCK_RELEASE();
	assert(lock_contested(&net_lock) != cos_get_thd_id());
	return ret;
}

int 
tread(spdid_t spdid, td_t td, int cbid, int sz)
{
	return -ENOTSUP;
}

int 
treadp(spdid_t spdid, td_t td, int *off, int *sz)
{
	net_connection_t nc;
	struct torrent *t;
	int ret;
	
	if (tor_isnull(td)) {printc("tread err 1\n"); return -EINVAL;}

	NET_LOCK_TAKE();
	t = tor_lookup(td);
	if (!t) {printc("tread err 2\n"); ERR_THROW(-EINVAL, done);}
	if (!(t->flags & TOR_READ)) {printc("tread err 3\n"); ERR_THROW(-EACCES, done);}

	assert(t->data);
	nc = (net_connection_t)t->data;
	
	ret = net_recv(spdid, nc, off, sz);
done:
	NET_LOCK_RELEASE();
	assert(lock_contested(&net_lock) != cos_get_thd_id());
	return ret;
}

/*** Initialization routines: ***/

static err_t cos_if_init(struct netif *ni)
{
	ni->name[0]    = 'c';
	ni->name[1]    = 'n';
	ni->mtu        = 1500;
	ni->output     = cos_net_stack_send;
	ni->linkoutput = cos_net_stack_link_send;

	return ERR_OK;
}

static void init_lwip(void)
{
	lwip_init();
	tcp_mem_free(lwip_free_payload);

	/* setting the IP address */
	IP4_ADDR(&ip, 10,0,2,8);
	IP4_ADDR(&gw, 10,0,1,1);
	/* IP4_ADDR(&ip, 192,168,1,128); */
	/* IP4_ADDR(&gw, 192,168,1,1); */
	IP4_ADDR(&mask, 255,255,255,0);
	
	netif_add(&cos_if, &ip, &mask, &gw, NULL, cos_if_init, ip_input);
	netif_set_default(&cos_if);
	netif_set_up(&cos_if);
	netif_set_link_up(&cos_if);
}

static void cos_net_create_netif_thd(void)
{
	union sched_param sp;
	
	sp.c.type  = SCHEDP_PRIO;
	sp.c.value = 4;

	event_thd = cos_thd_create(cos_net_evt_loop, NULL, sp.v, 0, 0);
	if (event_thd <= 0) BUG();
}

static void cos_net_time_loop(void)
{
	int cnt = 0;
#ifdef LWIP_STATS
	int stats_cnt = 0;
#endif

	printc("cosnet tcp timer %d\n", cos_get_thd_id());
	/* Start the tcp timer */
	while (1) {
		/* Sleep for a quarter of seconds as prescribed by lwip */
		NET_LOCK_TAKE();

		if (++cnt == 4) {
#ifdef TEST_TIMING
			timing_output();
#endif
		}
#ifdef LWIP_STATS
		if (++stats_cnt == 20) {
			stats_cnt = 0;
			stats_display();
		}
#endif
		tcp_tmr();
		NET_LOCK_RELEASE();
		timed_event_block(cos_spd_id(), 25); /* expressed in ticks currently */
		/* cos_mpd_update(); */
	}

	prints("net: Error -- returning from init!!!");
	BUG();
}

static void cos_net_create_timer_thd(void)
{
	union sched_param sp;
	int time_thd;
	
	sp.c.type  = SCHEDP_PRIO;
	sp.c.value = 6;

	time_thd = cos_thd_create(cos_net_time_loop, NULL, sp.v, 0, 0);
	if (time_thd <= 0) BUG();
}

static int init(void) 
{
	void *vaddr[4];
	int i;
	for (i=0; i<3; i++) {
		vaddr[i] = valloc_alloc(cos_spd_id(), cos_spd_id(), 80*256);
		assert(vaddr[i]);
	}
	for (i=0; i<3; i++) {
		valloc_free(cos_spd_id(), cos_spd_id(), vaddr[i], 80*256);
	}

	lock_static_init(&net_lock);
	NET_LOCK_TAKE();

	torlib_init();
	net_conn_init();
	init_lwip();
	cos_net_create_netif_thd();
	cos_net_create_timer_thd();

	NET_LOCK_RELEASE();
	return 0;
}

void cos_init(void *arg)
{
	static volatile int first = 1;

	if (first) {
		first = 0;
		init();
		/* BUG(); */
	} else {
		prints("net: not expecting more than one bootstrap.");
	}
}
