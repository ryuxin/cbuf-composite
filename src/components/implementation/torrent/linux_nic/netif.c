/**
 * Copyright 2009 by Boston University.  All rights reserved.
 * Copyright 2012 by The George Washington University.  All rights reserved.
 *
 * Redistribution of this file is permitted under the GNU General
 * Public License v2.
 *
 * Author: Gabriel Parmer, gabep1@cs.bu.edu, 2009
 *         added torrent interface: Gabriel Parmer, gparmer@gwu.edu, 2012
 */

//#define UPCALL_TIMING 1

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

#include <string.h>
#include <errno.h>

#include <torrent.h>
#include <torlib.h>
#include <cbuf.h>
#include <lwip/tcp.h>
#include "lwip/priv/tcp_priv.h"

#define NUM_WILDCARD_BUFFS 256 //64 //32
#define UDP_RCV_MAX (1<<15)
/* 
 * We need page-aligned data for the network ring buffers.  This
 * structure lies at the beginning of a page and describes the data in
 * it.  When amnt_buffs = 0, we can dealloc the page.
 */
#define NP_NUM_BUFFS 2
#define MTU 1500
#define MAX_SEND MTU
#define BUFF_ALIGN_VALUE 8
#define BUFF_ALIGN(sz)  ((sz + BUFF_ALIGN_VALUE) & ~(BUFF_ALIGN_VALUE-1))
struct buff_page {
	int amnt_buffs;
	struct buff_page *next, *prev;
	void *buffs[NP_NUM_BUFFS];
	short int buff_len[NP_NUM_BUFFS];
	/* FIXME: should be a bit-field */
	char buff_used[NP_NUM_BUFFS];
	int cbid, snd_cnt;
};

/* Meta-data for the circular queues */
typedef struct {
	/* pointers within the buffer, counts of buffers within the
	 * ring, and amount of total buffers used for a given
	 * principal both in the driver ring, and in the stack. */
	unsigned int rb_head, rb_tail, curr_buffs, max_buffs, tot_principal, max_principal;
	ring_buff_t *rb;
	cos_lock_t l;
	struct buff_page avail_pages;
} rb_meta_t;
static rb_meta_t rb1_md_wildcard, rb2_md;
static ring_buff_t rb1, rb2;
static int wildcard_acap_id;

//cos_lock_t tmap_lock;
struct thd_map {
	rb_meta_t *uc_rb;
};

COS_VECT_CREATE_STATIC(tmap);

cos_lock_t netif_lock;

#define NET_LOCK_TAKE()    \
	do {								\
		if (lock_take(&netif_lock)) prints("error taking net lock."); \
	} while(0)

#define NET_LOCK_RELEASE() \
	do {								\
		if (lock_release(&netif_lock)) prints("error releasing net lock."); \
	} while (0)

struct cos_net_xmit_headers xmit_headers;

/******************* Manipulations for the thread map: ********************/
/* This structure allows an upcall thread to find its associated ring
 * buffers
 */
static struct thd_map *get_thd_map(unsigned short int thd_id)
{
	return cos_vect_lookup(&tmap, thd_id);
}

static int add_thd_map(unsigned short int ucid, rb_meta_t *rbm)
{
	struct thd_map *tm;

	tm = malloc(sizeof(struct thd_map));
	if (NULL == tm) return -1;

	tm->uc_rb = rbm;
	if (0 > cos_vect_add_id(&tmap, tm, ucid)) {
		free(tm);
		return -1;
	}

	return 0;
}

static int rem_thd_map(unsigned short int tid)
{
	struct thd_map *tm;

	tm = cos_vect_lookup(&tmap, tid);
	if (NULL == tm) return -1;
	free(tm);
	if (cos_vect_del(&tmap, tid)) return -1;

	return 0;
}


/*********************** Ring buffer, and memory management: ***********************/

