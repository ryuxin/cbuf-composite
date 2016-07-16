#ifndef  BENCHMARK_H
#define  BENCHMARK_H
#include <basic.h>

static void 
fast_alloc_free(void)
{
#define total_loop_num 100000
#define inner_loop_num 20
	cbuf_t ids[inner_loop_num];
	char *addrs[inner_loop_num];
	int sz, i, j;
	u64_t start, end, alloc_tot, free_tot;
	for(sz = PAGE_SIZE; sz<=1024*PAGE_SIZE; sz *= 2) {
		alloc_tot = free_tot = 0;
		cbuf_mempool_resize(cos_spd_id(), sz*inner_loop_num);
		for(i=0; i<total_loop_num/inner_loop_num; i++) {
			for(j=0; j<inner_loop_num; j++) {
				addrs[j] = cbuf_alloc_ext(sz, &ids[j], CBUF_TMEM);
			}
			for(j=0; j<inner_loop_num; j++) {
				cbuf_free(ids[j]);
			}
			rdtscll(start);
			for(j=0; j<inner_loop_num; j++) {
				addrs[j] = cbuf_alloc(sz, &ids[j]);
				/* assert(addrs[j]); */
				/* addrs[j][0] = '$'; */
			}
			rdtscll(end);
			alloc_tot += (end-start);
			rdtscll(start);
			for(j=0; j<inner_loop_num; j++) {
				cbuf_free(ids[j]);
			}
			rdtscll(end);
			free_tot += (end-start);
		}
		cbuf_debug_clear_freelist(sz);
		printc("num pages %d fast alloc %llu\n", sz/PAGE_SIZE, alloc_tot/total_loop_num);
		printc("num pages %d fast free %llu\n", sz/PAGE_SIZE, free_tot/total_loop_num);
	}
}

static void
amortized_alloc_free(void)
{
#define total_loop_num 100000
	cbuf_t ids;
	char *addrs;
	int sz, i, j, num = 7;
	u64_t start, end, alloc_tot, free_tot;

	for(sz = PAGE_SIZE; sz<=512*PAGE_SIZE; sz *= 2) {
		alloc_tot = free_tot = 0;
		cbuf_mempool_resize(cos_spd_id(), sz);
		for(i=0; i<total_loop_num; i++) {
			rdtscll(start);
			addrs = cbuf_alloc(PAGE_SIZE, &ids);
			rdtscll(end);
			alloc_tot += (end-start);
			if (num) {
				cbuf_send(ids);
				call_cbuf_send(ids, PAGE_SIZE, num-1);
			}
			rdtscll(start);
			cbuf_free(ids);
			rdtscll(end);
			free_tot += (end-start);
		}
		cbuf_debug_clear_freelist(sz);
		printc("num pages %d amortized alloc %llu\n", sz/PAGE_SIZE, alloc_tot/total_loop_num);
		printc("num pages %d amortized free %llu\n", sz/PAGE_SIZE, free_tot/total_loop_num);
	}
}

static void 
cbuf_pipe_mbenchmark(void)
{
#define total_loop_num 100000
#define inner_loop_num 100
	cbuf_t ids, ret_id;
	char *addrs, *ret_addr;
	int sz, i, j;
	u64_t start, end, tot;

	for(sz = PAGE_SIZE; sz<=1024*PAGE_SIZE; sz *= 2) {
		tot = 0;
		cbuf_mempool_resize(cos_spd_id(), 0);
		cbuf_mempool_resize(cos_spd_id(), sz);
		call_cbuf_resize(sz);
		for(i=0; i<total_loop_num/inner_loop_num; i++) {
			addrs = (char *)cbuf_alloc(sz, &ids);
			call_pingpong_prepare(inner_loop_num, sz);
			rdtscll(start);
			for(j=0; j<inner_loop_num; j++) {
				addrs[0] = '$';
				cbuf_send(ids);
				ret_id = call_cbuf_pingpong(ids, sz);
				ret_addr = (char *)cbuf2buf(ret_id, sz);
				assert(ret_addr || ret_addr[0] == '&');
				cbuf_free(ret_id);
			}
			rdtscll(end);
			cbuf_free(ids);
			call_free(ret_id);
			tot += (end-start);
		}
		printc("num pages %d pipe round-trip %llu\n", sz/PAGE_SIZE, tot/total_loop_num);
	}
}

