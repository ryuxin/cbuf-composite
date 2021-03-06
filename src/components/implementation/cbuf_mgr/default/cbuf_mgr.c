/*
 * Copyright 2012 by The George Washington University.  All rights reserved.
 *
 * Redistribution of this file is permitted under the GNU General
 * Public License v2.
 *
 * Author: Gabriel Parmer, gparmer@gwu.edu, 2012
 */

#include <cos_component.h>
#include <mem_mgr_large.h>
#include <cbuf_meta.h>
#include <cbuf_mgr.h>
#include <cos_synchronization.h>
#include <valloc.h>
#include <sched.h>
#include <cos_alloc.h>
#include <cmap.h>
#include <cos_list.h>

#include <stkmgr.h>

/* debug helper */
#define OP_NUM 10
/* 0-create 1-collect 2-delete */
/* 3-retrieve 4-register */
/* 56-reserve 7-cbuf_alloc_map */
int op_nums[OP_NUM];
unsigned long long per_total[OP_NUM];

#define INIT_TARGET_SIZE 4096*2048

/** 
 * The main data-structures tracked in this component.
 * 
 * cbuf_comp_info is the per-component data-structure that tracks the
 * page shared with the component to return garbage-collected cbufs, the
 * cbufs allocated to the component, and the data-structures for
 * tracking where the cbuf_metas are associated with the cbufs.
 * 
 * cbuf_meta_range is a simple linked list to track the metas for
 * given cbuf id ranges.
 *
 * cbuf_info is the per-cbuf structure that tracks the cbid, size,
 * and contains a linked list of all of the mappings for that cbuf.
 *
 * See the following diagram:

  cbuf_comp_info                 cbuf_meta_range
  +------------------------+	  +---------+	+---------+
  | spdid     	           |  +-->| daddr   +--->         |
  | +--------------------+ |  |	  | cbid    |	|         |
  | | size = X, c-+      | |  |	  | meta    <---+       | |
  | --------------|------- |  |	  +-|-------+	+-------|-+
  | | size = ...  |      | |  |     |  +-----------+	+-->
  | +-------------|------+ |  |	    +->|           |
  |          cbuf_metas-------+	       +-----^-----+ cbuf_meta
  +---------------|--------+	+------------|---+
		  |   		| cbid, size |   |
		  |     	| +----------|-+ | +------------+
		  +------------>| | spdid,.. | |<->| .., addr   |
		   		| +------------+ | +------------+
				+----------------+     cbuf_maps
                                cbuf_info
*/

/* Per-cbuf information */
struct cbuf_maps {
	spdid_t spdid;
	vaddr_t addr;
	struct cbuf_meta *m;
	struct cbuf_maps *next, *prev;
};

struct cbuf_info {
	int cbid;
	unsigned long size;
	char *mem;
	struct cbuf_maps owner;
	struct cbuf_info *next, *prev;
};

/* Per-component information */
struct cbuf_meta_range {
	struct cbuf_meta *m;
	vaddr_t dest;
	int low_id;
	struct cbuf_meta_range *next, *prev;
};
#define CBUF_META_RANGE_HIGH(cmr) (cmr->low_id + (int)(PAGE_SIZE/sizeof(struct cbuf_meta)))

struct cbuf_bin {
	unsigned long size;
	struct cbuf_info *c;
};
struct blocked_thd {
	unsigned short int thd_id;
	unsigned long request_size;
	struct blocked_thd *next, *prev;
	unsigned long long blk_start;
};
struct cbuf_comp_info {
	spdid_t spdid;
	struct cbuf_shared_page *csp;
	vaddr_t dest_csp;
	int nbin;
	struct cbuf_bin cbufs[CBUF_MAX_NSZ];
	struct cbuf_meta_range *cbuf_metas;
	unsigned long target_size, allocated_size;
	int num_blocked_thds;
	struct blocked_thd bthd_list;
	unsigned long long gc_tot, gc_max;
	unsigned long long blk_tot, blk_max;
	int gc_num;
};

#define printl(s) //printc(s)
cos_lock_t cbuf_lock;
#define CBUF_LOCK_INIT() lock_static_init(&cbuf_lock);
#define CBUF_TAKE()      do { if (lock_take(&cbuf_lock))    BUG(); } while(0)
#define CBUF_RELEASE()   do { if (lock_release(&cbuf_lock)) BUG(); } while(0)
CVECT_CREATE_STATIC(components);
CMAP_CREATE_STATIC(cbufs);

static struct cbuf_meta_range *
cbuf_meta_lookup_cmr(struct cbuf_comp_info *comp, int cbid)
{
	struct cbuf_meta_range *cmr;
	assert(comp);

	cmr = comp->cbuf_metas;
	if (!cmr) return NULL;
	do {
		if (cmr->low_id <= cbid && CBUF_META_RANGE_HIGH(cmr) > cbid) {
			return cmr;
		}
		cmr = FIRST_LIST(cmr, next, prev);
	} while (cmr != comp->cbuf_metas);

	return NULL;
}

static struct cbuf_meta *
cbuf_meta_lookup(struct cbuf_comp_info *comp, int cbid)
{
	struct cbuf_meta_range *cmr;

	cmr = cbuf_meta_lookup_cmr(comp, cbid);
	if (!cmr) return NULL;
	return &cmr->m[cbid - cmr->low_id];
}

static struct cbuf_meta_range *
cbuf_meta_add(struct cbuf_comp_info *comp, int cbid, struct cbuf_meta *m, vaddr_t dest)
{
	struct cbuf_meta_range *cmr;

	if (cbuf_meta_lookup(comp, cbid)) return NULL;
	cmr = malloc(sizeof(struct cbuf_meta_range));
	if (unlikely(!cmr)) return NULL;
	INIT_LIST(cmr, next, prev);
	cmr->m      = m;
	cmr->dest   = dest;
	cmr->low_id = round_to_pow2(cbid, PAGE_SIZE/sizeof(struct cbuf_meta));

	if (comp->cbuf_metas) ADD_LIST(comp->cbuf_metas, cmr, next, prev);
	else                  comp->cbuf_metas = cmr;

	return cmr;
}