static void rb_init(rb_meta_t *rbm, ring_buff_t *rb)
{
	int i;

	for (i = 0 ; i < RB_SIZE ; i++) {
		rb->packets[i].status = RB_EMPTY;
	}
	memset(rbm, 0, sizeof(rb_meta_t));
	rbm->rb_head       = 0;
	rbm->rb_tail       = RB_SIZE-1;
	rbm->rb            = rb;
//	rbm->curr_buffs    = rbm->max_buffs     = 0; 
//	rbm->tot_principal = rbm->max_principal = 0;
	lock_static_init(&rbm->l);
	INIT_LIST(&rbm->avail_pages, next, prev);
}

static int rb_add_buff(rb_meta_t *r, void *buf, int len)
{
	ring_buff_t *rb = r->rb;
	unsigned int head;
	struct rb_buff_t *rbb;

	assert(rb);
	lock_take(&r->l);
	head = r->rb_head;
	assert(head < RB_SIZE);
	rbb = &rb->packets[head];

	/* Buffer's full! */
	if (head == r->rb_tail) {
		goto err;
	}
	assert(rbb->status == RB_EMPTY);
	rbb->ptr = buf;
	rbb->len = len;

//	print("Adding buffer %x to ring.%d%d", (unsigned int)buf, 0,0);
	/* 
	 * The status must be set last.  It is the manner of
	 * communication between the kernel and this component
	 * regarding which cells contain valid pointers. 
	 *
	 * FIXME: There should be a memory barrier here, but I'll
	 * cross my fingers...
	 */
	rbb->status = RB_READY;
	r->rb_head = (r->rb_head + 1) & (RB_SIZE-1);
	lock_release(&r->l);

	return 0;
err:
	lock_release(&r->l);
	return -1;
}

/* 
 * -1 : there is no available buffer
 * 1  : the kernel found an error with this buffer, still set address
 *      and len.  Either the address was not mapped into this component, 
 *      or the memory region did not fit into a page.
 * 0  : successful, address contains data
 */
static int rb_retrieve_buff(rb_meta_t *r, unsigned int **buf, int *max_len)
{
	ring_buff_t *rb;
	unsigned int tail;
	struct rb_buff_t *rbb;
	unsigned short int status;

	assert(r);
	lock_take(&r->l);
	rb = r->rb;
	assert(rb);
	assert(r->rb_tail < RB_SIZE);
	tail = (r->rb_tail + 1) & (RB_SIZE-1);
	assert(tail < RB_SIZE);
	/* Nothing to retrieve */
	if (/*r->rb_*/tail == r->rb_head) {
		goto err;
	}
	rbb = &rb->packets[tail];
	status = rbb->status;
	if (status != RB_USED && status != RB_ERR) {
		goto err;
	}
	
	*buf = rbb->ptr;
	*max_len = rbb->len;
	/* Again: the status must be set last.  See comment in rb_add_buff. */
	rbb->status = RB_EMPTY;
	r->rb_tail = tail;

	lock_release(&r->l);
	if (status == RB_ERR) return 1;
	return 0;
err:
	lock_release(&r->l);
	return -1;
}

static struct buff_page *alloc_buff_page(void)
{
	struct buff_page *page;
	int i, cbid;
	int buff_offset = BUFF_ALIGN(sizeof(struct buff_page));

	page = (struct buff_page *)cbuf_alloc(PAGE_SIZE, (cbuf_t *)&cbid);
	if (!page) {
		return NULL;
	}
	page->cbid       = cbid;
	page->snd_cnt    = 0;
	page->amnt_buffs = 0;
	INIT_LIST(page, next, prev);
	for (i = 0 ; i < NP_NUM_BUFFS ; i++) {
		char *bs = (char *)page;

		page->buffs[i] = bs + buff_offset;
		page->buff_used[i] = 0;
		page->buff_len[i] = MTU;
		buff_offset += MTU;
	}
	return page;
}

