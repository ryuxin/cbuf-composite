#include <cos_component.h>
#include <cos_alloc.h>
#include <print.h>
#include <valloc.h>
#include <cbuf.h>
#include <cbuf_mgr.h>
#include <sched.h>
#include <timed_blk.h>
#include <periodic_wake.h>
#define RUN_TIME 25
#define LAST_TIME 5
#define IDLE_TIME 25
#define MAX_CBUFS 200
#define MIN_CBUFS 20
#define PERIODICITY 10
#define UNTIL 20
#define ROUND 6

int tot_sz[IDLE_TIME+RUN_TIME+5] = {
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
0, 0, 0, 0, 0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 
64, 64, 64, 64, 64, 64, 64, 
64, 64, 64, 50, 40, 30, 20, 10};
int tot_num = 100000, cur_time=0, round = 0;

void update_time(void)
{
	while(1) {
		cur_time++;
		round = 0;
		timed_event_block(cos_spd_id(), 100);
	}
}

cbuf_t * cbuf_alloc_50Page(void)
{
	int i = 0;
	cbuf_t *cbids;
	cbids = (cbuf_t *)malloc(ROUND*sizeof(cbuf_t));
	cbuf_alloc(2*PAGE_SIZE, &cbids[i++]);
	cbuf_alloc(4*PAGE_SIZE, &cbids[i++]);
	cbuf_alloc(4*PAGE_SIZE, &cbids[i++]);
	cbuf_alloc(8*PAGE_SIZE, &cbids[i++]);
	cbuf_alloc(16*PAGE_SIZE, &cbids[i++]);
	cbuf_alloc(16*PAGE_SIZE, &cbids[i++]);
	return cbids;
}

void cbuf_free_50Page(cbuf_t *cbids)
{
	int i;
	for(i=0; i<ROUND; i++) {
		cbuf_free(cbids[i]);
	}
	free(cbids);
}

unsigned long long cbuf_interf(int t, int *r)
{
	int i, n;
	cbuf_t *cbids;
	unsigned long long start, end;
	rdtscll(start);

	n = tot_sz[t];
	cbids = (cbuf_t *)malloc(n*sizeof(cbuf_t));
	for(i=0; i<n; i++) {
		cbuf_alloc(PAGE_SIZE, &cbids[i]);
	}
	for(i=0; i<n; i++) {
		cbuf_free(cbids[i]);
	}
	*r = n;
	rdtscll(end);
	return end-start;
}

void run_interference(void)
{
	int sz=500, tot = 0, r;
	char *buf;
	cbuf_t cbid;
	unsigned long long exe_tot = 0;

	timed_event_block(cos_spd_id(), IDLE_TIME*100);
	printc("interference thd %d\n", cos_get_thd_id());
	periodic_wake_create(cos_spd_id(), PERIODICITY);
	while (1) {
		if (!tot_sz[cur_time]) {
			printc("interference thd finish time %d\n", cur_time);
			timed_event_block(cos_spd_id(), IDLE_TIME*100);
		}
		exe_tot += cbuf_interf(cur_time, &r);
		tot += r;
		if (tot > tot_num) {
			/* printc("interf round %d tot alloc %d cyc %llu sz %d\n", ++round, tot, exe_tot, tot_sz[cur_time]); */
			/* int tmepsz; */
			/* for(tmepsz=8; tmepsz<128; tmepsz=tmepsz*2) */
			/* 	printc("freelist %d num %d\n", tmepsz, cbuf_debug_freelist_num(tmepsz*PAGE_SIZE)); */
			tot = 0;
			exe_tot = 0;
			periodic_wake_wait(cos_spd_id());
			/* timed_event_block(cos_spd_id(), PERIODICITY*(100-UNTIL)/100); */
		}
	}
}

void
cos_init(void)
{
	return ;
	static int first = 1;
	int i, s;
	union sched_param sp;

	if (first) {
		void *vaddr[4];
		int i;
		for (i=0; i<3; i++) {
			vaddr[i] = valloc_alloc(cos_spd_id(), cos_spd_id(), 100*256);
			assert(vaddr[i]);
		}
		for (i=0; i<3; i++) {
			valloc_free(cos_spd_id(), cos_spd_id(), vaddr[i], 100*256);
		}
		sp.c.type  = SCHEDP_PRIO;
		sp.c.value = 6;
		if (cos_thd_create(update_time, 0, sp.v, 0, 0) <= 0) {
			BUG();
		}
		sp.c.type  = SCHEDP_PRIO;
		sp.c.value = 8;
		if (cos_thd_create(run_interference, 0, sp.v, 0, 0) <= 0) {
			BUG();
		}
		first = 0;
	} else {
		printc("Error cbuf interference init more than once\n");
	}
	return ;
}
