#include <cos_component.h>
#include <cos_alloc.h>
#include <print.h>
#include <valloc.h>
#include <cbuf.h>
#include <cbuf_mgr.h>
#include <sched.h>
#include <timed_blk.h>
#include <periodic_wake.h>

#define MAX_COST 0
#define TOT_COST 1
#define POLICY_4 1

#if MAX_COST
#define BLK_IDX 8
#define GC_IDX 10
#endif

#if TOT_COST
#define BLK_IDX 7
#define GC_IDX 9
#endif

#define POLICY_PERIODICITY 25
#define COM_NUM 5
struct cbuf_pool {
	spdid_t spdid;
	unsigned long target_size, used, min;
	int priority, weight, period;
	unsigned long blk, gc, blk_sz;
	unsigned long old_blk, old_gc;
	unsigned long long old_blk_gc;
	unsigned long old_avg_cost;
	unsigned long old_target;
};
struct cbuf_pool comps[COM_NUM+6];
int tot_cbuf = 300*PAGE_SIZE;
int period_num = 0;
unsigned long ws_exe_time, tot_ws_gc;
unsigned long long interf_gc_blk = 0;

static inline void
adjust_sz_limit(unsigned long *target, int num, unsigned long total)
{
	double ratio;
	unsigned long tot = 0;
	int i;

	for(i=0; i<num; i++) tot += target[i];
	if (tot > total) {
		for(i=0; i<num; i++) {
			ratio = (double)target[i]/(double)tot;
			target[i] = round_to_page((unsigned long)(total*ratio));
		}
	}
}

static inline void
adjust_sz_used(unsigned long *target)
{
	int i, more = 0;
	unsigned long tot = 0, less = 0;

	for(i=0; i<COM_NUM; i++) {
		if (target[i] < comps[i].min) {
			tot += (comps[i].min-target[i]);
			target[i] = comps[i].min;
		}
		if (target[i] < comps[i].used) {
			tot += (comps[i].used-target[i]);
		}
		if (target[i] > comps[i].target_size) {
			more++;
		}
	}
	if (more) less = tot/more;
	for(i=0; i<COM_NUM; i++) {
		if (target[i] > comps[i].target_size) {
			target[i] -= less;
		}
	}
}

static inline void  
adjust_sz_priority(unsigned long *target)
{
	int i;
	unsigned long tot = 0, nsz;

	for(i=0; i<COM_NUM-1; i++) tot += target[i];
	if (tot+target[COM_NUM-1] <= tot_cbuf) return ;
	nsz = target[COM_NUM-1];
	if (tot <= tot_cbuf) {
		target[COM_NUM-1] = tot_cbuf-tot;
		nsz -= target[COM_NUM-1];
	} else {
		target[COM_NUM-1] = 0;
		adjust_sz_limit(target, COM_NUM-1, tot_cbuf);
	}

}

static inline void
apply_target(unsigned long *target)
{
	int i;
	for(i=0; i<COM_NUM; i++) {
		comps[i].old_target = comps[i].target_size;
	}
	for(i=0; i<COM_NUM; i++) {
		target[i] = round_to_page((unsigned long)(target[i]));
	}
	for(i=0; i<COM_NUM; i++) {
		if (target[i] != comps[i].target_size) {
			cbuf_mempool_resize(comps[i].spdid, target[i]);
		}
	}
}

static inline unsigned long
block_req_sz(int idx)
{
	unsigned long req_sz;
	req_sz = cbuf_debug_cbuf_info(comps[idx].spdid, 6, 0);
	if (!req_sz) {
		comps[idx].weight = 1;
	} else {
		req_sz *= (1<<comps[idx].weight);
		if (comps[idx].weight < 10) comps[idx].weight++;
	}
	return req_sz;
}

