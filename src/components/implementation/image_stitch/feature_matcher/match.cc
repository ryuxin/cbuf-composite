#include "opencv2/stitching/detail/util.hpp"

#include <img_stitch.h>
#include <feature_matcher.h>
#include <image_register.h>

vector<ImageFeatures> features(NUM_IMAGE);
vector<MatchesInfo> pairwise_matches;
bool try_cuda = false;
float match_conf = 0.3f;

void
send_match(void *addr)
{
	struct feature_info *fea_info;
	int i;

	// send feature data. The destructor of UMat can free 
	// the cbuf, so do not need to free the cbuf here.
	// But we need to send and free cbuf of keypoints.
	fea_info = (struct feature_info *)((char *)addr+sizeof(matrix_info)*NUM_IMAGE);
	for (i=0; i<NUM_IMAGE; i++) {
		cbuf_send_free(fea_info[i].cbid);
		cbuf_send(fea_info[i].desc.mat.cbid);
		features[i].descriptors.release();
	}

	serialize_match(pairwise_matches, (void *)&fea_info[i]);
}

void *
receive_feature(cbuf_t cbid, unsigned long sz, cbuf_t *id)
{
	void *buf, *addr, *ret;

	addr = cbuf2buf(cbid, sz);
	buf = (char *)addr+sizeof(matrix_info)*NUM_IMAGE;
	retrieve_feature(buf, features);

	ret  = cbuf_alloc(sz, id);
	memcpy(ret, (void *)addr, (sizeof(feature_info) + sizeof(matrix_info))*NUM_IMAGE);
	cbuf_free(cbid);
	return ret;
}

extern "C" void 
img_stitch_feature_match(cbuf_t cbid, unsigned long sz)
{
	unsigned long long int start, end, ss, ee;
	cbuf_t s_cbid;
	void *addr;

	rdtscll(ss);
	addr = receive_feature(cbid, sz, &s_cbid);

	BestOf2NearestMatcher matcher(try_cuda, match_conf);
	matcher(features, pairwise_matches);
	matcher.collectGarbage();

	send_match(addr);
	cbuf_send_free(s_cbid);
	rdtscll(ee);
	printc("feature matcher start %llu end %llu\n", ss, ee);
	img_stitch_registration(s_cbid, sz);
	return ;
}
