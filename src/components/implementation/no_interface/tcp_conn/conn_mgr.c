/**
 * Copyright 2012 by Gabriel Parmer, gparmer@gwu.edu.  All rights
 * reserved.
 *
 * Adapted from the connection manager (no_interface/conn_mgr/) and
 * the file descriptor api (fd/api).
 *
 * Redistribution of this file is permitted under the GNU General
 * Public License v2.
 */

#define COS_FMT_PRINT
#include <cos_component.h>
#include <cos_alloc.h>
#include <cos_debug.h>
#include <cos_list.h>
#include <cvect.h>
#include <print.h>
#include <errno.h>
#include <cos_synchronization.h>
#include <sys/socket.h>
#include <stdio.h>
#include <torrent.h>
#include <sched.h>
#include <periodic_wake.h>

static volatile unsigned long http_conn_cnt = 0, http_req_cnt = 0;
static volatile unsigned long http_conn_tot = 0, http_req_tot = 0;
static cos_lock_t sc_lock;
char *parameter = "/bind:0:300/listen:300";
#define LOCK() if (lock_take(&sc_lock)) BUG();
#define UNLOCK() if (lock_release(&sc_lock)) BUG();

#define BUFF_SZ 2048//1401 //(COS_MAX_ARG_SZ/2)

CVECT_CREATE_STATIC(tor_from);
CVECT_CREATE_STATIC(tor_to);

static inline int 
tor_get_to(int from, long *teid) 
{ 
	int val = (int)cvect_lookup(&tor_from, from);
	*teid = val >> 16;
	return val & ((1<<16)-1); 
}

static inline int 
tor_get_from(int to, long *feid) 
{ 
	int val = (int)cvect_lookup(&tor_to, to);
	*feid = val >> 16;
	return val & ((1<<16)-1); 
}

static inline void 
tor_add_pair(int from, int to, long feid, long teid)
{
#define MAXVAL (1<<16)
	assert(from < MAXVAL);
	assert(to   < MAXVAL);
	assert(feid < MAXVAL);
	assert(teid < MAXVAL);
	if (cvect_add(&tor_from, (void*)((teid << 16) | to), from) < 0) BUG();
	if (cvect_add(&tor_to, (void*)((feid << 16) | from), to) < 0) BUG();
}

static inline void
tor_del_pair(int from, int to)
{
	cvect_del(&tor_from, from);
	cvect_del(&tor_to, to);
}

CVECT_CREATE_STATIC(evts);
#define EVT_CACHE_SZ 1
int evt_cache[EVT_CACHE_SZ];
int ncached = 0;

long evt_all = 0;

static inline long
evt_wait_all(void) { return evt_wait(cos_spd_id(), evt_all); }

/* 
 * tor > 0 == event is "from"
 * tor < 0 == event is "to"
 */
static inline long
evt_get(void)
{
	long eid;

	if (!evt_all) evt_all = evt_split(cos_spd_id(), 0, 1);
	assert(evt_all);

	eid = (ncached == 0) ?
		evt_split(cos_spd_id(), evt_all, 0) :
		evt_cache[--ncached];
	assert(eid > 0);

	return eid;
}

static inline void
evt_put(long evtid)
{
	if (ncached >= EVT_CACHE_SZ) evt_free(cos_spd_id(), evtid);
	else                         evt_cache[ncached++] = evtid;
}

/* positive return value == "from", negative == "to" */
static inline int
evt_torrent(long evtid) { return (int)cvect_lookup(&evts, evtid); }

static inline void
evt_add(int tid, long evtid) { cvect_add(&evts, (void*)tid, evtid); }

struct tor_conn {
	int  from, to;
	long feid, teid;
};

static inline void 
mapping_add(int from, int to, long feid, long teid)
{
	long tf, tt;

	LOCK();
	tor_add_pair(from, to, feid, teid);
	evt_add(from,    feid);
	evt_add(to * -1, teid);
	assert(tor_get_to(from, &tt) == to);
	assert(tor_get_from(to, &tf) == from);
	assert(evt_torrent(feid) == from);
	assert(evt_torrent(teid) == (-1*to));
	assert(tt == teid);
	assert(tf == feid);
	UNLOCK();
}

static inline void
mapping_remove(int from, int to, long feid, long teid)
{
	LOCK();
	tor_del_pair(from, to);
	cvect_del(&evts, feid);
	cvect_del(&evts, teid);
	UNLOCK();
}

static void 
accept_new(int accept_fd)
{
	static int to = 3;
	int from, feid, teid;
	static int num = 0;

	/* num++; */
	/* printc("accept %d\n", num); */
	while (1) {
		feid = evt_get();
		assert(feid > 0);
		from = tsplit(cos_spd_id(), accept_fd, "", 0, TOR_RW, feid);
		assert(from != accept_fd);
		if (-EAGAIN == from) {
			evt_put(feid);
			return;
		} else if (from < 0) {
			printc("from torrent returned %d\n", from);
			BUG();
			return;
		}

		teid = evt_get();
		assert(teid > 0);
		to = 3+(to+1)%3000;
		/* to++; */
		mapping_add(from, to, feid, teid);
	}
}