static void
cbuf_comp_info_init(spdid_t spdid, struct cbuf_comp_info *cci)
{
	void *p;
	memset(cci, 0, sizeof(*cci));
	cci->spdid       = spdid;
	cci->target_size = INIT_TARGET_SIZE;
	INIT_LIST(&cci->bthd_list, next, prev);
	cvect_add(&components, cci, spdid);
}

static struct cbuf_comp_info *
cbuf_comp_info_get(spdid_t spdid)
{
	struct cbuf_comp_info *cci;

	cci = cvect_lookup(&components, spdid);
	if (!cci) {
		cci = malloc(sizeof(*cci));
		if (!cci) return NULL;
		cbuf_comp_info_init(spdid, cci);
	}
	return cci;
}

static struct cbuf_bin *
cbuf_comp_info_bin_get(struct cbuf_comp_info *cci, unsigned long sz)
{
	int i;

	assert(sz);
	for (i = 0 ; i < cci->nbin ; i++) {
		if (sz == cci->cbufs[i].size) return &cci->cbufs[i];
	}
	return NULL;
}

static struct cbuf_bin *
cbuf_comp_info_bin_add(struct cbuf_comp_info *cci, unsigned long sz)
{
	/* TODO: this is wrong. cbuf_max_nsz is not the maximum size of cbuf */
	//if (unlikely(sz >= CBUF_MAX_NSZ)) return NULL;
	cci->cbufs[cci->nbin].size = sz;
	cci->nbin++;

	return &cci->cbufs[cci->nbin-1];
}

static int
cbuf_map(spdid_t spdid, vaddr_t daddr, void *page, unsigned long size, int flags)
{
	unsigned long off;
	assert(size == round_to_page(size));
	assert(daddr);
	assert(page);
	for (off = 0 ; off < size ; off += PAGE_SIZE) {
		vaddr_t d = daddr + off;
		if (unlikely(d != (mman_alias_page(cos_spd_id(), ((vaddr_t)page) + off,
						   spdid, d, flags)))) {
			for(d = daddr+off-PAGE_SIZE; d >= daddr; d -= PAGE_SIZE) {
				mman_revoke_page(spdid, d, 0);
			}
			return -ENOMEM;
		}
	}
	return 0;
}
/* map the memory from address p and size sz to the component spdid with 
 * permission flags. if p is NULL allocate a piece of new memory
 * return spdid's address to daddr, manager's virtual address to page*/
static int
cbuf_alloc_map(spdid_t spdid, vaddr_t *daddr, void **page, void *p, unsigned long sz, int flags)
{
	vaddr_t dest;
	int ret = 0;
	void *new_p;

	u64_t start, end;
	rdtscll(start);

	assert(sz == round_to_page(sz));
	if (!p) {
		new_p = page_alloc(sz/PAGE_SIZE);
		assert(new_p);
		memset(new_p, 0, sz);
	} else {
		new_p = p;
	}

	dest = (vaddr_t)valloc_alloc(cos_spd_id(), spdid, sz/PAGE_SIZE);
	/* if (unlikely(!dest)) goto free; */
	if (unlikely(!dest)) {
		printc("cbuf mgr alloc map dest spd %d\n", spdid);
		assert(0);
		goto free;
	}
	/* if (!cbuf_map(spdid, dest, new_p, sz, flags)) goto done; */
	if (!cbuf_map(spdid, dest, new_p, sz, flags)) {
		goto done;
	}
	else {
		printc("cbuf mgr alloc map no map spd %d dest %x p %x sz %d\n", spdid, dest, new_p, sz);
		assert(0);
	}

free: 
	if (dest) valloc_free(cos_spd_id(), spdid, (void *)dest, 1);
	if (!p) page_free(new_p, sz/PAGE_SIZE);
	ret = -1;

done:	
	if (page) *page  = new_p;
	*daddr = dest;
	rdtscll(end);
	op_nums[7]++;
	per_total[7] += (end-start);
	return ret;
}

/* Do any components have a reference to the cbuf? 
 * key function coordinates manager and client.
 * When this returns 1, this cbuf may or may not be used by some components.
 * When this returns 0, it guarantees: 
 * If all clients use the protocol correctly, there is no reference 
 * to the cbuf and no one will receive the cbuf after this. Furthermore, 
 * if the cbuf is in some free list, its inconsistent bit is already set.
 * That is to say, the manager can safely collect or re-balance this cbuf.
 *
 * Proof: 1. If a component gets the cbuf from free-list, it will 
 * simply discard this cbuf as its inconsistent bit is set.
 * 2. Assume component c sends the cbuf. 
 * It is impossible to send the cbuf after we check c's refcnt, since c 
 * has no reference to this cbuf.
 * If this send happens before we check c's refcnt, because the sum of 
 * nsent is equal to the sum of nrecv, this send has been received and 
 * no further receive will happen.
 * 3. No one will call cbuf2buf to receive this cbuf after this function, 
 * as all sends have been received and no more sends will occur during this function
 *
 * However, if clients do not use protocol correctly, this function 
 * provides no guarantee. cbuf_unmap_prepare takes care of this case.
 */
