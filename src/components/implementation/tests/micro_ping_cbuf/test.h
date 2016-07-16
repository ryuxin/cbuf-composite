#ifndef  TEST_H
#define  TEST_H

#include <basic.h>
void dispatch_test(int iter, int size, int id)
{
	u64_t start, end, start_tmp, end_tmp;
	int i, spdid = cos_spd_id();
	switch (id)
	{
	case 0:
		rdtscll(start);
		for (i = 0; i < iter ; i++) {
			bufs[i] = valloc_alloc(spdid, spdid, size);
			assert(bufs[i]);
		}
		rdtscll(end);
		printc("%d VALLOC alloc %d %llu cycles avg\n", iter, size, (end-start)/iter);
		rdtscll(start);
		for (i = 0; i < CBUFS ; i++) {
			valloc_free(cos_spd_id(), cos_spd_id(), bufs[i], size);
		}
		rdtscll(end);
		printc("%d VALLOC free %d %llu cycles avg\n", iter, size, (end-start)/iter);
		break;
	case 1:
		rdtscll(start);
		for (i = 0; i < iter ; i++) {
			bufs[i] = page_alloc(size);
			assert(bufs[i]);
		}
		rdtscll(end);
		printc("%d PAGE alloc %d %llu cycles avg\n", iter, size, (end-start)/iter);
		rdtscll(start);
		for (i = 0; i < iter ; i++) {
			page_free(bufs[i], size);
		}
		rdtscll(end);
		printc("%d PAGE free %d %llu cycles avg\n", iter, size, (end-start)/iter);
		break;
	case 2:
		rdtscll(start);
		for (i = 0; i < iter; i++) {
			bufs[i] = cbuf_alloc(size*CBUF_SZ, &cbids[i]);
			assert(bufs[i]);
		}
		rdtscll(end);
		printc("%d CBUF alloc slow %d %llu cycles avg\n", iter, size, (end-start)/iter);
		rdtscll(start);
		for (i = 0; i < iter; i++) {
			cbuf_free(cbids[i]);
		}
		rdtscll(end);
		printc("%d CBUF free %d %llu cycles avg\n", iter, size, (end-start)/iter);
		break;
	}
}
static void test_mbenchmark(void)
{
        /* VALLOC */
	dispatch_test(CBUFS, 1, 0);
	dispatch_test(CBUFS, 2, 0);
	dispatch_test(CBUFS, 2, 0);
	dispatch_test(CBUFS, 1, 0);
	printc("===================\n");
	/* PGAE ALLOC */
	dispatch_test(CBUFS, 1, 1);
	dispatch_test(CBUFS, 2, 1);
	dispatch_test(CBUFS, 2, 1);
	dispatch_test(CBUFS, 1, 1);
	printc("===================\n");
	/* CBUF ALLOC SLOW */
	memset(cbids, 0 , CBUFS*sizeof(cbuf_t));
	cbuf_mempool_resize(cos_spd_id(), 6*CBUFS*CBUF_SZ);
	dispatch_test(CBUFS, 1, 2);
	dispatch_test(CBUFS, 2, 2);
	dispatch_test(CBUFS, 2, 2);
	dispatch_test(CBUFS, 1, 2);
	printc("===================\n");
	/* CBUF ALLOC SLOW */
	memset(cbids, 0 , CBUFS*sizeof(cbuf_t));
	cbuf_mempool_resize(cos_spd_id(), 0);
	cbuf_mempool_resize(cos_spd_id(), 6*CBUFS*CBUF_SZ);
	dispatch_test(CBUFS, 1, 2);
	dispatch_test(CBUFS, 2, 2);
	dispatch_test(CBUFS, 2, 2);
	dispatch_test(CBUFS, 1, 2);
}

#endif /* TEST_H */
