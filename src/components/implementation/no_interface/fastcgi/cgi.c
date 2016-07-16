/**
 */

#include <stdlib.h>
#include <cos_component.h>
#include <print.h>
#include <sched.h>
#include <cbuf.h>
#include <evt.h>
#include <torrent.h>

#define CBUF_SND_MAX 16
struct http_request_param {
        int size;
};
td_t tor;
long evt;

void cos_init(void *arg)
{
	int off, sz, temp, agg_sz, cbuf_num, i, tot=0;
	char *params1 = "cgi", *reply;
	struct http_request_param *buf;
	cbuf_t cb, reply_id, ret_id;
	union sched_param sp;
	static int first = 1;
	struct cbuf_agg *agg;

	if (first) {
		void *vaddr[4];
		int i;
		for (i=0; i<2; i++) {
			vaddr[i] = valloc_alloc(cos_spd_id(), cos_spd_id(), 80*256);
			assert(vaddr[i]);
		}
		for (i=0; i<2; i++) {
			valloc_free(cos_spd_id(), cos_spd_id(), vaddr[i], 80*256);
		}
		/* cbuf_mempool_resize(cos_spd_id(), 200*PAGE_SIZE); */

		first = 0;
		evt = evt_split(cos_spd_id(), 0, 0);
		tor = tsplit(cos_spd_id(), td_root, params1, strlen(params1), TOR_ALL | TOR_NONPERSIST | TOR_RW, evt);
		if (tor < 1) {
			printc("cgi: split failed %d\n", tor);
		}

		sp.c.type = SCHEDP_PRIO;
		sp.c.value = 8;
		if (sched_create_thd(cos_spd_id(), sp.v, 0, 0) == 0) BUG();
		return ;
	}
	printc("cgi thd %d\n", cos_get_thd_id());
	while (1) {
		while (1) {
			cb = treadp(cos_spd_id(), tor, &off, &sz);
			if ((int)cb<0) evt_wait(cos_spd_id(), evt);
			else break;
		}
		buf = (struct http_request_param *)(cbuf2buf(cb, sz) + off);
		sz = buf->size;
		cbuf_free(cb);
		reply = (char *)cbuf_alloc(sz, &ret_id);
		memset(reply, '$', sz);

		/* printc("cgi recv sz %d agg num %d\n", sz, agg->ncbufs); */
		/* cbuf_num = sz+CBUF_SND_MAX*PAGE_SIZE-1; */
		/* cbuf_num /= (CBUF_SND_MAX*PAGE_SIZE); */
		/* agg_sz = sizeof(int)+sizeof(struct cbuf_agg_elem)*cbuf_num; */
		/* agg = (struct cbuf_agg *)cbuf_alloc(agg_sz, &ret_id); */
		/* agg->ncbufs = cbuf_num; */
		/* tot = 0; */
		/* for(i=0; i<cbuf_num-1; i++) { */
		/* 	reply = (char *)cbuf_alloc(CBUF_SND_MAX*PAGE_SIZE, &reply_id); */
		/* 	memset(reply, '$', CBUF_SND_MAX*PAGE_SIZE); */
		/* 	agg->elem[i].id     = reply_id; */
		/* 	agg->elem[i].offset = 0; */
		/* 	agg->elem[i].len    = CBUF_SND_MAX*PAGE_SIZE; */
		/* 	tot += CBUF_SND_MAX*PAGE_SIZE; */
		/* 	cbuf_send_free(reply_id); */
		/* } */
		/* sz -= tot; */

		/* reply = (char *)cbuf_alloc(sz, &reply_id); */
		/* memset(reply, '$', sz); */
		/* agg->elem[i].id     = reply_id; */
		/* agg->elem[i].offset = 0; */
		/* agg->elem[i].len    = sz; */
		/* cbuf_send_free(reply_id); */

		temp = (tor << 16) | ret_id;
		cbuf_send_free(ret_id);
		twritep(cos_spd_id(), temp, 0, sz);
	}

	return;
}