static int
cbuf_referenced(struct cbuf_info *cbi)
{
	struct cbuf_maps *m = &cbi->owner;
	int sent, recvd, ret = 1;
	unsigned long old_nfo, new_nfo;
	unsigned long long old;
	struct cbuf_meta *mt, *own_mt = m->m;

	old_nfo = own_mt->nfo;
	new_nfo = old_nfo | CBUF_INCONSISENT;
	if (unlikely(!cos_cas(&own_mt->nfo, old_nfo, new_nfo))) {
		goto done;
	}

	mt   = (struct cbuf_meta *)(&old);
	sent = recvd = 0;
	do {
		struct cbuf_meta *meta = m->m;

		/* Guarantee atomically read the two words (refcnt and nsent/nrecv).
		 * Consider this case, c0 sends a cbuf to c1 and frees this 
		 * this cbuf, but before c1 receives it, the manager comes in 
		 * and checks c1's refcnt. Now it is zero. But before the manager 
		 * checks c1's nsent/nrecv, it is preempted by c1. c1 receives 
		 * this cbuf--increment refcnt and nsent/nrecv. After this, we 
		 * switch back the manager, who will continues to check c1's 
		 * nsent/nrecv, now it is 1, which is equal to c0's nsent/nrecv. 
		 * Thus the manager can collect or unmap this cbuf.*/
		old = *(unsigned long long *)meta;
		if (unlikely(!cos_dcas(&old, old, old))) {
			goto unset;
		}
		if (CBUF_REFCNT(mt)) goto unset;		
		/* TODO: add per-mapping counter of sent and recv in the manager */
		/* each time atomic clear those counter in the meta */
		sent  += mt->snd_rcv.nsent;
		recvd += mt->snd_rcv.nrecvd;
		m      = FIRST_LIST(m, next, prev);
	} while (m != &cbi->owner);
	if (sent != recvd) goto unset;
	ret = 0;
	if (CBUF_IS_IN_FREELIST(own_mt)) goto done;

unset:
	CBUF_FLAG_ATOMIC_REM(own_mt, CBUF_INCONSISENT);
done:
	return ret;
}

/* TODO: Incorporate this into cbuf_referenced */
static void
cbuf_references_clear(struct cbuf_info *cbi)
{
	struct cbuf_maps *m = &cbi->owner;

	do {
		struct cbuf_meta *meta = m->m;

		if (meta) {
			meta->snd_rcv.nsent = meta->snd_rcv.nrecvd = 0;
		}
		m = FIRST_LIST(m, next, prev);
	} while (m != &cbi->owner);

	return;
}

/* Before actually unmap cbuf from a component, we need to atomically
 * clear the page pointer in the meta, which guarantees that clients 
 * do not have seg fault. Clients have to check NULL when receive cbuf */
static int
cbuf_unmap_prepare(struct cbuf_info *cbi)
{
	struct cbuf_maps *m = &cbi->owner;
	unsigned long old_nfo, new_nfo;

	if (cbuf_referenced(cbi)) return 1;
	cbuf_references_clear(cbi);

	/* We need to clear out the meta. Consider here manager removes a
	 * cbuf from component c0 and allocates that cbuf to component c1,  
	 * but c1 sends the cbuf back to c0. If c0 sees the old meta, it may 
	 * be confused. However, we have to leave the inconsistent bit here */
	do {
		old_nfo = m->m->nfo;
		if (old_nfo & CBUF_REFCNT_MAX) return 1;
		new_nfo = old_nfo & CBUF_INCONSISENT;
		if (unlikely(!cos_cas(&m->m->nfo, old_nfo, new_nfo))) {
			return 1;
		}
		m   = FIRST_LIST(m, next, prev);
	} while (m != &cbi->owner);

	return 0;
}

/* As clients maybe malicious or don't use protocol correctly, we cannot 
 * simply unmap memory here. We guarantee that fault can only happen within 
 * the malicious component, but for other components, they either receive a 
 * NULL pointer from cbuf2buf or see wrong data. No fault happen in other 
 * components. See details in cbuf_unmap_prepare  */
static int
cbuf_free_unmap(struct cbuf_comp_info *cci, struct cbuf_info *cbi)
{
	struct cbuf_maps *m = &cbi->owner, *next;
	struct cbuf_bin *bin;
	void *ptr = cbi->mem;
	unsigned long off, size = cbi->size;

	if (cbuf_unmap_prepare(cbi)) return 1;

	/* Unmap all of the pages from the clients */
	for (off = 0 ; off < size ; off += PAGE_SIZE) {
		mman_revoke_page(cos_spd_id(), (vaddr_t)ptr + off, 0);
	}

	/* 
	 * Deallocate the virtual address in the client, and cleanup
	 * the memory in this component
	 */
	m = FIRST_LIST(&cbi->owner, next, prev);
	while (m != &cbi->owner) {
		next = FIRST_LIST(m, next, prev);
		REM_LIST(m, next, prev);
		valloc_free(cos_spd_id(), m->spdid, (void*)m->addr, size/PAGE_SIZE);
		free(m);
		m = next;
	}
	valloc_free(cos_spd_id(), m->spdid, (void*)m->addr, size/PAGE_SIZE);

	/* deallocate/unlink our data-structures */
	page_free(ptr, size/PAGE_SIZE);
	cmap_del(&cbufs, cbi->cbid);
	cci->allocated_size -= size;
	bin = cbuf_comp_info_bin_get(cci, size);
	if (EMPTY_LIST(cbi, next, prev)) {
		bin->c = NULL;
	} else {
		if (bin->c == cbi) bin->c = cbi->next;
		REM_LIST(cbi, next, prev);
	}
	free(cbi);

	return 0;
}

static inline void
cbuf_mark_relinquish_all(struct cbuf_comp_info *cci)
{
	int i;
	struct cbuf_bin *bin;
	struct cbuf_info *cbi;
	struct cbuf_maps *m;

	for(i=cci->nbin-1; i>=0; --i) {
		bin = &cci->cbufs[i];
		cbi = bin->c;
		do {
			if (!cbi) break;
			m = &cbi->owner;
			do {
				CBUF_FLAG_ATOMIC_ADD(m->m, CBUF_RELINQ);
				m  = FIRST_LIST(m, next, prev);
			} while(m != &cbi->owner);
			cbi   = FIRST_LIST(cbi, next, prev);
		} while (cbi != bin->c);
	}
}