static void *alloc_rb_buff(rb_meta_t *r)
{
	struct buff_page *p;
	int i;
	void *ret = NULL;

	lock_take(&r->l);
	if (EMPTY_LIST(&r->avail_pages, next, prev)) {
		if (NULL == (p = alloc_buff_page())) {
			lock_release(&r->l);
			return NULL;
		}
		ADD_LIST(&r->avail_pages, p, next, prev);
	}
	p = FIRST_LIST(&r->avail_pages, next, prev);
	assert(p->amnt_buffs < NP_NUM_BUFFS);
	for (i = 0 ; i < NP_NUM_BUFFS ; i++) {
		if (p->buff_used[i] == 0) {
			p->buff_used[i] = 1;
			ret = p->buffs[i];
			p->amnt_buffs++;
			break;
		}
	}
	assert(NULL != ret);
	if (p->amnt_buffs == NP_NUM_BUFFS) {
		REM_LIST(p, next, prev);
	}
	lock_release(&r->l);
	return ret;
}

static void release_rb_buff(rb_meta_t *r, void *b)
{
	struct buff_page *p;
	int i;

	assert(r && b);

	p = (struct buff_page *)(((unsigned long)b) & ~(4096-1));

	lock_take(&r->l);
	for (i = 0 ; i < NP_NUM_BUFFS ; i++) {
		if (p->buffs[i] == b) {
			p->buff_used[i] = 0;
			p->amnt_buffs--;
			REM_LIST(p, next, prev);
			ADD_LIST(&r->avail_pages, p, next, prev);
			lock_release(&r->l);
			return;
		}
	}
	/* b must be malformed such that p (the page descriptor) is
	 * not at the start of its page */
	BUG();
}

#include <sched.h>

static int cos_net_create_net_acap(unsigned short int port, rb_meta_t *rbm)
{
	int acap;

	acap = cos_async_cap_cntl(COS_ACAP_CREATE, cos_spd_id(), cos_spd_id(), cos_get_thd_id());
	assert(acap);
	/* cli acap not used. The server acap will be triggered by
	 * network driver. */
	wildcard_acap_id = acap & 0xFFFF;
	assert(wildcard_acap_id > 0);

	if (sched_create_net_acap(cos_spd_id(), wildcard_acap_id, port)) return -1;
	if (cos_buff_mgmt(COS_BM_RECV_RING, rb1.packets, sizeof(rb1.packets), wildcard_acap_id)) {
		prints("net: could not setup recv ring.\n");
		return -1;
	}
	return 0;
}

u16_t
lwip_ntohs(u16_t n)
{
  return ((n & 0xff) << 8) | ((n & 0xff00) >> 8);
}

static int __netif_xmit(struct cbuf_agg *agg, unsigned int sz)
{
	int i, j, len_on_page;
	struct gather_item *gi;
	char *data;
	assert(agg && sz > 0);
	xmit_headers.len = 0;
	/* 
	 * Here we do 2 things: create a separate gather data entry
	 * for each packet, and separate the data in individual
	 * packets into separate gather entries if it crosses page
	 * boundaries.  
	 */
	for (i=0, j=0; j<agg->ncbufs && i<XMIT_HEADERS_GATHER_LEN; i++, j++) {
		gi = &xmit_headers.gather_list[i];
		gi->len = agg->elem[j].len;
		data = cbuf2buf(agg->elem[j].id, gi->len)+agg->elem[j].offset;
		gi->data = data;
		len_on_page = (unsigned long)round_up_to_page(data) - (unsigned long)data;
		/* Data split across pages??? */
		if (len_on_page < agg->elem[j].len) {
			int len_on_second = agg->elem[j].len - len_on_page;

			if (XMIT_HEADERS_GATHER_LEN == i+1) assert(0);
			gi->len  = len_on_page;
			gi = gi+1;
			gi->data = data + len_on_page;
			gi->len  = len_on_second;
			i++;
		}
	}
	xmit_headers.gather_len = i;

	/* Send the collection of pbuf data on its way. */
	if (cos_buff_mgmt(COS_BM_XMIT, NULL, 0, 0)) {
		prints("net: could not xmit data.\n");
	}

	for(i=0; i<agg->ncbufs; i++) {
		cbuf_free(agg->elem[i].id);
	}
	return sz;
/* segment_err: */
/* 	printc("net: attempted to xmit too many segments"); */
/* 	goto done; */
}