static inline unsigned long
gc_req_sz(int idx, unsigned long long oa, 
	  unsigned long long na, int block)
{
	unsigned long nsz = comps[idx].target_size;
	unsigned long req_sz;
	double tra = 0;
	tra = (double)na/(double)oa;
	req_sz = block_req_sz(idx);
	comps[idx].blk_sz = req_sz;
	if (!comps[idx].blk) return nsz;
	if (req_sz) {
		nsz = comps[idx].target_size+req_sz;
	} else if (comps[idx].old_blk) {
		if (comps[idx].old_target >= comps[idx].target_size) {
			nsz = (unsigned long)(comps[idx].target_size*tra);
		} else {
			nsz = 2*comps[idx].target_size-comps[idx].old_target;
		}
	} else {
		if (comps[idx].old_target > comps[idx].target_size) {
			nsz = comps[idx].old_target;
		}
		else {
			nsz = comps[idx].target_size*(1<<comps[idx].weight);
		}
	}
ret:
	/* printc("cbuf policy spd %d na %llu oa %llu ratio %lf large %d old %d target %d new %d block %d cur blk %d old blk %d weight %d\n", comps[idx].spdid, na, oa, tra, na>oa, comps[idx].old_target/PAGE_SIZE, comps[idx].target_size/PAGE_SIZE, nsz/PAGE_SIZE, req_sz/PAGE_SIZE, comps[idx].blk, comps[idx].old_blk, 1<<comps[idx].weight); */
	return nsz;
}

void predict_nsz(unsigned long *nsz, int s, int e)
{
	int i;
	unsigned long req_sz, alloc;
	unsigned long long oa, na;

	for(i=s; i<e; i++) {
		nsz[i] = comps[i].target_size;
		alloc = cbuf_debug_cbuf_info(comps[i].spdid, 1, 0);
		comps[i].blk = cbuf_debug_cbuf_info(comps[i].spdid, BLK_IDX, 0);
		comps[i].gc  = cbuf_debug_cbuf_info(comps[i].spdid, GC_IDX, 0);
		na = (unsigned long long)comps[i].blk+(unsigned long long)comps[i].gc;
		if (i == COM_NUM-1) interf_gc_blk += na;
		if (!alloc || !na) continue;
		comps[i].period++;
		oa = comps[i].old_blk_gc;
		if (i<COM_NUM-1) tot_ws_gc += (unsigned long)na;
		/* printc("cbuf policy spd %d blk %d gc %d oa %llu na %llu target %lu\n", comps[i].spdid, comps[i].blk, comps[i].gc, oa, na, nsz[i]/PAGE_SIZE); */
		if (oa) {
			nsz[i] = gc_req_sz(i, oa, na, comps[i].blk);
		} else {
			req_sz = block_req_sz(i);
			nsz[i] = req_sz+comps[i].target_size;
		}
		comps[i].old_blk_gc = na;
		comps[i].old_blk    = comps[i].blk;
		comps[i].old_gc     = comps[i].gc;
	}
}

#ifdef POLICY_1
void
policy(void)
{
	unsigned long nsz[COM_NUM];

	predict_nsz(nsz, 0, COM_NUM);
	apply_target(nsz);
	double tra = (double)tot_ws_gc/(double)ws_exe_time;
	printc("policy ws tot %lu gc %lu prec %lf\n", ws_exe_time, tot_ws_gc, tra*100);
	return ;
}
#endif

#ifdef POLICY_2
void
policy(void)
{
	unsigned long nsz[COM_NUM];

	predict_nsz(nsz, 0, COM_NUM);
	adjust_sz_limit(nsz, COM_NUM, tot_cbuf);
	adjust_sz_used(nsz);
	apply_target(nsz);
	double tra = (double)tot_ws_gc/(double)ws_exe_time;
	printc("policy ws tot %lu gc %lu prec %lf\n", ws_exe_time, tot_ws_gc, tra*100);
	return ;
}

#endif

#ifdef POLICY_3
void
policy(void)
{
	unsigned long nsz[COM_NUM];

	predict_nsz(nsz, 0, COM_NUM);
	adjust_sz_priority(nsz);
	adjust_sz_used(nsz);
	apply_target(nsz);
	double tra = (double)tot_ws_gc/(double)ws_exe_time;
	printc("policy ws tot %lu gc %lu prec %lf\n", ws_exe_time, tot_ws_gc, tra*100);
	return ;
}
#endif

#ifdef POLICY_4

double target_size[3];
unsigned long total_cnt[3];
double max_small = 0, min_large;
static inline int
size_equal(unsigned long a, unsigned long b)
{
	return a/PAGE_SIZE == b/PAGE_SIZE;
}