static inline void
cbuf_unmark_relinquish_all(struct cbuf_comp_info *cci)
{
	int i;
	struct cbuf_bin *bin;
	struct cbuf_info *cbi;
	struct cbuf_maps *m;

	for(i=cci->nbin-1; i>=0; --i) {
		bin = &cci->cbufs[i];
		cbi = bin->c;
		do {
			if (!cbi) break;
			m = &cbi->owner;
			do {
				CBUF_FLAG_ATOMIC_REM(m->m, CBUF_RELINQ);
				m  = FIRST_LIST(m, next, prev);
			} while(m != &cbi->owner);
			cbi   = FIRST_LIST(cbi, next, prev);
		} while (cbi != bin->c);
	}
}

static inline void
cbuf_thread_block(struct cbuf_comp_info *cci, unsigned long request_size)
{
	struct blocked_thd bthd;

	bthd.thd_id       = cos_get_thd_id();
	bthd.request_size = request_size;
	rdtscll(bthd.blk_start);
	ADD_LIST(&cci->bthd_list, &bthd, next, prev);
	cci->num_blocked_thds++;
	cbuf_mark_relinquish_all(cci);
	CBUF_RELEASE();
	sched_block(cos_spd_id(), 0);
}

/* wake up all blocked threads whose request size smaller than or equal to available size */
static void
cbuf_thd_wake_up(struct cbuf_comp_info *cci, unsigned long sz)
{
	struct blocked_thd *bthd, *next;
	unsigned long long cur, tot;

	assert(cci->num_blocked_thds >= 0);
	/* Cannot wake up thd when in shrink */
	assert(cci->target_size >= cci->allocated_size);

	if (cci->num_blocked_thds == 0) return;
	bthd = cci->bthd_list.next;
	while (bthd != &cci->bthd_list) {
		next = FIRST_LIST(bthd, next, prev);
		if (bthd->request_size <= sz) {
			REM_LIST(bthd, next, prev);
			cci->num_blocked_thds--;
			rdtscll(cur);
			tot = cur-bthd->blk_start;
			cci->blk_tot += tot;
			if (tot > cci->blk_max) cci->blk_max = tot;
			sched_wakeup(cos_spd_id(), bthd->thd_id);
		}
		bthd = next;
	}
	if (cci->num_blocked_thds == 0) cbuf_unmark_relinquish_all(cci);
}

static void
cbuf_shrink(struct cbuf_comp_info *cci, int diff);
int
cbuf_create(spdid_t spdid, unsigned long size, int cbid)
{
	struct cbuf_comp_info *cci;
	struct cbuf_info *cbi;
	struct cbuf_meta *meta;
	struct cbuf_bin *bin;
	int ret = 0;

	printl("cbuf_create\n");
	if (unlikely(cbid < 0)) return 0;
	CBUF_TAKE();

	u64_t start, end;
	rdtscll(start);

	cci = cbuf_comp_info_get(spdid);
	if (unlikely(!cci)) goto done;

	/* 
	 * Client wants to allocate a new cbuf, but the meta might not
	 * be mapped in.
	 */
	if (!cbid) {
		/* TODO: check if have enough free memory: ask mem manager */
		/*memory usage exceeds the target, block this thread*/
		if (size + cci->allocated_size > cci->target_size) {
			cbuf_shrink(cci, size);
			/* printc("spd %d block 1 target %d alloc %d req %d page\n", spdid, cci->target_size/PAGE_SIZE, cci->allocated_size/PAGE_SIZE, size/PAGE_SIZE); */
			if (size + cci->allocated_size > cci->target_size) {
				/* printc("spd %d block target %d alloc %d req %d page\n", spdid, cci->target_size/PAGE_SIZE, cci->allocated_size/PAGE_SIZE, size/PAGE_SIZE); */
				cbuf_thread_block(cci, size);
				return 0;
			}
		}

 		cbi = malloc(sizeof(struct cbuf_info));

		if (unlikely(!cbi)) goto done;
		/* Allocate and map in the cbuf. Discard inconsistent cbufs */
		/* TODO: Find a better way to manage those inconsistent cbufs */
		do {
			cbid = cmap_add(&cbufs, cbi);
			meta = cbuf_meta_lookup(cci, cbid);
			if (meta && meta->nfo) printc("cbuf mgr spd %d id %d meta %x nfo %x\n", spdid, cbid, meta, meta->nfo);
		} while(meta && CBUF_INCONSISENT(meta));

		cbi->cbid        = cbid;
		size             = round_up_to_page(size);
		cbi->size        = size;
		cbi->owner.m     = NULL;
		cbi->owner.spdid = spdid;
		INIT_LIST(&cbi->owner, next, prev);
		INIT_LIST(cbi, next, prev);
		if (cbuf_alloc_map(spdid, &(cbi->owner.addr), 
				   (void**)&(cbi->mem), NULL, size, MAPPING_RW)) {
			goto free;
		}
	} 
	/* If the client has a cbid, then make sure we agree! */
	else {
		cbi = cmap_lookup(&cbufs, cbid);
		if (unlikely(!cbi)) goto done;
		if (unlikely(cbi->owner.spdid != spdid)) goto done;
	}
	meta = cbuf_meta_lookup(cci, cbid);

	/* We need to map in the meta for this cbid.  Tell the client. */
	if (!meta) {
		ret = cbid * -1;
		goto done;
	}
	
	/* 
	 * Now we know we have a cbid, a backing structure for it, a
	 * component structure, and the meta mapped in for the cbuf.
	 * Update the meta with the correct addresses and flags!
	 */
	memset(meta, 0, sizeof(struct cbuf_meta));
	meta->sz            = cbi->size >> PAGE_ORDER;
	meta->cbid_tag.cbid = cbid;
	CBUF_FLAG_ADD(meta, CBUF_OWNER);
	CBUF_PTR_SET(meta, cbi->owner.addr);
	CBUF_REFCNT_INC(meta);
	/* When creates a new cbuf, the manager should be the only
	 * one who can access the meta */
	/* TODO: malicious client may trigger this assertion, just for debug */
	assert(CBUF_REFCNT(meta) == 1);
	assert(CBUF_PTR(meta));
	cbi->owner.m = meta;
	/* Install cbi last. If not, after return a negative cbid, 
	 * collection may happen and get a dangle cbi */
	bin = cbuf_comp_info_bin_get(cci, size);
	if (!bin) bin = cbuf_comp_info_bin_add(cci, size);
	if (unlikely(!bin)) goto free;
	if (bin->c) ADD_LIST(bin->c, cbi, next, prev);
	else        bin->c   = cbi;
	cci->allocated_size += size;
	ret = cbid;
done:
	rdtscll(end);
	op_nums[0]++;
	per_total[0] += (end-start);
	CBUF_RELEASE();

	return ret;
free:
	cmap_del(&cbufs, cbid);
	free(cbi);
	goto done;
}

