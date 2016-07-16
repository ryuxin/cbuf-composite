#include <basic.h>
#include <m_benchmark.h>
#include <test.h>

static void cbuf_mbenchmark()
{
	cbuf_dispatch_mbenchmark(1);
	cbuf_dispatch_mbenchmark(2);
	cbuf_dispatch_mbenchmark(4);
}

void 
cos_init(void)
{
	void *vaddr;
	vaddr = valloc_alloc(cos_spd_id(), cos_spd_id(), 20*1024);
	valloc_free(cos_spd_id(), cos_spd_id(), vaddr, 20*1024);
	printc("\nMICRO BENCHMARK TEST (PINGPONG WITH CBUF & CBUFP)\n");

	/* Benchmark for basic atomic instructions and component invocation */
	basic_benchmark();
	fast_alloc_free();
	amortized_alloc_free();
	cbuf_pipe_mbenchmark();
	tmem_mbenchmark();
	cbuf_mbenchmark();

	/* valloc, page_alloc and cbuf_alloc slow path test */
//	test_mbenchmark();
	printc("\nMICRO BENCHMARK TEST (PINGPONG WITH CBUF & CBUFP) DONE!\n\n");
	return;
}