void
policy(void)
{
	unsigned long req_sz = 0, alloc, nsz[COM_NUM];
	unsigned long qos_time, left;
	int i, max = 0, n;
	double tra, qos=25, share = 0, s_5, l_5, nsize = 0;
	unsigned long long oa = 0, na[COM_NUM];
	s_5 = qos-5;
	l_5 = qos+5;

	tot_ws_gc = 0;
	alloc = cbuf_debug_cbuf_info(comps[0].spdid, 1, 0);
	for(i=0; i<COM_NUM-1; i++) {
		nsz[i] = comps[i].target_size;
		comps[i].blk = cbuf_debug_cbuf_info(comps[i].spdid, BLK_IDX, 0);
		comps[i].gc  = cbuf_debug_cbuf_info(comps[i].spdid, GC_IDX, 0);
		comps[i].period++;
		na[i] = (unsigned long long)comps[i].blk+(unsigned long long)comps[i].gc;
		tot_ws_gc += (unsigned long)na[i];
	}
	predict_nsz(nsz, COM_NUM-1, COM_NUM);
	if (!tot_ws_gc || !(comps[0].blk+comps[0].gc)) goto ret;
	tra = (double)tot_ws_gc/(double)ws_exe_time*100;
	if (-2.5 <= tra-qos && tra-qos <= 2.5) {
		total_cnt[1]++;
		target_size[1] = target_size[1]+(double)comps[0].target_size/total_cnt[1]-target_size[1]/total_cnt[1];
		nsz[0] = round_to_page((unsigned long)target_size[1]+PAGE_SIZE/2);
	} else {
		if (qos<2) {
			nsz[0] = 2*comps[0].target_size;
			goto ret;
		}
		qos_time = (unsigned long)((double)ws_exe_time/100*qos);
		left = tot_ws_gc-na[0];
		qos_time -= left;
		if (tra < qos) {
			total_cnt[0]++;
			target_size[0] = target_size[0] + (double)comps[0].target_size/total_cnt[0]-target_size[0]/total_cnt[0];
			if (target_size[0] > max_small) max_small = target_size[0];
			if (!size_equal((unsigned long)target_size[1], comps[0].target_size) 
			    && comps[0].target_size < target_size[1]) {
				total_cnt[1] = target_size[1] = 0;
			}

			if (target_size[2] == 0) {
				share  = (double)na[0]/(double)qos_time;
				nsz[0] = comps[0].target_size*share;
			} else {
				if (size_equal((unsigned long)target_size[2], comps[0].target_size) ||
				    comps[0].target_size < target_size[2]) {
					target_size[2] = min_large;
					total_cnt[2] = 1;
					total_cnt[1] = target_size[1] = 0;
				}
				n = (!!target_size[1])+2;
				nsize= ((double)comps[0].target_size+target_size[1]+target_size[2])/n;
				nsz[0] = round_to_page((unsigned long)nsize);
			}
		} else {
			total_cnt[2]++;
			target_size[2] = target_size[2] + (double)comps[0].target_size/total_cnt[2]-target_size[2]/total_cnt[2];
			if (target_size[2] < min_large) min_large = target_size[2];
			if (size_equal((unsigned long)max_small, comps[0].target_size) ||
			    comps[0].target_size > max_small) {
				max_small *= 2;
			}
			if (!size_equal((unsigned long)target_size[1], comps[0].target_size) 
			    && comps[0].target_size > target_size[1]) {
				total_cnt[1] = target_size[1] = 0;
			}
			if (target_size[0] == 0) {
				share  = (double)na[0]/(double)qos_time;
				nsz[0] = comps[0].target_size*share;
			} else {
				if (size_equal((unsigned long)target_size[0], comps[0].target_size) ||
				    comps[0].target_size > target_size[0]) {
					target_size[0] = max_small;
					total_cnt[0] = 1;
					total_cnt[1] = target_size[1] = 0;
				}
				n = (!!target_size[1])+2;
				nsize = ((double)comps[0].target_size+target_size[1]+target_size[0])/n;
				nsz[0] = round_up_to_page((unsigned long)nsize);
			}
		}
	}
ret:
	printc("tot %lu tgc %lu blk %lu small %lu large %lu\n", ws_exe_time, tot_ws_gc, comps[0].blk, (unsigned long)max_small/PAGE_SIZE, (unsigned long)min_large/PAGE_SIZE);
	printc("tra %lf cur %lu new %lu nsize %lf tar %lf s_5 %lf l_5 %lf\n", tra, comps[0].target_size/PAGE_SIZE, nsz[0]/PAGE_SIZE, nsize, target_size[1]/PAGE_SIZE, target_size[0]/PAGE_SIZE, target_size[2]/PAGE_SIZE);
	req_sz = tot_cbuf;
	for(i=0; i<COM_NUM-1; i++) {
		if (nsz[i] < comps[i].min) nsz[i] = comps[i].min;
		req_sz -= nsz[i];
	}
	nsz[COM_NUM-1] = req_sz;
	adjust_sz_used(nsz);
	apply_target(nsz);
}
#endif