static void 
tmem_mbenchmark(void)
{
	u64_t start, end, start_tmp, end_tmp;
	int i, j;

	memset(cbids, 0 , CBUFS*sizeof(cbuf_t));
	cbuf_debug_clear_freelist(CBUF_SZ);
	cbuf_mempool_resize(cos_spd_id(), CBUFS*CBUF_SZ);
	cbuf_debug_profile(0);

        /* CACHING */
	rdtscll(start);
	for (i = 0; i < CBUFS ; i++) {
		bufs[i] = cbuf_alloc_ext(CBUF_SZ, &cbids[i], CBUF_TMEM);
		assert(bufs[i]);
	}
	rdtscll(end);
	printc("TMEM %d alloc slow %llu cycles avg\n", CBUFS, (end-start)/CBUFS);

	rdtscll(start);
	for (i = 0; i < CBUFS; i++) {
		cbuf_send(cbids[i]);
	}
	rdtscll(end);
	printc("TMEM %d send %llu cycles avg\n", CBUFS, (end-start)/CBUFS);
	rdtscll(start);
	for (i = 0; i < CBUFS; i++) {
		simple_call_buf2buf(cbids[i], CBUF_SZ);
	}
	rdtscll(end);
	printc("TMEM %d inv cbuf2buf miss %llu cycles avg\n", CBUFS, (end-start)/CBUFS);

	rdtscll(start);
	for (i = 0; i < CBUFS; i++) {
		cbuf_free(cbids[i]);
	}
	rdtscll(end);
	printc("TMEM %d free %llu cycles avg\n", CBUFS, (end-start)/CBUFS);
	for (i = 0; i < CBUFS; i++) {
		call_free(cbids[i]);
	}

        /* CBUF_ALLOC  */
	rdtscll(start);
	for (i = 0; i < CBUFS; i++) {
		bufs[i] = cbuf_alloc_ext(CBUF_SZ, &cbids[i], CBUF_TMEM);
		assert(bufs[i]);
	}
	rdtscll(end);
	printc("TMEM %d fast alloc_cbuf %llu cycles avg\n", CBUFS, (end-start)/CBUFS);

        /* CBUF2BUF  */
	rdtscll(start);
	for (i = 0; i < CBUFS; i++) {
		cbuf_send(cbids[i]);
	}
	rdtscll(end);
	printc("TMEM %d send %llu cycles avg\n", CBUFS, (end-start)/CBUFS);
	rdtscll(start);
	for (i = 0; i < CBUFS; i++){
		simple_call_buf2buf(cbids[i], CBUF_SZ);
	}
	rdtscll(end);
	printc("TMEM %d inv cbuf2buf %llu cycles avg\n", CBUFS, (end-start)/CBUFS);

        /* CBUF_FREE  */
	rdtscll(start);
	for (i = 0; i < CBUFS; i++){
		cbuf_free(cbids[i]);
	}
	rdtscll(end);
	printc("TMEM %d free %llu cycles avg\n", CBUFS, (end-start)/CBUFS);
	for (i = 0; i < CBUFS; i++) {
		call_free(cbids[i]);
	}

        /* CBUF_ALLOC-CBUF2BUF-CBUF_FREE */
	start = 0;
	for (j = 1; j<101; j++) {
		rdtscll(start_tmp);
		for (i = 0; i < 100; i++){
			bufs[0] = cbuf_alloc_ext(CBUF_SZ, &cbids[0], CBUF_TMEM);
			cbuf_send(cbids[0]);
			call_cbuf2buf(cbids[0], CBUF_SZ);
			cbuf_free(cbids[0]);
		}
		rdtscll(end_tmp);
		start += (end_tmp-start_tmp);
		bufs[j] = cbuf_alloc_ext(CBUF_SZ, &cbids[j], CBUF_TMEM);
	}
	for(j=1; j<101; j++) cbuf_free(cbids[j]);
	printc("TMEM %d alloc-cbuf2buf-free %llu cycles avg\n", 100*100, start/(100*100));

	printc("on freelist %d\n", cbuf_debug_freelist_num(CBUF_SZ));
	rdtscll(start);
	for (i = 0; i < CBUFS; i++) {
		bufs[i] = cbuf_alloc(CBUF_SZ, &cbids[i]);
		assert(bufs[i]);
	}
	rdtscll(end);
	printc("CBUF %d fast alloc_cbuf %llu cycles avg\n", CBUFS, (end-start)/CBUFS);
	for (i = 0; i < CBUFS; i++) {
		cbuf_free(cbids[i]);
	}
	printc("===========debug=========\n");
	cbuf_debug_cbuf_info(cos_spd_id(), 2, 1);
	cbuf_debug_profile(1);
	printc("=========================\n");
}

