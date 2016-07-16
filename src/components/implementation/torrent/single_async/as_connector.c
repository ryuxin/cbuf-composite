/**
 * Copyright 2012 by Gabriel Parmer, gparmer@gwu.edu
 *
 * Redistribution of this file is permitted under the GNU General
 * Public License v2.
 */

#include <cos_component.h>
#include <torrent.h>
#include <torlib.h>

#include <cbuf.h>
#include <print.h>
#include <cos_synchronization.h>
#include <evt.h>
#include <cos_alloc.h>
#include <cos_map.h>
#include <cringbuf.h>

#include <ck_ring.h>
#define MBOX_BUFFER_SIZE 256
struct cbuf_info {
	cbuf_t cb;
	int sz, off;
};
CK_RING(cbuf_info, cb_buffer);
struct evt_info {
	int evt_id;
};
CK_RING(evt_info, evt_buffer);

enum {
	SERV   = 0,
	CLIENT = 1
};

struct as_conn {
	spdid_t owner;
	struct torrent *t; /* server evtid */
	CK_RING_INSTANCE(cb_buffer) *cbs[2];
	CK_RING_INSTANCE(evt_buffer) *evts;
};

static void 
free_as_conn_root(void *d)
{
	struct as_conn *i = (struct as_conn *)d;
	printc("fs free as conn\n");
	free_page(i->cbs[0]);
	free_page(i->cbs[1]);
	free_page(i->evts);
	free(i);
}
#define FS_DATA_FREE free_as_conn_root
#include <fs.h>

static cos_lock_t fs_lock;
struct fsobj root;
#define LOCK() if (lock_take(&fs_lock)) BUG();
#define UNLOCK() if (lock_release(&fs_lock)) BUG();

#define MAX_ALLOC_SZ 4096
#define MAX_DATA_SZ (MAX_ALLOC_SZ - sizeof(struct __cringbuf))

int
mbox_create_addr(spdid_t spdid, struct torrent *t, struct fsobj *parent, 
		 char *subpath, tor_flags_t tflags)
{
	int ret = 0, i;
	struct fsobj *fsc = NULL;;
	struct as_conn *ac;
	struct cbuf_info *buffer[2];
	struct evt_info *e;

	assert(parent);
	if (!(parent->flags & TOR_SPLIT)) ERR_THROW(-EACCES, done);
	fsc = fsobj_alloc(subpath, parent);
	if (!fsc) ERR_THROW(-EINVAL, done);
	fsc->flags    = tflags;

	ac = malloc(sizeof(struct as_conn));
	if (!ac) ERR_THROW(-ENOMEM, free);
	ac->owner = spdid;
	ac->t = t;
	for (i = 0 ; i < 2 ; i++) {
		ac->cbs[i] = alloc_page();
		CK_RING_INIT(cb_buffer, ac->cbs[i], buffer[i], MBOX_BUFFER_SIZE);
	}
	ac->evts = alloc_page();
	CK_RING_INIT(evt_buffer, ac->evts, e, MBOX_BUFFER_SIZE);
	fsc->data = t->data = (void*)ac;
	fsc->allocated = fsc->size = 0;
	t->flags = tflags | TOR_SPLIT;
done:
	return ret;
free:
	fsobj_release(fsc);
	fsc = NULL;
	goto done;
}

static int
mbox_put(struct torrent *t, cbuf_t cb, int sz, int off, int ep)
{
	struct as_conn *ac;
	int other_ep = !ep, ret = 0;
	struct cbuf_info cbi;
	struct evt_info evt;

	if (sz < 1) return -EAGAIN;
	ac  = t->data;
	cbi.cb  = cb;
	cbi.sz  = sz;
	cbi.off = off;
	ret = CK_RING_ENQUEUE_SPSC(cb_buffer, ac->cbs[ep], &cbi);
	if (ret == 0) return -EALREADY;
	if (!ep) {        	/* server */
		if (!CK_RING_DEQUEUE_SPSC(evt_buffer, ac->evts, &evt)) {
			printc("mbox server deq evt error\n");
			return -EAGAIN;
		}
		evt_trigger(cos_spd_id(), evt.evt_id);
	} else {            	/* client */
		evt.evt_id = t->evtid;
		if (!CK_RING_ENQUEUE_SPSC(evt_buffer, ac->evts, &evt)) {
			printc("mbox client enq evt error\n");
			return -EAGAIN;
		}
		evt_trigger(cos_spd_id(), ac->t->evtid);
	}
	return ret;
}

