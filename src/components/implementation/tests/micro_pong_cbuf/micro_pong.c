#include <cos_component.h>
#include <print.h>
#include <sched.h>
#include <valloc.h>
#include <cbuf.h>
#include <micro_pong.h>

//#define VERBOSE
#ifdef VERBOSE
#define printv(fmt,...) printc(fmt, ##__VA_ARGS__)
#else
#define printv(fmt,...) 
#endif

extern void next_call_cbuf_send(cbuf_t cb, unsigned long sz, int num);

void call(void)
{
	return;
}

int simple_call_buf2buf(cbuf_t cb, int len) 
{
	char *b;
	b = cbuf2buf(cb,len);
	assert(b);
	return 0;
}

int call_cbuf2buf(cbuf_t cb, int len)
{
	char *b;
	b = cbuf2buf(cb, len);
	assert(b);
	cbuf_free(cb);
	return 0;
}

int call_free(cbuf_t cb)
{
	cbuf_free(cb);
	return 0;
}

cbuf_t call_cbuf_alloc(unsigned long sz)
{
	cbuf_t cbuf;
	char *addr;
	addr = cbuf_alloc(sz, &cbuf);
	assert(addr && cbuf);
	addr[0] = '$';
	cbuf_send(cbuf);
	cbuf_send_free(cbuf);
	return cbuf;
}

cbuf_t global_id;
char *global_addr;

void call_pingpong_prepare(int num, int sz)
{
	global_addr = cbuf_alloc(sz, &global_id);

}

cbuf_t call_cbuf_pingpong(cbuf_t cb, unsigned long sz)
{
	char *b;

	b = (char *)cbuf2buf(cb, sz);
	assert(b && b[0] == '$');
	cbuf_free(cb);
	global_addr[0] = '&';
	cbuf_send(global_id);
	return global_id;
}

void call_cbuf_send(cbuf_t cb, unsigned long sz, int num)
{
	char *b;

	b = (char *)cbuf2buf(cb, sz);
	if (!num) cbuf_free(cb);
	else {
		cbuf_send_free(cb);
		next_call_cbuf_send(cb, sz, num-1);
		return ;
	}
}

void call_cbuf_resize(unsigned long sz)
{
	cbuf_mempool_resize(cos_spd_id(), 0);
	cbuf_mempool_resize(cos_spd_id(), sz);
}

void call_cbuf_debug()
{
	cbuf_debug_cbuf_info(cos_spd_id(), 1, 1);
}

void call_cs(void)
{
	static int first = 0;
	static int high, low;
	u64_t start = 0, end = 0;

	if(first == 1){
		low = cos_get_thd_id();
		sched_wakeup(cos_spd_id(), high);
	}

	if(first == 0){
		first = 1;
		high = cos_get_thd_id();
		sched_block(cos_spd_id(), 0);
		rdtscll(start);
		sched_block(cos_spd_id(), low);
	}

	if (cos_get_thd_id() == low) {
		sched_wakeup(cos_spd_id(), high);
	}

	if (cos_get_thd_id() == high) {
		rdtscll(end);
		printc("context switch cost: %llu cycs\n", (end-start) >> 1);
		first = 0;
	}
	return;
}

void 
cos_init(void)
{
	void *vaddr = NULL;
	vaddr = valloc_alloc(cos_spd_id(), cos_spd_id(), 20*1024);
	assert(vaddr);
	valloc_free(cos_spd_id(), cos_spd_id(), vaddr, 20*1024);
}