void
run_policy(void)
{
	int i, req, t1, t2;

	printc("policy thd %d begin to run\n", cos_get_thd_id());
	timed_event_block(cos_spd_id(), POLICY_PERIODICITY*20);
	periodic_wake_create(cos_spd_id(), POLICY_PERIODICITY);
	ws_exe_time = sched_ws_cycles();
	while (1) {
		for(i=0; i<COM_NUM; i++) {
			cbuf_debug_cbuf_info(comps[i].spdid, BLK_IDX, 2);
		}
		periodic_wake_wait(cos_spd_id());
		ws_exe_time = sched_ws_cycles();
		tot_ws_gc = 0;
		period_num++;
		for (t1 = 0, i=0; i<COM_NUM-1; i++) {
			t1 += cbuf_debug_cbuf_info(comps[i].spdid, 1, 0);
		}
		t2 = cbuf_debug_cbuf_info(comps[COM_NUM-1].spdid, 1, 0);
		if (period_num%4 == 0) {
			printc("cbuf policy period %d mem1 %d mem2 %d tot mem3 %d interf %llu\n", period_num/4, t1/PAGE_SIZE, t2/PAGE_SIZE, (t1+t2)/PAGE_SIZE, interf_gc_blk);
			interf_gc_blk = 0;
		}
		for(i=0; i<COM_NUM; i++) {
			comps[i].target_size = cbuf_memory_target_get(comps[i].spdid);
			comps[i].used = cbuf_debug_cbuf_info(comps[i].spdid, 2, 0);
		}
		policy();
#ifndef POLICY_4
		if (period_num == 1) {
			for(i=0; i<COM_NUM; i++) {
				if (i==3) continue;
				cbuf_mempool_resize(comps[i].spdid, (tot_cbuf-comps[3].min)/(COM_NUM-1));
			}
			cbuf_mempool_resize(comps[3].spdid, comps[3].min);
		}
#endif
	}
	return ;
}

void
cos_init(void)
{
	return ;
	static int first = 1;
	int i;
	union sched_param sp;

	if (first) {
		memset(comps, 0, sizeof(comps));
		comps[0].spdid = 9;  // fastcgi
		comps[1].spdid = 7;  // http
		comps[1].min   = 10*PAGE_SIZE;
		comps[2].spdid = 21; // tcp
		comps[2].min   = 10*PAGE_SIZE;
		comps[3].spdid = 14; // linux nic
		comps[3].min   = 200*PAGE_SIZE;
		comps[4].spdid = 19;  // interference
		/* comps[2+i].spdid = 6;  // conn */
		/* comps[4+i].spdid = 15; // ip */
		for(i=0; i<COM_NUM; i++) {
			if (i==3) continue;
			cbuf_mempool_resize(comps[i].spdid, (tot_cbuf-comps[3].min)/(COM_NUM)/2);
		}
		cbuf_mempool_resize(comps[3].spdid, comps[3].min);
#ifdef POLICY_4
		memset(target_size, 0, sizeof(target_size));
		memset(total_cnt, 0, sizeof(target_size));
		min_large = tot_cbuf;
#endif
		sp.c.type  = SCHEDP_PRIO;
		sp.c.value = 6;
		if (cos_thd_create(run_policy, 0, sp.v, 0, 0) <= 0) {
			BUG();
		}
		printc("Simple policy init\n");
		first = 0;
	} else {
		printc("Error cbuf policy init more than once\n");
	}
	return ;
}
