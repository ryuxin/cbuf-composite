#ifndef  BASIC_H
#define  BASIC_H

#include <cos_component.h>
#include <print.h>
#include <valloc.h>
#include <cos_alloc.h>
#include <mem_mgr_large.h>
#include <micro_pong.h>
#include <cbuf.h>

#define ITERS 10000
#define CBUFS 1000
#define CBUF_SZ PAGE_SIZE

cbuf_t cbids[CBUFS], p3[CBUFS];
char *bufs[CBUFS], *buf3[CBUFS];
char zero[CBUF_SZ], zer2[CBUF_SZ];

void basic_benchmark()
{
	u64_t start, end, start_tmp, end_tmp;
	u8_t nsent = 0;
	int i, refcnt = 0;
        /* RDTSCLL */
	rdtscll(start_tmp);
	for (i = 0 ; i < ITERS; i++) {
		rdtscll(start);
	}
	rdtscll(end_tmp);
	printc("%d rdtscll avg %lld cycles avg\n", ITERS, (end_tmp-start_tmp)/ITERS);

	rdtscll(start_tmp);
	start = 0;
	for (i = 0 ; i < ITERS; i++) {
		cos_dcas(&start, start, start);
	}
	rdtscll(end_tmp);
	printc("%d dcas avg %lld cycles avg\n", ITERS, (end_tmp-start_tmp)/ITERS);

	rdtscll(start_tmp);
	for (i = 0 ; i < ITERS; i++) {
		cos_faa_byte(&nsent, 1);
	}
	rdtscll(end_tmp);
	printc("%d byte faa avg %lld cycles avg\n", ITERS, (end_tmp-start_tmp)/ITERS);

	rdtscll(start_tmp);
	for (i = 0 ; i < ITERS; i++) {
		cos_faa(&refcnt, 1);
	}
	rdtscll(end_tmp);
	printc("%d word faa avg %lld cycles avg\n", ITERS, (end_tmp-start_tmp)/ITERS);

	rdtscll(start_tmp);
	nsent = refcnt = 0;
	for (i = 0 ; i < ITERS; i++) {
		cos_faa(&refcnt, 1);
		cos_mem_fence();
		cos_faa_byte(&nsent, 1);
	}
	rdtscll(end_tmp);
	printc("%d faa fense faa avg %lld cycles avg\n", ITERS, (end_tmp-start_tmp)/ITERS);

	rdtscll(start_tmp);
	nsent = refcnt = 0;
	for (i = 0 ; i < ITERS; i++) {
		cos_faa(&refcnt, 1);
		asm volatile("" ::: "memory");
		cos_faa_byte(&nsent, 1);
	}
	rdtscll(end_tmp);
	printc("%d faa compiler barrier faa avg %lld cycles avg\n", ITERS, (end_tmp-start_tmp)/ITERS);

	rdtscll(start_tmp);
	start = 0;
	for (i = 0 ; i < ITERS; i++) {
		cos_mem_fence();
	}
	rdtscll(end_tmp);
	printc("%d mem fence avg %lld cycles avg\n", ITERS, (end_tmp-start_tmp)/ITERS);

	rdtscll(start_tmp);
	for (i = 0 ; i < ITERS; i++) {
		memset(zero, '$', CBUF_SZ);
	}
	rdtscll(end_tmp);
	printc("%d zero page avg %lld cycles avg\n", ITERS, (end_tmp-start_tmp)/ITERS);

	rdtscll(start_tmp);
	for (i = 0 ; i < ITERS; i++) {
		memcpy(zer2, zero, CBUF_SZ);
	}
	rdtscll(end_tmp);
	printc("%d memcpy page avg %lld cycles avg\n", ITERS, (end_tmp-start_tmp)/ITERS);

        /* PINGPONG */
	for (i = 0 ; i < ITERS; i++) {
		call();
	}
	rdtscll(start);
	for (i = 0 ; i < ITERS; i++) {
		call();
	}
	rdtscll(end);
	printc("%d inv w/o cbuf avg %lld  cycles avg\n", ITERS, (end-start)/ITERS);
	printc("===================\n");
}

#endif /*BASIC_H */