static int interrupt_process(cbuf_t *id, int *off, int *sz)
{
	unsigned short int ucid = cos_get_thd_id();
	unsigned int *buff;
	int max_len;
	struct thd_map *tm;
	unsigned int len;
	struct buff_page *p;

	tm = get_thd_map(ucid);
	assert(tm);
	if (rb_retrieve_buff(tm->uc_rb, &buff, &max_len)) {
		prints("net: could not retrieve buffer from ring.\n");
		goto err;
	}
	len = buff[0];
	if (unlikely(len > MTU)) {
		printc("len %d > %d\n", len, MTU);
		goto err_replace_buff;
	}
	p    = (struct buff_page *)(((unsigned long)buff) & ~(PAGE_SIZE-1));
	*id  = p->cbid;
	*off = (char *)&buff[1]-(char *)p;
	*sz  = len;
	p->snd_cnt++;
	cbuf_send(p->cbid);
	if (p->snd_cnt == NP_NUM_BUFFS) cbuf_free(p->cbid);

	/* OK, fill in a new buffer. */
	if(!(p = (void *)alloc_rb_buff(tm->uc_rb))) {
		prints("net: could not allocate the ring buffer.");
	}
	if(rb_add_buff(tm->uc_rb, (void *)p, MTU)) {
		prints("net: could not populate the ring with buffer");
	}

	return 0;

err_replace_buff:
	/* Recycle the buffer (essentially dropping packet)... */
	if (rb_add_buff(tm->uc_rb, buff, MTU)) {
		prints("net: OOM, and filed to add buffer.");
	}
err:
	return -1;
}

#ifdef UPCALL_TIMING
u32_t last_upcall_cyc;
#endif

unsigned long netif_upcall_cyc(void)
{
#ifdef UPCALL_TIMING
	u32_t t = last_upcall_cyc;
	last_upcall_cyc = 0;
	return t;
#else
	return 0;
#endif
}

static int interrupt_wait(void)
{
	int ret;

	assert(wildcard_acap_id > 0);
	if (-1 == (ret = cos_ainv_wait(wildcard_acap_id))) {
		printc("netif ainv wait retun fal\n");
		BUG();
	}

#ifdef UPCALL_TIMING
	last_upcall_cyc = (u32_t)ret;
#endif	
	return 0;
}

/* 
 * Currently, this only adds to the wildcard acap.
 */
int netif_event_create(spdid_t spdid)
{
	unsigned short int ucid = cos_get_thd_id();

	NET_LOCK_TAKE();

	/* Wildcard upcall */
	if (cos_net_create_net_acap(0, &rb1_md_wildcard)) BUG();
	assert(wildcard_acap_id > 0);
	add_thd_map(ucid, /*0 wildcard port ,*/ &rb1_md_wildcard);
	NET_LOCK_RELEASE();
	printc("created net uc %d associated with acap %d\n", ucid, wildcard_acap_id);

	return 0;
}

int netif_event_release(spdid_t spdid)
{
	assert(wildcard_acap_id > 0);
	
	NET_LOCK_TAKE();
	rem_thd_map(cos_get_thd_id());
	NET_LOCK_RELEASE();

	return 0;
}

int netif_event_wait(spdid_t spdid, int *off, int *sz)
{
	cbuf_t ret_id;
//	printc("%d: I\n", cos_get_thd_id());
	interrupt_wait();
	NET_LOCK_TAKE();
	if (interrupt_process(&ret_id, off, sz)) BUG();
	NET_LOCK_RELEASE();

	return ret_id;
}