static int
mbox_get(struct as_conn *ac, int *sz, int *off, int ep)
{
	struct cbuf_info cbi;
	int other_ep = !ep;
	cbuf_t cb;

	if (!CK_RING_DEQUEUE_SPSC(cb_buffer, ac->cbs[other_ep], &cbi)) {
		return -EAGAIN;
	}
	cb   = cbi.cb;
	*off = cbi.off;
	*sz  = cbi.sz;
	if (!cb) {
		printc("mbox ep %d other %d off %d sz %d\n", ep, other_ep, *off, *sz);
		printc("mbox head %d tail %d size %d\n", ac->cbs[other_ep]->c_head, ac->cbs[other_ep]->p_tail, ac->cbs[other_ep]->size);
	}
	return cb;
}

td_t 
tsplit(spdid_t spdid, td_t td, char *param, 
       int len, tor_flags_t tflags, long evtid) 
{
	td_t ret = -1;
	struct torrent *t, *nt;
	struct fsobj *fsc, *parent = NULL; /* child, and parent */
	char *subpath;

	LOCK();
	t = tor_lookup(td);
	if (!t) ERR_THROW(-EINVAL, done);

	nt = tor_alloc(NULL, tflags);
	if (!nt) ERR_THROW(-ENOMEM, done);
	nt->evtid = evtid;

	fsc = fsobj_path2obj(param, len, t->data, &parent, &subpath);
	if (!fsc) {
		if (!(tflags & TOR_NONPERSIST)) ERR_THROW(-EINVAL, free);
		ret = mbox_create_addr(spdid, nt, parent, subpath, tflags);
	} else {
		struct as_conn *ac = (struct as_conn*)fsc->data;
		assert(ac);
		if ((~fsc->flags) & tflags) ERR_THROW(-EACCES, free);
		nt->data = ac;
		nt->flags = tflags & TOR_RW;
	}
	ret = nt->td;
done:
	UNLOCK();
	return ret;
free:
	tor_free(nt);
	goto done;
}

int 
tmerge(spdid_t spdid, td_t td, td_t td_into, char *param, int len)
{
	return -ENOTSUP;
}
int 
twrite(spdid_t spdid, td_t td, int cbid, int sz)
{
	return -ENOTSUP;
}
void
trelease(spdid_t spdid, td_t td)
{
	struct torrent *t;
	struct as_conn *ac;

	if (!tor_is_usrdef(td)) return;

	LOCK();
	t = tor_lookup(td);
	if (!t) goto done;

	ac = (struct as_conn *)t->data;
	if (t == ac->t) {
		printc("no support for server release\n");
		goto done;
	}
	tor_free(t);
done:
	UNLOCK();
	return;
}

int 
tread(spdid_t spdid, td_t td, int cbid, int sz)
{
	return -ENOTSUP;
}

int 
treadp(spdid_t spdid, td_t td, int *off, int *sz)
{
	cbuf_t ret;
	struct torrent *t;
	struct as_conn *ac;

	if (tor_isnull(td)) return -EINVAL;

	LOCK();
	t = tor_lookup(td);
	if (!t) ERR_THROW(-EINVAL, done);
	assert(!tor_is_usrdef(td) || t->data);
	if (!(t->flags & TOR_READ)) ERR_THROW(-EACCES, done);

	ac = t->data;
	ret = mbox_get(ac, sz, off, ac->owner != spdid);
done:	
	UNLOCK();
	return ret;
}

int 
twritep(spdid_t spdid, int temp, int off, int sz)
{
	td_t td;
	int cbid;
	int ret = -1;
	struct torrent *t;
	struct as_conn *ac;

	cbid = temp & 0xffff;
	td = temp >> 16;
	if (tor_isnull(td)) return -EINVAL;
	LOCK();
	t = tor_lookup(td);
	if (!t) ERR_THROW(-EINVAL, done);
	assert(t->data);
	if (!(t->flags & TOR_WRITE)) ERR_THROW(-EACCES, done);
	ac = t->data;
	ret = mbox_put(t, cbid, sz, off, ac->owner != spdid);
done:	
	UNLOCK();
	return ret;
}

int cos_init(void)
{
	lock_static_init(&fs_lock);
	torlib_init();

	fs_init_root(&root);
	root_torrent.data = &root;
	root.flags = TOR_READ | TOR_SPLIT;

	return 0;
}
