#include "opencv2/stitching/detail/autocalib.hpp"
#include "opencv2/stitching/detail/util.hpp"
#include "opencv2/stitching/detail/warpers.hpp"
#include "opencv2/stitching/warpers.hpp"
#include <img_stitch.h>
#include <image_warper.h>
#include <image_blender.h>

Mat images[NUM_IMAGE];
vector<CameraParams> cameras;
vector<Point> corners(NUM_IMAGE);
vector<Size> sizes(NUM_IMAGE);
UMat images_warped_s[NUM_IMAGE];
UMat masks_warped[NUM_IMAGE];
int camera_cbid;

cbuf_t
send_warped_img(cbuf_t cbid, void *buf, unsigned long sz)
{
	int i;
	void *addr;
	cbuf_t id;
	struct umatrix_info *um;
	struct matrix_info *img_info = (struct matrix_info *)buf;
	
	// free original image data
	for(int i = 0; i < NUM_IMAGE; ++i) {
		cbuf_free(img_info[i].cbid);
	}
	cbuf_free(cbid);

	addr  = cbuf_alloc(sz, &id);
	std::copy(corners.begin(), corners.end(), (Point *)addr);
	addr = (char *)addr+sizeof(Point)*corners.size();
	std::copy(sizes.begin(), sizes.end(), (Size *)addr);
	addr = (char *)addr+sizeof(Size)*sizes.size();

	um = (struct umatrix_info *)addr;
	for(i=0; i<NUM_IMAGE; i++) serialize_umat(images_warped_s[i], &um[i]);
	um += NUM_IMAGE;
	for(i=0; i<NUM_IMAGE; i++) serialize_umat(masks_warped[i], &um[i]);
	cbuf_free(camera_cbid);
	// free warped image and matrix data
	for(int i = 0; i < NUM_IMAGE; ++i) {
		images_warped_s[i].release();
		masks_warped[i].release();
	}
	cbuf_send_free(id);
	return id;
}

void *
receive_camera(cbuf_t cbid, unsigned long sz)
{
	void *buf, *data, *ret;

	buf = cbuf2buf(cbid, sz);
	struct matrix_info *image_info = (struct matrix_info *)buf;
	retrieve_image(buf, images);

	data = (char *)buf+sizeof(matrix_info)*NUM_IMAGE;
	camera_cbid = retrieve_camera(data, cameras);
	return buf;
}

extern "C" void 
img_stitch_warp(cbuf_t cbid, unsigned long sz)
{
	unsigned long long int start, end, ss, ee;
	void *buf;
	cbuf_t s_cbid;

	rdtscll(ss);
	buf = receive_camera(cbid, sz);

	UMat images_warped[NUM_IMAGE];
	UMat masks[NUM_IMAGE];

	// Find median focal length
	vector<double> focals;
	for (size_t i = 0; i < cameras.size(); ++i) {
		focals.push_back(cameras[i].focal);
	}

	sort(focals.begin(), focals.end());
	float warped_image_scale;
	if (focals.size() % 2 == 1) {
		warped_image_scale = static_cast<float>(focals[focals.size() / 2]);
	} else {
		warped_image_scale = static_cast<float>(focals[focals.size() / 2 - 1] + focals[focals.size() / 2]) * 0.5f;
	}

	// Preapre images masks
	for (int i = 0; i < NUM_IMAGE; ++i) {
		masks[i].create(images[i].size(), CV_8U);
		masks[i].setTo(Scalar::all(255));
	}

	// Warp images and their masks
	Ptr<WarperCreator> warper_creator;
	warper_creator = makePtr<cv::SphericalWarper>();

	if (!warper_creator) return ;

	double seam_work_aspect = 1;
	Ptr<RotationWarper> warper = warper_creator->create(static_cast<float>(warped_image_scale * seam_work_aspect));

	for (int i = 0; i < NUM_IMAGE; ++i) {
		Mat_<float> K;
		cameras[i].K().convertTo(K, CV_32F);
		float swa = (float)seam_work_aspect;
		K(0,0) *= swa; K(0,2) *= swa;
		K(1,1) *= swa; K(1,2) *= swa;

		corners[i] = warper->warp(images[i], K, cameras[i].R, INTER_LINEAR, BORDER_REFLECT, images_warped[i]);
		sizes[i] = images_warped[i].size();

		masks_warped[i].allocator = new CbufMatAllocator();
		warper->warp(masks[i], K, cameras[i].R, INTER_NEAREST, BORDER_CONSTANT, masks_warped[i]);
	}

	for (int i = 0; i < NUM_IMAGE; ++i) {
		images_warped_s[i].allocator = new CbufMatAllocator();
		images_warped[i].convertTo(images_warped_s[i], CV_16S);
	}

	s_cbid = send_warped_img(cbid, buf, sz);
	rdtscll(ee);
	printc("feature warperer start %llu end %llu\n", ss, ee);
	img_stitch_blend(s_cbid, sz);
	return ;
}