static const char success_head[] =
	"HTTP/1.1 200 OK\r\n"
	"Date: Mon, 01 Jan 1984 00:00:01 GMT\r\n"
	"Content-Type: text/html\r\n"
	"Connection: close\r\n"
	"Content-Length: 128\r\n\r\n";
/* #define MAX_SUPPORTED_DIGITS 20 */
/* PS_SLAB_CREATE_DEF(head, sizeof(success_head)+MAX_SUPPORTED_DIGITS) */

static void 
from_data_new(struct tor_conn *tc)
{
	int from, to, amnt, off;
	char *buf1, *head, *data, *s = "I am composite";
	struct cbuf_agg *agg;
	cbuf_t cb1, cb2, cb3, cbid;

	static int num = 0;

	from = tc->from;
	to   = tc->to;
	cbid = treadp(cos_spd_id(), from, &off, &amnt);
	if (-EPIPE == cbid) {
		goto close;
	}
	num++;
	if ((int)cbid < 0) printc("recv num %d id %d e1 %d e2 %d e3 %d\n", num, cbid, -EINVAL, -EPERM, -ENOMEM);
	if (0 == cbid) {
		/* printc("tread num %d evt %d id %d\n", num, tc->feid, cbid); */
		return ;
	}
	buf1 = (char *)cbuf2buf(cbid, amnt)+off;
	/* printc("recv amnt %d %.*s\n", amnt, amnt, buf1); */
	cbuf_free(cbid);

	agg = (struct cbuf_agg *)cbuf_alloc(BUFF_SZ, &cb1);
	/* head = ps_slab_alloc_head() */
	head = cbuf_alloc(BUFF_SZ, &cb2);
	amnt = sizeof(success_head) - 1;
	memcpy(head, success_head, amnt);
	head[amnt] = '\0';
	agg->elem[0].id = cb2;
	agg->elem[0].offset = 0;
	agg->elem[0].len = amnt+1;

	data = cbuf_alloc(BUFF_SZ, &cb3);
	memcpy(data, "$$$$$", 128);
	agg->elem[1].id = cb3;
	agg->elem[1].offset = 0;
	agg->elem[1].len = 128;

	agg->ncbufs = 2;
	cbuf_send_free(cb1);
	cbuf_send_free(cb2);
	cbuf_send_free(cb3);
	if (BUFF_SZ != twrite(cos_spd_id(), from, -1*cb1, BUFF_SZ)) {
		printc("conn_mgr: write failed w/ %d on fd %d\n", amnt, from);
	}
done:
	http_req_cnt++;
	http_req_tot++;
	return ;
close:
	mapping_remove(from, to, tc->feid, tc->teid);
	trelease(cos_spd_id(), from);
	evt_put(tc->feid);
	evt_put(tc->teid);
	http_conn_tot++;
	http_conn_cnt++;
	goto done;

}

void report_statics(void)
{
	if (periodic_wake_create(cos_spd_id(), 100)) BUG();
	while (1) {
		periodic_wake_wait(cos_spd_id());
		printc("HTTP conns %ld tot %ld, reqs %ld tot %ld\n", http_conn_cnt, 	http_conn_tot, http_req_cnt, http_req_tot);
		http_conn_cnt = http_req_cnt = 0;
	}
}

void
cos_init(void *arg)
{
	int c, accept_fd, ret;
	long eid;
	char *create_str;
	int lag, nthds, prio;
	union sched_param sp;
	
	cvect_init_static(&evts);
	cvect_init_static(&tor_from);
	cvect_init_static(&tor_to);
	lock_static_init(&sc_lock);
		
	sp.c.type  = SCHEDP_PRIO;
	sp.c.value = 4;
	if (cos_thd_create(report_statics, 0, sp.v, 0, 0) <= 0) {
		BUG();
	}

	create_str = strstr(parameter, "/");
	assert(create_str);

	eid = evt_get();
	ret = c = tsplit(cos_spd_id(), td_root, create_str, strlen(create_str), TOR_ALL, eid);
	if (ret <= td_root) BUG();
	accept_fd = c;
	evt_add(c, eid);
	printc("http thd %d\n", cos_get_thd_id());

	/* event loop... */
	while (1) {
		struct tor_conn tc;
		int t;
		long evt;

		memset(&tc, 0, sizeof(struct tor_conn));
		evt = evt_wait_all();
		t   = evt_torrent(evt);

		if (t > 0) {
			tc.feid = evt;
			tc.from = t;
			if (t == accept_fd) {
				tc.to = 0;
				accept_new(accept_fd);
			} else {
				tc.to = tor_get_to(t, &tc.teid);
				assert(tc.to > 0);
				from_data_new(&tc);
			}
		}
	}
}