vaddr_t
cbuf_map_at(spdid_t s_spd, int cbid, spdid_t d_spd, vaddr_t d_addr)
{
	vaddr_t ret = (vaddr_t)NULL;
	struct cbuf_info *cbi;
	int flags, id;
	
	id = cbid;
	CBUF_TAKE();
	cbi = cmap_lookup(&cbufs, id);
	assert(cbi);
	if (unlikely(!cbi)) goto done;
	assert(cbi->owner.spdid == s_spd);
	// the low-order bits of the d_addr are packed with the MAPPING flags (0/1)
	// and a flag (2) set if valloc should not be used.
	flags = d_addr & 0x3;
	d_addr &= ~0x3;
	if (!(flags & 2) && valloc_alloc_at(s_spd, d_spd, (void*)d_addr, cbi->size/PAGE_SIZE)) goto done;
	if (cbuf_map(d_spd, d_addr, cbi->mem, cbi->size, flags & (MAPPING_READ|MAPPING_RW))) goto free;
	ret = d_addr;
	/* do not add d_spd to the meta list because the cbuf is not
	 * accessible directly. The s_spd must maintain the necessary info
	 * about the cbuf and its mapping in d_spd. */
done:
	CBUF_RELEASE();
	return ret;
free:
	if (!(flags & 2)) valloc_free(s_spd, d_spd, (void*)d_addr, cbi->size);
	goto done;
}

int
cbuf_unmap_at(spdid_t s_spd, int cbid, spdid_t d_spd, vaddr_t d_addr)
{
	struct cbuf_info *cbi;
	int ret = 0, id;
	u32_t off;
	int err;

	id = cbid;
	assert(d_addr);
	CBUF_TAKE();
	cbi = cmap_lookup(&cbufs, id);
	if (unlikely(!cbi)) ERR_THROW(-EINVAL, done);
	if (unlikely(cbi->owner.spdid != s_spd)) ERR_THROW(-EINVAL, done);
	assert(cbi->size == round_to_page(cbi->size));
	/* unmap pages in only the d_spd client */
	for (off = 0 ; off < cbi->size ; off += PAGE_SIZE)
		mman_release_page(d_spd, d_addr + off, 0);
	err = valloc_free(s_spd, d_spd, (void*)d_addr, cbi->size/PAGE_SIZE);
	if (unlikely(err)) ERR_THROW(-EFAULT, done);
	assert(!err);
done:
	CBUF_RELEASE();
	return ret;
}

/*
 * Allocate and map the garbage-collection list used for cbuf_collect()
 */
vaddr_t
cbuf_map_collect(spdid_t spdid)
{
	struct cbuf_comp_info *cci;
	vaddr_t ret = (vaddr_t)NULL;

	printl("cbuf_map_collect\n");

	CBUF_TAKE();
	cci = cbuf_comp_info_get(spdid);
	if (unlikely(!cci)) goto done;

	/* if the mapped page exists already, just return it. */
	if (cci->dest_csp) {
		ret = cci->dest_csp;
		goto done;
	}

	assert(sizeof(struct cbuf_shared_page) <= PAGE_SIZE);
	/* alloc/map is leaked. Where should it be freed/unmapped? */
	if (cbuf_alloc_map(spdid, &cci->dest_csp, (void**)&cci->csp, NULL, PAGE_SIZE, MAPPING_RW)) {
		goto done;
	}
	ret = cci->dest_csp;

	/* initialize a continuous ck ring */
	assert(cci->csp->ring.size == 0);
	CK_RING_INIT(cbuf_ring, &cci->csp->ring, NULL, CSP_BUFFER_SIZE);

done:
	CBUF_RELEASE();
	return ret;
}

/*
 * For a certain principal, collect any unreferenced and not_in 
 * free list cbufs so that they can be reused.  This is the 
 * garbage-collection mechanism.
 *
 * Collect cbufs and add them onto the shared component's ring buffer.
 *
 * This function is semantically complicated. It can return no cbufs 
 * even if they are available to force the pool of cbufs to be
 * expanded (the client will call cbuf_create in this case). 
 * Or, the common case: it can return a number of available cbufs.
 */
