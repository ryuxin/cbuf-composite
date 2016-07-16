#include "opencv2/stitching/detail/camera.hpp"
#include "opencv2/stitching/detail/util.hpp"

#include <img_stitch.h>
#include <feature_finder.h>
#include <feature_matcher.h>

Mat images[NUM_IMAGE];
vector<ImageFeatures> features(NUM_IMAGE);

void
send_feature(void *addr)
{
	cbuf_t cbid;
	struct matrix_info *img_info = (struct matrix_info *)addr;
	void *buf = (char *)addr+sizeof(matrix_info)*NUM_IMAGE;

	// send and free image data
	// As the image matrix receives a data pointer from outside world, it cannot 
	// free the data pointer using its own allocator, so manually free cbuf here.
	for(int i = 0; i < NUM_IMAGE; ++i) {
		cbuf_send_free(img_info[i].cbid);
	}
	serialize_feature(features, buf);
	// free descriptor of features
	for(int i = 0; i < NUM_IMAGE; ++i) {
		features[i].descriptors.release();
	}
}

void *
receive_image(cbuf_t cbid, unsigned long sz, cbuf_t *id)
{
	void *data, *addr;

	data = cbuf2buf(cbid, sz);
	retrieve_image(data, images);

	addr = cbuf_alloc(sz, id);
	memcpy(addr, data, sizeof(matrix_info)*NUM_IMAGE);
	cbuf_free(cbid);
	return addr;
}

extern "C" void 
img_stitch_feature_find(cbuf_t cbid, unsigned long sz)
{
	unsigned long long int start, end, ss, ee;
	cbuf_t s_cbid;
	void *addr;
	rdtscll(ss);
	addr = receive_image(cbid, sz, &s_cbid);

	Ptr<FeaturesFinder> finder;
	finder = makePtr<SurfFeaturesFinder>();

	for(int i = 0; i < NUM_IMAGE; ++i) {
		(*finder)(images[i], features[i]);
		features[i].img_idx = i;
	}
	finder->collectGarbage();

	send_feature(addr);
	cbuf_send_free(s_cbid);
	rdtscll(ee);
	printc("feature finder start %llu end %llu\n", ss, ee);
	img_stitch_feature_match(s_cbid, sz);

	return ;
}
