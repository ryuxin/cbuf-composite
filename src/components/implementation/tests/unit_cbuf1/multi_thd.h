#ifndef  MULTI_H
#define  MULTI_H

#include <sched.h>
#include <timed_blk.h>

#define CBUFP_SZ 4096
#define CBUFPS   200
#define TMEMS    200
#define NUM_ROUND 100
int thd_finish, allocated, start_ok;
void single_thd_tmem_micro_test(void)
{
	cbuf_t cbs[TMEMS], cb;
	void *bufs[TMEMS], *buf;
	int i, j;
	u64_t start, end, re_alloc = 0, re_free = 0;

	for(j=0; j<NUM_ROUND; j++) {
		cbuf_debug_clear_freelist(PAGE_SIZE);
		cbuf_mempool_resize(cos_spd_id(), 400*PAGE_SIZE);
		rdtscll(start);
		for(i=0; i<TMEMS; i++) {
			bufs[i] = cbuf_alloc_ext(CBUFP_SZ, &cbs[i], CBUF_TMEM);
		}
		rdtscll(end);
		re_alloc += end-start;
		rdtscll(start);
		for(i=0; i<TMEMS; i++) {
			cbuf_free(cbs[i]);
		}
		rdtscll(end);
		re_free += end-start;
	}
	printc("alloc no collect slow %llu tmem free %llu cycs\n", re_alloc/(TMEMS*NUM_ROUND), re_free/(TMEMS*NUM_ROUND));
	for(i=0; i<TMEMS; i++) {
		bufs[i] = cbuf_alloc_ext(CBUFP_SZ, &cbs[i], CBUF_TMEM);
	}
	for(i=0; i<TMEMS; i++) {
		cbuf_free(cbs[i]);
	}

	re_alloc = re_free = 0;
	for(j=0; j<NUM_ROUND; j++) {
		rdtscll(start);
		for(i=0; i<TMEMS; i++) {
			bufs[i] = cbuf_alloc_ext(CBUFP_SZ, &cbs[i], CBUF_TMEM);
		}
		rdtscll(end);
		re_alloc += end-start;
		rdtscll(start);
		for(i=0; i<TMEMS; i++) {
			cbuf_free(cbs[i]);
		}
		rdtscll(end);
		re_free += end-start;
	}
	printc("alloc cached %llu tmem free %llu cycs\n", re_alloc/(TMEMS*NUM_ROUND), re_free/(TMEMS*NUM_ROUND));
}

void single_thd_alloc_free(void)
{
	cbuf_t cbs[CBUFPS], cb;
	void *bufs[CBUFPS], *buf;
	int i;

	cbuf_debug_clear_freelist(PAGE_SIZE);
	cbuf_mempool_resize(cos_spd_id(), CBUFPS*PAGE_SIZE);

	for(i=0; i<CBUFPS; i++) {
		bufs[i] = cbuf_alloc(CBUFP_SZ , &cbs[i]);
		assert(bufs[i]);
	}
	assert(CBUFP_SZ*CBUFPS == cbuf_debug_cbuf_info(cos_spd_id(), 2, 0));

	for(i=0; i<CBUFPS; i++) assert(NULL == cbuf2buf(cbs[i], CBUFP_SZ));

	for(i=0; i<CBUFPS; i++) {
		cbuf_free(cbs[i]);
		assert(CBUFP_SZ == cbuf_debug_cbuf_info(cos_spd_id(), 3, 0));
		buf = cbuf_alloc(CBUFP_SZ, &cb);
		/*should collect the exact one just freed*/
		assert(0 == cbuf_debug_freelist_num(CBUFP_SZ));
		assert(cb == cbs[i]);
		assert(buf == bufs[i]);
	}
	printc("collect single one finish\n");

	cbuf_mempool_resize(cos_spd_id(), cbuf_memory_target_get(cos_spd_id())+PAGE_SIZE);
	buf = cbuf_alloc(CBUFP_SZ, &cb);
	assert(0 == cbuf_debug_freelist_num(CBUFP_SZ));
	cbuf_free(cb);
	assert(CBUFP_SZ*(1+CBUFPS) == cbuf_debug_cbuf_info(cos_spd_id(), 1, 0));
	for(i=0; i<CBUFPS; i++) cbuf_free(cbs[i]);
	assert(CBUFP_SZ*(1+CBUFPS) == cbuf_debug_cbuf_info(cos_spd_id(), 3, 0));
	printc("cbuf free finish\n");

	/*should collect all free cbufs*/
	buf = cbuf_alloc(CBUFP_SZ, &cb);
	assert(CBUFPS == cbuf_debug_freelist_num(CBUFP_SZ));

	for(i=0; i<CBUFPS; i++) bufs[i] = cbuf_alloc(CBUFP_SZ, &cbs[i]);
	printc("fast alloc %d cbuf finish\n", CBUFPS);
	cbuf_mempool_resize(cos_spd_id(), 0);
	/*should give all cbuf back to manager*/
	cbuf_free(cb);
	for(i=0; i<CBUFPS; i++) cbuf_free(cbs[i]);
	assert(0 == cbuf_debug_cbuf_info(cos_spd_id(), 1, 0));
	printc("cbuf delete finish\n");
}

