#ifndef   	IMG_STITCH_H
#define   	IMG_STITCH_H

#include <string>
#include "opencv2/opencv_modules.hpp"
#include <opencv2/core/utility.hpp>
#include "opencv2/imgcodecs.hpp"
#include "opencv2/stitching/detail/matchers.hpp"
#include "opencv2/stitching/detail/camera.hpp"

#define UHZ 2900
#define NUM_IMAGE 2

using namespace std;
using namespace cv;
using namespace cv::detail;

#ifdef __cplusplus
extern "C" {
#endif

#include <cbuf.h>
#include <print.h>

struct matrix_info {
	int row, col, flag;
	int size;
	cbuf_t cbid;
};

struct umatrix_info {
	matrix_info mat;
	int dim;
	int step[2];
};

struct feature_info {
	int img_idx;
	cv::Size img_size;
	int num_keypoint;
	cbuf_t cbid;
	umatrix_info desc;
};

struct match_info_head {
	int num_match, size;
	cbuf_t cbid;
};

struct match_info {
	int src_idx, dst_idx;
	int num_inliers;
	double confidence;
	int num_dmatch, num_mask;
	matrix_info mat;
};

struct camera_info {
	double focal;
	double aspect;
	double ppx, ppy;
	matrix_info R, t;
};
#ifdef __cplusplus
}
#endif

static inline void
serialize_matrix(const Mat &sm, struct matrix_info *dm)
{
	dm->row  = sm.rows;
	dm->col  = sm.cols;
	dm->flag = sm.type();
	dm->size = sm.u->size;
}

static inline void
serialize_umatrix(const UMat &sm, struct matrix_info *dm)
{
	dm->row  = sm.rows;
	dm->col  = sm.cols;
	dm->flag = sm.flags;
	dm->size = sm.u->size;
}

static inline void 
serialize_umat(const UMat &sm, struct umatrix_info *dm)
{
	struct matrix_info *t;
	CbufMatAllocator *allocator;

	dm->step[0] = sm.step.buf[0];
	dm->step[1] = sm.step.buf[1];
	dm->dim = sm.dims;
	t = &dm->mat;
	allocator = (CbufMatAllocator *)sm.allocator;
	serialize_umatrix(sm, t);
	t->cbid = allocator->cbid;
	/* the cbuf should be freed by umat's destructor */
	cbuf_send(allocator->cbid);
}

static inline void 
retrieve_umat(struct umatrix_info *dm, UMat &sm)
{
	struct matrix_info *m;
	void *data;

	m              = &dm->mat;
	data           = cbuf2buf(m->cbid, m->size);
	UMatData* u    = new UMatData(new CbufMatAllocator(m->cbid));
	u->urefcount   = 1;
	u->refcount    = 0;
	u->data        = u->origdata = (uchar *)data;
	u->size        = m->size;
	sm             = UMat();
	sm.allocator   = new CbufMatAllocator(m->cbid);
	sm.rows        = m->row;
	sm.cols        = m->col;
	sm.flags       = m->flag;
	sm.step.buf[0] = dm->step[0];
	sm.step.buf[1] = dm->step[1];
	sm.step.p      = sm.step.buf;
	sm.size.p      = &sm.rows;
	sm.dims        = dm->dim;
	sm.u           = u;
}

void 
serialize_image(Mat *img, void *buf)
{
	CbufMatAllocator *allocator;
	struct matrix_info *image_info = (struct matrix_info *)buf;

	for(int i = 0; i < NUM_IMAGE; ++i) {
		allocator = (CbufMatAllocator *)img[i].allocator;
		serialize_matrix(img[i], &image_info[i]);
		image_info[i].cbid = allocator->cbid;
		cbuf_send_free(allocator->cbid);
	}
}

void
retrieve_image(void *buf, Mat *img)
{
	struct matrix_info *image_info, *t;
	void *data;

	image_info = (struct matrix_info *)buf;
	for(int i=0; i<NUM_IMAGE; i++) {
		t = &image_info[i];
		data = cbuf2buf(t->cbid, t->size);
		img[i] = Mat(t->row, t->col, t->flag, data);
	};
}

void
serialize_feature(const std::vector<ImageFeatures> &feat, void *buf)
{
	struct feature_info *fea_info;
	KeyPoint *keypoints;

	fea_info = (struct feature_info *)buf;
	for(int i = 0; i < NUM_IMAGE; ++i) {
		fea_info[i].img_idx = feat[i].img_idx;
		fea_info[i].img_size = feat[i].img_size;

		fea_info[i].num_keypoint = feat[i].keypoints.size();
		keypoints = (KeyPoint *)cbuf_alloc(fea_info[i].num_keypoint*sizeof(KeyPoint), &fea_info[i].cbid);
		std::copy(feat[i].keypoints.begin(), feat[i].keypoints.end(), keypoints);
		cbuf_send_free(fea_info[i].cbid);

		serialize_umat(feat[i].descriptors, &fea_info[i].desc);
	}
}

void
retrieve_feature(void *buf, std::vector<ImageFeatures> &feat)
{
	struct feature_info *fea_info, *t;
	KeyPoint *keypoints;

	fea_info = (struct feature_info *)buf;
	for(int i=0; i<NUM_IMAGE; i++) {
		t = &fea_info[i];
		feat[i].img_idx = t->img_idx;
		feat[i].img_size = t->img_size;

		keypoints = (KeyPoint *)cbuf2buf(t->cbid, t->num_keypoint*sizeof(KeyPoint));
		feat[i].keypoints.assign(keypoints, keypoints+t->num_keypoint);

		retrieve_umat(&t->desc, feat[i].descriptors);
	};
}