int
cbuf_collect(spdid_t spdid, unsigned long size)
{
	struct cbuf_info *cbi;
	struct cbuf_comp_info *cci;
	struct cbuf_shared_page *csp;
	struct cbuf_bin *bin;
	int ret = 0;

	printl("cbuf_collect\n");

	CBUF_TAKE();
	u64_t start, end;
	rdtscll(start);
	cci  = cbuf_comp_info_get(spdid);
	if (unlikely(!cci)) ERR_THROW(-ENOMEM, done);
	if (size + cci->allocated_size <= cci->target_size) goto done;

	csp  = cci->csp;
	if (unlikely(!csp)) ERR_THROW(-EINVAL, done);

	assert(csp->ring.size == CSP_BUFFER_SIZE);
	ret = CK_RING_SIZE(cbuf_ring, &csp->ring);
	if (ret != 0) goto done;
	/* 
	 * Go through all cbufs we own, and report all of them that
	 * have no current references to them.  Unfortunately, this is
	 * O(N*M), N = min(num cbufs, PAGE_SIZE/sizeof(int)), and M =
	 * num components.
	 */
	size = round_up_to_page(size);
	bin  = cbuf_comp_info_bin_get(cci, size);
	if (!bin) ERR_THROW(0, done);
	cbi  = bin->c;
	do {
		if (!cbi) break;
		/* skip cbufs which are in freelist. Coordinates with cbuf_free to 
		 * detect such cbufs correctly. 
		 * We must check refcnt first and then next pointer.
		 *
		 * If do not check refcnt: the manager may check "next" before cbuf_free 
		 * (when it is NULL), then switch to client who calls cbuf_free to set 
		 * "next", decrease refcnt and add cbuf to freelist. Then switch back to 
		 * manager, but now it will collect this in-freelist cbuf.
		 * 
		 * Furthermore we must check refcnt before the "next" pointer: 
		 * If not, similar to above case, the manager maybe preempted by client 
		 * between the manager checks "next" and refcnt. Therefore the manager 
		 * finds the "next" is null and refcnt is 0, and collect this cbuf.
		 * Short-circuit can prevent reordering. */
		assert(cbi->owner.m);
		if (!CBUF_REFCNT(cbi->owner.m) && !CBUF_IS_IN_FREELIST(cbi->owner.m)
                 		    && !cbuf_referenced(cbi)) {
			struct cbuf_ring_element el = { .cbid = cbi->cbid };
			cbuf_references_clear(cbi);
			if (!CK_RING_ENQUEUE_SPSC(cbuf_ring, &csp->ring, &el)) break;
			/* Prevent other collection collecting those cbufs.
			 * The manager checks if the shared ring buffer is empty upon 
			 * the entry, if not, it just returns. This is not enough to 
			 * prevent double-collection. The corner case is: 
			 * after the last one in ring buffer is dequeued and 
			 * before it is added to the free-list, the manager  
			 * appears. It may collect the last one again. */
			cbi->owner.m->next = (struct cbuf_meta *)1;
			if (++ret == CSP_BUFFER_SIZE) break;
		}
		cbi = FIRST_LIST(cbi, next, prev);
	} while (cbi != bin->c);
	if (ret) cbuf_thd_wake_up(cci, ret*size);

done:
	rdtscll(end);
	op_nums[1]++;
	cci->gc_num++;
	per_total[1] = (end-start)+900;
	cci->gc_tot += per_total[1];
	if (per_total[1] > cci->gc_max) cci->gc_max = per_total[1];
	CBUF_RELEASE();
	return ret;
}

/* 
 * Called by cbuf_free.
 */
int
cbuf_delete(spdid_t spdid, int cbid)
{
	struct cbuf_comp_info *cci;
	struct cbuf_info *cbi;
	struct cbuf_meta *meta;
	int ret = -EINVAL, sz;

	printl("cbuf_delete\n");
	CBUF_TAKE();

	u64_t start, end;
	rdtscll(start);

	cci  = cbuf_comp_info_get(spdid);
	if (unlikely(!cci)) goto done;
	cbi  = cmap_lookup(&cbufs, cbid);
	if (unlikely(!cbi)) goto done;
	meta = cbuf_meta_lookup(cci, cbid);
	/* Other threads can access the meta data simultaneously. For
	 * example, others call cbuf2buf which increase the refcnt. */
	CBUF_REFCNT_ATOMIC_DEC(meta);
	/* Find the owner of this cbuf */
	if (cbi->owner.spdid != spdid) {
		cci = cbuf_comp_info_get(cbi->owner.spdid);
		if (unlikely(!cci)) goto done;
	}
	if (cbuf_free_unmap(cci, cbi)) 	goto done;
	if (cci->allocated_size < cci->target_size) {
		cbuf_thd_wake_up(cci, cci->target_size - cci->allocated_size);
	}
	ret = 0;
done:
	rdtscll(end);
	op_nums[2]++;
	per_total[2] += (end-start);
	CBUF_RELEASE();
	return ret;
}

/* 
 * Called by cbuf2buf to retrieve a given cbid.
 */
int
cbuf_retrieve(spdid_t spdid, int cbid, unsigned long size)
{
	struct cbuf_comp_info *cci, *own;
	struct cbuf_info *cbi;
	struct cbuf_meta *meta, *own_meta;
	struct cbuf_maps *map;
	vaddr_t dest;
	void *page;
	int ret = -EINVAL, off;

	printl("cbuf_retrieve\n");

	CBUF_TAKE();

	u64_t start, end;
	rdtscll(start);

	cci        = cbuf_comp_info_get(spdid);
	if (unlikely(!cci)) {printc("cbuf 1\n"); goto done;}
	cbi        = cmap_lookup(&cbufs, cbid);
	if (unlikely(!cbi)) {printc("cbuf 2\n"); goto done;}
	/* shouldn't cbuf2buf your own buffer! */
	if (cbi->owner.spdid == spdid) {printc("cbuf 3\n"); goto done;}
	meta       = cbuf_meta_lookup(cci, cbid);
	if (!meta) {printc("cbuf 4\n"); goto done;}
	assert(!(meta->nfo & ~CBUF_INCONSISENT));

	map        = malloc(sizeof(struct cbuf_maps));
	if (unlikely(!map)) ERR_THROW(-ENOMEM, done);
	if (size > cbi->size) {printc("cbuf 5\n"); goto done;}
	assert(round_to_page(cbi->size) == cbi->size);
	size       = cbi->size;
	/* TODO: change to MAPPING_READ */
	if (cbuf_alloc_map(spdid, &map->addr, NULL, cbi->mem, size, MAPPING_RW)) {
		printc("cbuf mgr map fail spd %d mem %x sz %d cbid %d\n", spdid, cbi->mem, size, cbid);
		goto free;
	}

	INIT_LIST(map, next, prev);
	ADD_LIST(&cbi->owner, map, next, prev);
	CBUF_PTR_SET(meta, map->addr);
	map->spdid          = spdid;
	map->m              = meta;
	meta->sz            = cbi->size >> PAGE_ORDER;
	meta->cbid_tag.cbid = cbid;
	own                 = cbuf_comp_info_get(cbi->owner.spdid);
	if (unlikely(!own)) goto done;
	/* We need to inherit the relinquish bit from the sender. 
	 * Otherwise, this cbuf cannot be returned to the manager. */
	own_meta            = cbuf_meta_lookup(own, cbid);
	if (CBUF_RELINQ(own_meta)) CBUF_FLAG_ADD(meta, CBUF_RELINQ);
	ret                 = 0;
done:
	rdtscll(end);
	op_nums[3]++;
	per_total[3] += (end-start);

	CBUF_RELEASE();
	return ret;
free:
	free(map);
	goto done;
}