int netif_event_xmit(spdid_t spdid, struct cbuf_agg * agg, int sz)
{
	int ret;

	if (sz > MTU || sz <= 0) return -EINVAL;

	/* NET_LOCK_TAKE(); */
	ret = __netif_xmit(agg, (unsigned int)sz);
	/* NET_LOCK_RELEASE(); */

	return ret;
}

td_t 
tsplit(spdid_t spdid, td_t tid, char *param, int len, 
       tor_flags_t tflags, long evtid)
{
	td_t ret = -ENOMEM;
	struct torrent *t;

	if (tid != td_root) return -EINVAL;
	netif_event_create(spdid);
	t = tor_alloc((void*)1, tflags);
	if (!t) ERR_THROW(-ENOMEM, err);
	ret = t->td;
err:
	return ret;
}

void
trelease(spdid_t spdid, td_t td)
{
	struct torrent *t;

	if (!tor_is_usrdef(td)) return;
	t = tor_lookup(td);
	if (!t) goto done;
	netif_event_release(cos_spd_id());
	tor_free(t);
done:
	return;
}

int 
tmerge(spdid_t spdid, td_t td, td_t td_into, char *param, int len)
{
	return -ENOTSUP;
}

int 
twrite(spdid_t spdid, td_t td, int cbid, int sz)
{
	struct torrent *t;
	struct cbuf_agg *agg;
	int ret = -1;

	if (tor_isnull(td)) return -EINVAL;
	t = tor_lookup(td);
	if (!t) ERR_THROW(-EINVAL, done);
	if (!(t->flags & TOR_WRITE)) ERR_THROW(-EACCES, done);

	agg = (struct cbuf_agg *)cbuf2buf(cbid, sz);
	if (!agg) ERR_THROW(-EINVAL, done);
	ret = netif_event_xmit(spdid, agg, sz);
	cbuf_free(cbid);
done:
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
	struct torrent *t;
	cbuf_t ret;

	if (tor_isnull(td)) return -EINVAL;
	t = tor_lookup(td);
	if (!t)                      ERR_THROW(-EINVAL, done);
	if (!(t->flags & TOR_WRITE)) ERR_THROW(-EACCES, done);
	ret = netif_event_wait(spdid, off, sz);
done:
	return ret;
}

/*** Initialization routines: ***/

static int init(void) 
{
	unsigned short int i;
	void *b;
	void *vaddr[4];
	for (i=0; i<3; i++) {
		vaddr[i] = valloc_alloc(cos_spd_id(), cos_spd_id(), 100*256);
		assert(vaddr[i]);
	}
	for (i=0; i<3; i++) {
		valloc_free(cos_spd_id(), cos_spd_id(), vaddr[i], 100*256);
	}

	torlib_init();
	lock_static_init(&netif_lock);

	NET_LOCK_TAKE();

	cos_vect_init_static(&tmap);
	
	rb_init(&rb1_md_wildcard, &rb1);
	rb_init(&rb2_md, &rb2);

	/* Setup the region from which headers will be transmitted. */
	if (cos_buff_mgmt(COS_BM_XMIT_REGION, &xmit_headers, sizeof(xmit_headers), 0)) {
		prints("net: error setting up xmit region.");
	}

	for (i = 0 ; i < NUM_WILDCARD_BUFFS ; i++) {
		if(!(b = alloc_rb_buff(&rb1_md_wildcard))) {
			prints("net: could not allocate the ring buffer.");
		}
		if(rb_add_buff(&rb1_md_wildcard, b, MTU)) {
			prints("net: could not populate the ring with buffer");
		}
	}

	NET_LOCK_RELEASE();

	return 0;
}

void cos_init(void *arg)
{
	static volatile int first = 1;
	
	if (first) {
		first = 0;
		init();
	} else {
		prints("net: not expecting more than one bootstrap.");
	}
}