static void cbuf_dispatch_mbenchmark(int size)
{
	unsigned long long start, end;
	int i;

	memset(cbids, 0 , CBUFS*sizeof(cbuf_t));
	cbuf_debug_clear_freelist(size*CBUF_SZ);
	cbuf_mempool_resize(cos_spd_id(), size*CBUFS*CBUF_SZ);
	cbuf_debug_profile(0);

	rdtscll(start);
	for (i = 0; i < CBUFS; i++) {
		bufs[i] = cbuf_alloc(size*CBUF_SZ, &cbids[i]);
		assert(bufs[i]);
	}
	rdtscll(end);
	printc("CBUF %d alloc slow %d %llu cycles avg\n", CBUFS, size, (end-start)/CBUFS);

	rdtscll(start);
	for (i = 0; i < CBUFS; i++) {
		cbuf_send(cbids[i]);
		call_cbuf2buf(cbids[i], size*CBUF_SZ);
	}
	rdtscll(end);
	printc("CBUF %d send inv cbuf2buf miss %d %llu cycles avg\n", CBUFS, size, (end-start)/CBUFS);

	rdtscll(start);
	for (i = 0; i < CBUFS; i++) {
		cbuf_free(cbids[i]);
	}
	rdtscll(end);
	printc("CBUF %d free %d %llu cycles avg\n", CBUFS, size, (end-start)/CBUFS);

	rdtscll(start);
	for (i = 0; i < 511; i++) {
		bufs[i] = cbuf_alloc(size*CBUF_SZ, &cbids[i]);
		assert(bufs[i]);
	}
	rdtscll(end);
	printc("CBUF %d alloc collect amortized %d %llu cycles avg\n", 511, size, (end-start)/511);

	bufs[i] = cbuf_alloc(size*CBUF_SZ, &cbids[i]);
	rdtscll(start);
	for (i = 512 ; i < CBUFS; i++) {
		bufs[i] = cbuf_alloc(size*CBUF_SZ, &cbids[i]);
		assert(bufs[i]);
	}
	rdtscll(end);
	printc("CBUF %d alloc fast %d %llu cycles avg\n", CBUFS-512, size, (end-start)/(CBUFS-512));

	for (i = 0 ; i < CBUFS; i++) {
		cbuf_send(cbids[i]);
		call_cbuf2buf(cbids[i], size*CBUF_SZ);
	}
	rdtscll(start);
	for (i = 0 ; i < CBUFS; i++) {
		cbuf_send(cbids[i]);
		call_cbuf2buf(cbids[i], size*CBUF_SZ);
	}
	rdtscll(end);
	printc("CBUF %d inv cbuf2buf %d %llu cycles avg\n", CBUFS, size, (end-start)/CBUFS);

	rdtscll(start);
	for (i = 0 ; i < CBUFS; i++) {
		cbuf_free(cbids[i]);
	}
	rdtscll(end);
	printc("CBUF %d free %d %llu cycles avg\n", CBUFS, size, (end-start)/CBUFS);

	rdtscll(start);
	bufs[0] = cbuf_alloc(size*CBUF_SZ, &cbids[0]);
	cbuf_send_free(cbids[0]);
	call_cbuf2buf(cbids[0], size*CBUF_SZ);
	assert(bufs[0]);
	rdtscll(end);
	printc("CBUF %d collect-cbuf2buf-send/free %d %llu cycles avg\n", 1, size, end-start);

	rdtscll(start);
	for (i = 1 ; i < 511 ; i++) {
		bufs[i] = cbuf_alloc(size*CBUF_SZ, &cbids[i]);
		cbuf_send_free(cbids[i]);
		call_cbuf2buf(cbids[i], size*CBUF_SZ);
		assert(bufs[i]);
	}
	rdtscll(end);
	printc("CBUF %d fast alloc-cbuf2buf-send/free %d %llu cycles avg\n", 510, size, (end-start)/510);

	cbuf_debug_clear_freelist(size*CBUF_SZ);
	cbuf_mempool_resize(cos_spd_id(), size*CBUFS*CBUF_SZ);
	rdtscll(start);
	for (i = 0; i < CBUFS; i++) {
		bufs[i] = cbuf_alloc(size*CBUF_SZ, &cbids[i]);
		cbuf_send_free(cbids[i]);
		call_cbuf2buf(cbids[i], size*CBUF_SZ);
		assert(bufs[i]);
	}
	rdtscll(end);
	printc("CBUF %d slow alloc-cbuf2buf miss-send/free %d %llu cycles avg\n", CBUFS, size, (end-start)/CBUFS);

	/* clear pong's virtual address space */
	cbuf_mempool_resize(cos_spd_id(), 0);
	call_cbuf_resize(CBUFS*size*CBUF_SZ);
	for (i = 0; i < CBUFS; i++) {
		cbids[i] = call_cbuf_alloc(size*CBUF_SZ);
	}
	rdtscll(start);
	for (i = 0; i < CBUFS; i++) {
		bufs[i] = cbuf2buf(cbids[i], size*CBUF_SZ);
		assert(bufs[i][0] == '$');
	}
	rdtscll(end);
	printc("CBUF %d cbuf2buf miss %d %llu cycles avg\n", CBUFS, size, (end-start)/CBUFS);

	rdtscll(start);
	for (i = 0; i < CBUFS; i++) {
		bufs[i] = cbuf2buf(cbids[i], size*CBUF_SZ);
	}
	rdtscll(end);
	printc("CBUF %d cbuf2buf fast %d %llu cycles avg\n", CBUFS, size, (end-start)/CBUFS);
	for (i = 0; i < CBUFS; i++) {
		cbuf_free(cbids[i]);
		cbuf_free(cbids[i]);
	}
	printc("===========debug=========\n");
	cbuf_debug_profile(1);
	printc("=========================\n");
}

#endif /*BENCHMARK_H */