vaddr_t
cbuf_register(spdid_t spdid, int cbid)
{
	struct cbuf_comp_info  *cci;
	struct cbuf_meta_range *cmr;
	void *p;
	vaddr_t dest, ret = 0;

	printl("cbuf_register\n");
	CBUF_TAKE();
	u64_t start, end;
	rdtscll(start);

	cci = cbuf_comp_info_get(spdid);
	if (unlikely(!cci)) goto done;
	cmr = cbuf_meta_lookup_cmr(cci, cbid);
	if (cmr) ERR_THROW(cmr->dest, done);

	/* Create the mapping into the client */
	if (cbuf_alloc_map(spdid, &dest, &p, NULL, PAGE_SIZE, MAPPING_RW)) goto done;
	assert((unsigned int)p == round_to_page(p));
	cmr = cbuf_meta_add(cci, cbid, p, dest);
	assert(cmr);
	ret = cmr->dest;
done:
	rdtscll(end);
	op_nums[4]++;
	per_total[4] += (end-start);

	CBUF_RELEASE();
	return ret;
}

static void
cbuf_shrink(struct cbuf_comp_info *cci, int diff)
{
	int i, sz;
	struct cbuf_bin *bin;
	struct cbuf_info *cbi, *next, *head;

	for(i=cci->nbin-1; i>=0; i--) {
		bin = &cci->cbufs[i];
		sz = (int)bin->size;
		if (!bin->c) continue;
		cbi = FIRST_LIST(bin->c, next, prev);
		int i = 0;
		while (cbi != bin->c) {
			next = FIRST_LIST(cbi, next, prev);
			if (!cbuf_free_unmap(cci, cbi)) {
				diff -= sz;
				if (diff <= 0) return;
			}
			cbi = next;
		}
		if (!cbuf_free_unmap(cci, cbi)) {
			diff -= sz;
			if (diff <= 0) return;
		}
	}
	if (diff > 0) cbuf_mark_relinquish_all(cci);
}

static inline void
cbuf_expand(struct cbuf_comp_info *cci, int diff)
{
	if (cci->allocated_size < cci->target_size) {
		cbuf_thd_wake_up(cci, cci->target_size - cci->allocated_size);
	}
}

/* target_size is an absolute size */
void
cbuf_mempool_resize(spdid_t spdid, unsigned long target_size)
{
	struct cbuf_comp_info *cci;
	int diff;

	CBUF_TAKE();
	cci = cbuf_comp_info_get(spdid);
	if (unlikely(!cci)) goto done;
	target_size = round_up_to_page(target_size);
	/* printc("cbuf mgr spd %d rsz %d -> %d\n", spdid, cci->target_size/PAGE_SIZE, target_size/PAGE_SIZE); */
	diff = (int)(target_size - cci->target_size);
	cci->target_size = target_size;
	if (diff < 0 && cci->allocated_size > cci->target_size) {
		cbuf_shrink(cci, cci->allocated_size - cci->target_size);
	}
	if (diff > 0) cbuf_expand(cci, diff);
done:
	CBUF_RELEASE();
}

unsigned long
cbuf_memory_target_get(spdid_t spdid)
{
	struct cbuf_comp_info *cci;
	int ret;
	CBUF_TAKE();
	cci = cbuf_comp_info_get(spdid);
	if (unlikely(!cci)) ERR_THROW(-ENOMEM, done);
	ret = cci->target_size;
done:
	CBUF_RELEASE();
	return ret;
}

/* the assembly code that invokes stkmgr expects this memory layout */
struct cos_stk {
	struct cos_stk *next;
	u32_t flags;
	u32_t thdid_owner;
	u32_t cpu_id;
} __attribute__((packed));
#define D_COS_STK_ADDR(d_addr) (d_addr + PAGE_SIZE - sizeof(struct cos_stk))

/* Never give up! */
void
stkmgr_return_stack(spdid_t s_spdid, vaddr_t addr)
{
	BUG();
}

/* map a stack into d_spdid.
 * TODO: use cbufs. */
void *
stkmgr_grant_stack(spdid_t d_spdid)
{
	struct cbuf_comp_info *cci;
	void *p, *ret = NULL;
	vaddr_t d_addr;

	printl("stkmgr_grant_stack (cbuf)\n");

	CBUF_TAKE();
	cci = cbuf_comp_info_get(d_spdid);
	if (!cci) goto done;

	if (cbuf_alloc_map(d_spdid, &d_addr, (void**)&p, NULL, PAGE_SIZE, MAPPING_RW)) goto done;
	ret = (void*)D_COS_STK_ADDR(d_addr);

done:
	CBUF_RELEASE();
	return ret;
}