void
serialize_match(const std::vector<MatchesInfo> &pairwise, void *buf)
{
	cbuf_t cbid;
	struct matrix_info *m;
	struct match_info_head *hdr;
	struct match_info *matches_info, *t;
	char *data;
	int i, tot = 0;

	hdr = (struct match_info_head *)buf;
	hdr->num_match = pairwise.size();
	for(i=0; i<hdr->num_match; i++) {
		tot += pairwise[i].matches.size()*sizeof(DMatch);
		tot += pairwise[i].inliers_mask.size()*sizeof(uchar);
		if (pairwise[i].H.u) {
			tot += pairwise[i].H.u->size;
		}
	}
	hdr->size = tot;
	data = (char *)cbuf_alloc(tot, &hdr->cbid);

	matches_info = (struct match_info *)((char *)hdr+sizeof(struct match_info_head));
	for(i=0; i<hdr->num_match; i++) {
		t = &matches_info[i];
		t->src_idx     = pairwise[i].src_img_idx; 
		t->dst_idx     = pairwise[i].dst_img_idx;
		t->num_inliers = pairwise[i].num_inliers;
		t->confidence  = pairwise[i].confidence;
		t->num_dmatch  = pairwise[i].matches.size();
		if (!t->num_dmatch) continue;

		t->num_mask = pairwise[i].inliers_mask.size();
		m = &t->mat;
		serialize_matrix(pairwise[i].H, m);
		memcpy(data, pairwise[i].H.data, m->size);
		data += pairwise[i].H.u->size;
		std::copy(pairwise[i].matches.begin(), pairwise[i].matches.end(), (DMatch *)data);
		data += t->num_dmatch*sizeof(DMatch);
		std::copy(pairwise[i].inliers_mask.begin(), pairwise[i].inliers_mask.end(), (uchar *)data);
		data += t->num_mask*sizeof(uchar);
	}
	cbuf_send_free(hdr->cbid);
}

void
retrieve_match(void *buf, std::vector<MatchesInfo> &pairwise)
{
	struct matrix_info *m;
	struct match_info_head *hdr;
	struct match_info *matches_info, *matches;
	char *data;
	int i;

	hdr = (struct match_info_head *)buf;
	data = (char *)cbuf2buf(hdr->cbid, hdr->size);

	pairwise.resize(hdr->num_match);
	matches_info = (struct match_info *)((char *)hdr+sizeof(struct match_info_head));
	for(i=0; i<hdr->num_match; i++) {
		matches = &matches_info[i];
		pairwise[i].src_img_idx = matches->src_idx;
		pairwise[i].dst_img_idx = matches->dst_idx;
		pairwise[i].num_inliers = matches->num_inliers;
		pairwise[i].confidence  = matches->confidence;
		if (!matches->num_dmatch) continue;

		m = &matches->mat;
		pairwise[i].H = Mat(m->row, m->col, m->flag, data);
		data += m->size;
		pairwise[i].matches.assign((DMatch *)data, (DMatch *)data+matches->num_dmatch);
		data += matches->num_dmatch*sizeof(DMatch);
		pairwise[i].inliers_mask.assign((uchar *)data, (uchar *)data+matches->num_mask);
		data += matches->num_mask*sizeof(uchar);
	}
}

void
serialize_camera(const std::vector<CameraParams> &camera, void *buf)
{
	struct camera_info *c, *t;
	struct match_info_head *hdr;
	int i, tot = 0;
	char *data;

	hdr = (struct match_info_head *)buf;
	hdr->num_match = camera.size();
	for(i=0; i<hdr->num_match; i++) {
		tot += camera[i].R.u->size;
		tot += camera[i].t.u->size;
	}
	data = (char *)cbuf_alloc(tot, &hdr->cbid);
	hdr->size = tot;

	c = (struct camera_info *)((char *)hdr+sizeof(struct match_info_head));
	for(i=0; i<hdr->num_match; i++) {
		t = &c[i];
		t->focal  = camera[i].focal;
		t->aspect = camera[i].aspect;
		t->ppx    = camera[i].ppx;
		t->ppy    = camera[i].ppy;
		serialize_matrix(camera[i].R, &t->R);
		memcpy(data, camera[i].R.data, t->R.size);
		data += t->R.size;
		serialize_matrix(camera[i].t, &t->t);
		memcpy(data, camera[i].t.data, t->t.size);
		data += t->t.size;
	}
	cbuf_send_free(hdr->cbid);
}

int
retrieve_camera(void *buf, std::vector<CameraParams> &camera)
{
	struct camera_info *c, *t;
	struct match_info_head *hdr;
	struct matrix_info *m;
	int i;
	char *data;

	hdr = (struct match_info_head *)buf;
	camera.resize(hdr->num_match);
	data = (char *)cbuf2buf(hdr->cbid, hdr->size);

	c = (struct camera_info *)((char *)hdr+sizeof(struct match_info_head));
	for(i=0; i<hdr->num_match; i++) {
		t = &c[i];
		camera[i].focal = t->focal;
		camera[i].aspect = t->aspect;
		camera[i].ppx = t->ppx;
		camera[i].ppy = t->ppy;
		m = &t->R;
		camera[i].R = Mat(m->row, m->col, m->flag, data);
		data += m->size;
		m = &t->t;
		camera[i].t = Mat(m->row, m->col, m->flag, data);
		data += m->size;
	}
	return hdr->cbid;
}
#endif 	    /* !IMG_STITCH_H */