void single_thd_test(void)
{
	printc("============\n");
	unit_cbuf_debug_info();
	printc("============\n");
	cbuf_debug_cbuf_info(cos_spd_id(), 0, 1);
	cbuf_mempool_resize(cos_spd_id(), 0);
	cbuf_debug_cbuf_info(cos_spd_id(), 0, 1);
	printc("on freelist %d\n", cbuf_debug_freelist_num(PAGE_SIZE));
	printc("============\n");
	single_thd_tmem_micro_test();
	printc("============\n");
	single_thd_alloc_free();
}

/* to run m minutes */
void alloc_free_test(int m)
{
	cbuf_t cbs[CBUFPS], cb;
	char *bufs[CBUFPS], *buf;
	int i;
	unsigned long sz = PAGE_SIZE;
	unsigned long long start, end = 0, total;

	total = (unsigned long long)2*1000000000*60*m;
	printc("thd %d m %d %llu\n", cos_get_thd_id(), m, total);
	rdtscll(start);
	int j = 0;
	do {
		j++;
		for(i=0; i<CBUFPS; i++) {
			bufs[i] = cbuf_alloc_ext(sz, &cbs[i], CBUF_TMEM);
			assert(bufs[i]);
			cbuf_free(cbs[i]);
		}

		for(i=0; i<CBUFPS; i++) {
			bufs[i] = cbuf_alloc_ext(sz, &cbs[i], CBUF_TMEM);
			assert(bufs[i]);
		}
		for(i=0; i<CBUFPS; i++) {
			cbuf_free(cbs[i]);
		}

		for(i=0; i<CBUFPS; i++) {
			bufs[i] = cbuf_alloc(sz, &cbs[i]);
			bufs[i][0] = '_';
			cbuf_send(cbs[i]);
			unit_cbuf(cbs[i], sz);
		}

		for(i=0; i<CBUFPS; i++) {
			cbuf_send(cbs[i]);
			unit_cbuf(cbs[i], sz);
		}

		for(i=0; i<CBUFPS; i++) {
			cbuf_free(cbs[i]);
		}

		rdtscll(end);
	} while(end-start < total);
	cos_faa(&thd_finish, 1);
}

void relinq_test()
{
	cbuf_t cbs[10];
	int i, j = 10;
	while(j--) {
		while (!start_ok) {
			timed_event_block(cos_spd_id(), 1);
		}
		for(i=0; i<10; i++) {
			cbuf_alloc(PAGE_SIZE, &cbs[i]);
			allocated++;
		}
		for(i=0; i<10; i++) {
			cbuf_free(cbs[i]);
			allocated--;
		}
	}
}

void multi_thd_test()
{
	int i, num_thd;
	cbuf_t cb;
	union sched_param sp;
	sp.c.type  = SCHEDP_RPRIO; 
	sp.c.value = 4;
	num_thd = 4;
	cbuf_mempool_resize(cos_spd_id(), num_thd*CBUFPS*PAGE_SIZE);
	thd_finish = allocated = start_ok = 0;
	for(i=0; i<num_thd; i++) {
		if (cos_thd_create(alloc_free_test, 0, sp.v, 0, 0) <= 0) {
			BUG();
		}
	}
	while (thd_finish != num_thd) {
		timed_event_block(cos_spd_id(), 1);
	}
	cbuf_debug_profile(1);

	cbuf_mempool_resize(cos_spd_id(), 10*PAGE_SIZE);
	if (cos_thd_create(relinq_test, NULL, sp.v, 0, 0) <= 0) BUG();
	i = 10;
	while (i--) {
		cbuf_alloc(PAGE_SIZE, &cb);
		start_ok = 1;
		while (allocated != 9) {
			timed_event_block(cos_spd_id(), 1);
		}
		/* one thd blocked, all cbuf are relinq */
		assert(cbuf_debug_cbuf_info(cos_spd_id(), 5, 0) == 1);
		assert(cbuf_debug_cbuf_info(cos_spd_id(), 4, 0) == 10);
		start_ok = 0;
		cbuf_free(cb);
		while (allocated != 0) {
			timed_event_block(cos_spd_id(), 1);
		}
	}
	printc("simple relinq block wake finish\n");

	thd_finish = 0;
	num_thd = 6;
	for(i=0; i<num_thd; i++) {
		/* parse a large time to run stress test */
		if (cos_thd_create(alloc_free_test, 0, sp.v, 0, 0) <= 0) {
			BUG();
		}
	}
	printc("master %d\n", cos_get_thd_id());
	while (1) {
		int cur_thd = num_thd-thd_finish;
		if (cur_thd == 0) break;
		int tmp = cbuf_debug_cbuf_info(cos_spd_id(), 5, 0);

		if (tmp == num_thd-thd_finish) {
			cbuf_mempool_resize(cos_spd_id(), cur_thd*CBUFPS*PAGE_SIZE);
			timed_event_block(cos_spd_id(), 3);
		} else {
			cbuf_mempool_resize(cos_spd_id(), cur_thd*CBUFPS*PAGE_SIZE/2);
			timed_event_block(cos_spd_id(), 1);
		}
	}
	printc("stress relinq block wake finish\n");
	cbuf_debug_profile(1);
}

#endif /* MULTI_H */