void
cos_init(void)
{
	void *vaddr[4];
	int i;
	for (i=0; i<4; i++) {
		vaddr[i] = valloc_alloc(cos_spd_id(), cos_spd_id(), 100*256);
		assert(vaddr[i]);
	}
	for (i=0; i<4; i++) {
		valloc_free(cos_spd_id(), cos_spd_id(), vaddr[i], 100*256);
	}

	CBUF_LOCK_INIT();
	cmap_init_static(&cbufs);
	cmap_add(&cbufs, NULL);

	/* debug */
	memset(op_nums, 0, sizeof(op_nums));
	memset(per_total, 0, sizeof(per_total));
}


/* Debug helper functions */
static int __debug_reference(struct cbuf_info * cbi)
{
	struct cbuf_maps *m = &cbi->owner;
	int sent = 0, recvd = 0;
	do {
		struct cbuf_meta *meta = m->m;
		if (CBUF_REFCNT(meta)) return 1;
		sent  += meta->snd_rcv.nsent;
		recvd += meta->snd_rcv.nrecvd;
		m   = FIRST_LIST(m, next, prev);
	} while (m != &cbi->owner);
	if (sent != recvd) return 1;
	return 0;
}

unsigned long cbuf_debug_cbuf_info(spdid_t spdid, int index, int p)
{
	unsigned long ret[20], sz;
	struct cbuf_comp_info *cci;
	struct cbuf_bin *bin;
	struct cbuf_info *cbi, *next, *head;
	struct cbuf_meta *meta;
	struct blocked_thd *bthd;
	unsigned long long cur;
	int i;

	CBUF_TAKE();
	cci = cbuf_comp_info_get(spdid);
	if (unlikely(!cci)) assert(0);
	memset(ret, 0, sizeof(ret));

	ret[0] = cci->target_size;
	ret[1] = cci->allocated_size;
	if(p == 1) printc("target %lu %lu allocate %lu %lu\n", ret[0], ret[0]/PAGE_SIZE, ret[1], ret[1]/PAGE_SIZE);

	for(i=cci->nbin-1; i>=0; i--) {
		bin = &cci->cbufs[i];
		sz = bin->size;
		if (!bin->c) continue;
		cbi = bin->c;
		do {
			if (__debug_reference(cbi)) ret[2] += sz;
			else                        ret[3] += sz;
			meta = cbi->owner.m;
			if (CBUF_RELINQ(meta)) ret[4]++;
			cbi = FIRST_LIST(cbi, next, prev);
		} while(cbi != bin->c);
	}
	if(p == 1) printc("using %lu %lu garbage %lu %lu relinq %lu\n", ret[2], ret[2]/PAGE_SIZE, ret[3], ret[3]/PAGE_SIZE, ret[4]);
	assert(ret[2]+ret[3] == ret[1]);
	ret[5] = cci->num_blocked_thds;
	if (ret[5]) {
		rdtscll(cur);
		bthd = cci->bthd_list.next;
		while (bthd != &cci->bthd_list) {
			cci->blk_tot += (cur-bthd->blk_start);
			ret[6] += bthd->request_size;
			bthd->blk_start = cur;
			bthd = FIRST_LIST(bthd, next, prev);
		}
		/* printc("cbuf mgr num blk %d sz %d\n", ret[5], ret[6]); */
	}
	if(p == 1) printc("spd %d %lu thd blocked request %d pages %d\n", spdid, ret[5], ret[6], ret[6]/PAGE_SIZE);
	ret[7] = (unsigned long)cci->blk_tot;
	ret[8] = (unsigned long)cci->blk_max;
	ret[9] = (unsigned long)cci->gc_tot;
	ret[10] = (unsigned long)cci->gc_max;
	if (p == 1) printc("spd %d blk_tot %lu blk_max %lu gc_tot %lu gc_max %lu\n", spdid, ret[7], ret[8], ret[9], ret[10]);
	if (p == 2) {
		cci->blk_tot = cci->blk_max = cci->gc_tot = cci->gc_max = 0;
		/* printc("cbuf mgr gc spd %d number %d\n", spdid, cci->gc_num); */
		cci->gc_num = 0;
	}

	CBUF_RELEASE();
	return ret[index];
}

void cbuf_debug_cbiddump(int cbid)
{
	struct cbuf_info *cbi;
	struct cbuf_maps *m;
	printc("mgr dump cbid %d\n", cbid);
	cbi = cmap_lookup(&cbufs, cbid);
	assert(cbi);
	printc("cbid %d cbi: id %d sz %lu mem %p\n", cbid, cbi->cbid, cbi->size, cbi->mem);
	m   = &cbi->owner;
	do {
		struct cbuf_meta *meta = m->m;
		printc("map: spd %d addr %lux meta %p\n", m->spdid, m->addr, m->m);
		printc("meta: nfo %lux addr %lux cbid %d\n", meta->nfo, CBUF_PTR(meta), meta->cbid_tag.cbid);
		cos_mmap_cntl(55, MAPPING_RW, m->spdid, CBUF_PTR(meta), 0);
		m = FIRST_LIST(m, next, prev);
	} while(m != &cbi->owner);
}

void cbuf_debug_profile(int p)
{
	if (p) {
		if (op_nums[0] != 0) printc("create %d avg %llu\n", op_nums[0], per_total[0]/op_nums[0]);
		if (op_nums[1] != 0) printc("collect %d avg %llu\n", op_nums[1], per_total[1]/op_nums[1]);
		if (op_nums[2] != 0) printc("delete %d avg %llu\n", op_nums[2], per_total[2]/op_nums[2]);
		if (op_nums[3] != 0) printc("retrieve %d avg %llu\n", op_nums[3], per_total[3]/op_nums[3]);
		if (op_nums[4] != 0) printc("register %d avg %llu\n", op_nums[4], per_total[4]/op_nums[4]);
		if (op_nums[7] != 0) printc("alloc map %d avg %llu\n", op_nums[7], per_total[7]/op_nums[7]);
	}
	memset(op_nums, 0, sizeof(op_nums));
	memset(per_total, 0, sizeof(per_total));
}
