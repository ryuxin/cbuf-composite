#include "opencv2/stitching/detail/autocalib.hpp"
#include "opencv2/stitching/detail/blenders.hpp"
#include "opencv2/stitching/detail/timelapsers.hpp"
#include "opencv2/stitching/detail/exposure_compensate.hpp"
#include "opencv2/stitching/detail/motion_estimators.hpp"
#include "opencv2/stitching/detail/seam_finders.hpp"
#include "opencv2/stitching/detail/util.hpp"
#include "opencv2/stitching/detail/warpers.hpp"
#include "opencv2/stitching/warpers.hpp"

#include <img_stitch.h>
#include <image_register.h>
#include <image_warper.h>

vector<ImageFeatures> features(NUM_IMAGE);
vector<MatchesInfo> pairwise_matches;
vector<CameraParams> cameras;
float conf_thresh = 0.5f;
string ba_refine_mask = "xxxxx";

void 
send_camera(void *addr)
{
	int i;
	struct feature_info *fea_info;
	struct match_info_head *hdr;
	void *buf = (char *)addr+sizeof(matrix_info)*NUM_IMAGE;

	// free feature and math_info data
	fea_info = (struct feature_info *)buf;
	for (i=0; i<NUM_IMAGE; i++) {
		cbuf_free(fea_info[i].cbid);
		features[i].descriptors.release();
	}
	hdr = (struct match_info_head *)&fea_info[i];
	cbuf_free(hdr->cbid);

	serialize_camera(cameras, buf);
}

void *
receive_match(cbuf_t cbid, unsigned long sz, cbuf_t *id)
{
	void *buf, *data, *ret;

	buf = cbuf2buf(cbid, sz);
	data = (char *)buf+sizeof(matrix_info)*NUM_IMAGE;
	retrieve_feature(data, features);

	data = (char *)data+sizeof(feature_info)*NUM_IMAGE;
	retrieve_match(data, pairwise_matches);

	ret  = cbuf_alloc(sz, id);
	memcpy(ret, buf, sz);
	cbuf_free(cbid);
	return ret;
}

extern "C" void 
img_stitch_registration(cbuf_t cbid, unsigned long sz)
{
	unsigned long long int start, end, ss, ee;
	cbuf_t s_cbid;
	void *addr;

	rdtscll(ss);
	addr = receive_match(cbid, sz, &s_cbid);

	HomographyBasedEstimator estimator;
	if (!estimator(features, pairwise_matches, cameras)) {
		return ;
	}

	for (size_t i = 0; i < cameras.size(); ++i) {
		Mat R;
		cameras[i].R.convertTo(R, CV_32F);
		cameras[i].R = R;
	}

	Ptr<detail::BundleAdjusterBase> adjuster;
	adjuster = makePtr<detail::BundleAdjusterRay>();

	adjuster->setConfThresh(conf_thresh);
	Mat_<uchar> refine_mask = Mat::zeros(3, 3, CV_8U);
	if (ba_refine_mask[0] == 'x') refine_mask(0,0) = 1;
	if (ba_refine_mask[1] == 'x') refine_mask(0,1) = 1;
	if (ba_refine_mask[2] == 'x') refine_mask(0,2) = 1;
	if (ba_refine_mask[3] == 'x') refine_mask(1,1) = 1;
	if (ba_refine_mask[4] == 'x') refine_mask(1,2) = 1;
	adjuster->setRefinementMask(refine_mask);
	if (!(*adjuster)(features, pairwise_matches, cameras)) {
		return ;
	}

	send_camera(addr);
	cbuf_send_free(s_cbid);
	rdtscll(ee);
	printc("feature estimator start %llu end %llu\n", ss, ee);
	img_stitch_warp(s_cbid, sz);
	return ;
}
