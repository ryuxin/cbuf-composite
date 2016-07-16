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

static cos_lock_t sc_lock;
#define LOCK() if (lock_take(&sc_lock)) BUG();
#define UNLOCK() if (lock_release(&sc_lock)) BUG();

#define BUFF_SZ 2048//1401 //(COS_MAX_ARG_SZ/2)
#define EVT_CACHE_SZ 1
#define MSG_SZ 16
int evt_cache[EVT_CACHE_SZ];
int ncached = 0;
long evt_all = 0;
char *param = "/udp/bind:0:200/connect:0xf79da480:5555";

static inline long
evt_wait_all(void) { return evt_wait(cos_spd_id(), evt_all); }

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

int
udp_bind(char *str)
{
	int td;
	long eid;

	eid = evt_get();
	td  = tsplit(cos_spd_id(), td_root, str, strlen(str), TOR_ALL, eid);
	if (td <= td_root) BUG();

	return td;
}

int
udp_recv(int tid, int *off, int *sz)
{
	int eid = -1;
	cbuf_t cbid = 0;

	while(1) {
		cbid = treadp(cos_spd_id(), tid, off, sz);
		if (0 == cbid) 	eid = evt_wait_all();
		else if (cbid == -EINVAL) goto close;
		else break;
	}

done:
	return cbid;
close:
	trelease(cos_spd_id(), tid);
	evt_put(eid);
	goto done;

}

int 
udp_send(int tid, int cbid, int sz)
{
	int amnt;
	cbuf_send(cbid);
	amnt = twrite(cos_spd_id(), tid, cbid, sz);
	if (amnt != sz) {
		printc("udp send failed w/ %d of %d on fd %d\n",
		       amnt, sz, tid);
		trelease(cos_spd_id(), tid);
		return 0;
	}
	return sz;
}

void
udp_thread(void)
{
	int td, sz, i, j, off;
	cbuf_t cbid, id;
	char *packet, *s = "I am composite 1", *buf;

	td = udp_bind(param);
	cbuf_mempool_resize(cos_spd_id(), 4096*600);
	cbuf_debug_cbuf_info(cos_spd_id(), 1, 1);
	/* event loop... */
	for(i=0; i>-50; i++) {
	/* for(i=0; i<5; i++) { */
		for(j=0; j<100; j++) {
			buf = cbuf_alloc(BUFF_SZ, &id);
			memcpy(buf, s, strlen(s));
			cbid = udp_recv(td, &off, &sz);
			assert(cbid > 0);
			packet = (char *)cbuf2buf(cbid, sz)+off;
			assert(packet);
//			printc("recv %d sz %d cbid %d\n", i*100+j, sz, cbid);
			cbuf_free(cbid);
			udp_send(td, id, MSG_SZ);
			cbuf_free(id);
		}
	}
}

void
cos_init(void *arg)
{
	union sched_param sp;

	lock_static_init(&sc_lock);
	sp.c.type  = SCHEDP_RPRIO; 
	sp.c.value = 4;
	if (cos_thd_create(udp_thread, 0, sp.v, 0, 0) <= 0) {
		BUG();
	}
	return ;
}
