#include <string>
#include "opencv2/opencv_modules.hpp"
#include <opencv2/core/utility.hpp>
#include "opencv2/imgcodecs.hpp"
#include "opencv2/stitching/detail/autocalib.hpp"
#include "opencv2/stitching/detail/blenders.hpp"
#include "opencv2/stitching/detail/timelapsers.hpp"
#include "opencv2/stitching/detail/camera.hpp"
#include "opencv2/stitching/detail/exposure_compensate.hpp"
#include "opencv2/stitching/detail/matchers.hpp"
#include "opencv2/stitching/detail/motion_estimators.hpp"
#include "opencv2/stitching/detail/seam_finders.hpp"
#include "opencv2/stitching/detail/util.hpp"
#include "opencv2/stitching/detail/warpers.hpp"
#include "opencv2/stitching/warpers.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#include <print.h>

#ifdef __cplusplus
}
#endif

using namespace std;
using namespace cv;
using namespace cv::detail;

#define UHZ 2900
#define NUM_IMAGE 2
// Default command line args
vector<String> img_names;
bool preview = true;
bool try_cuda = false;
double work_megapix = 0.08;
double seam_megapix = 0.1;
double compose_megapix = -1;
float conf_thresh = 0.5f;
string features_type = "surf";
string ba_cost_func = "ray";
string ba_refine_mask = "xxxxx";
bool do_wave_correct = false/*true*/;
WaveCorrectKind wave_correct = detail::WAVE_CORRECT_HORIZ;
bool save_graph = false;
std::string save_graph_to;
string warp_type = "spherical";
int expos_comp_type = ExposureCompensator::NO/*GAIN_BLOCKS*/;
float match_conf = 0.3f;
string seam_find_type = "no"/*"gc_color"*/;
int blend_type = Blender::MULTI_BAND;
int timelapse_type = Timelapser::AS_IS;
float blend_strength = 5;
string result_name = "result.bmp";
bool timelapse = false;
int timelapse_range = 5;


static int parseCmdArgs(int argc)
{
	img_names.push_back("temp0.bmp");
	img_names.push_back("temp1.bmp");
	return 0;
}

int mycompare(Mat &aa, Mat &bb)
{
	Mat a = aa, b;
	int i, j, k;
	assert(aa.type() == CV_8UC3);
	bb.convertTo(b, CV_8U);
	assert(b.type() == CV_8UC3);
	if (a.cols != b.cols || a.rows != b.rows || a.dims != b.dims) {
		printc("size error a col %d row %d b col %d row %d\n", a.cols, a.rows, b.cols, b.rows);
		return 1;
	}

        for(i=0; i<a.rows; i++) {
        	for(j=0; j<a.cols; j++) {
        		Vec3b t1, t2;
        		t1 = a.at<Vec3b>(i, j);
			t2 = b.at<Vec3b>(i, j);
			int t = 0;
			t = ((int)t2[0]<<16) | ((int)t2[1]<<8) | (int)t2[2];
			if (315<=i && i<347) printc("%d ", t);
        		// for(k=0; k<3; k++) {
        		// 	if (t1[k] != t2[k]) {
			// 		printc("pixel error %d %d %d %d %d\n", i, j, k, t1[k], t2[k]);
        		// 		return 1;
        		// 	}
        		// }
		}
	}
	return 0;
}

void check_stack()
{
        unsigned long sp, top;
        asm volatile("mov %%esp, %0" : "=r" (sp));
	top = sp & 0xfffff000;
	if (*(char *)top != '$') {
		printc("stack overflow %c sp %x top %x\n", *(char *)top, sp, top);
//		*(char *)0 = 1;
	} else {
		printc("stack ok\n");
	}
}

extern "C" void 
cos_init(void)
{
        unsigned long sp, top;
        asm volatile("mov %%esp, %0" : "=r" (sp));
	top = sp & 0xfffff000;
	*(char *)top = '$';
	printc("sp %x top %x\n", sp, top);

	printc("==========Image stitching begin =========\n");
	unsigned long long int start, end;
	rdtscll(start);
	int retval = parseCmdArgs(2);
	if (retval) return ;
	// Check if have enough images
	assert(NUM_IMAGE == static_cast<int>(img_names.size()));

	Mat images[NUM_IMAGE], pre_images[NUM_IMAGE], exp;
	Mat result, result_mask;
	exp = imread("exp.bmp");
	rdtscll(start);
	for(int i = 0; i < NUM_IMAGE; ++i) {
		images[i] = imread(img_names[i]);
		if (images[i].empty()) return ;
	}
	rdtscll(end);
	printc("read prepare image  %llu  %llu\n", end-start, (end-start)/UHZ);

	rdtscll(start);
	vector<ImageFeatures> features(NUM_IMAGE);
	Ptr<FeaturesFinder> finder;
	finder = makePtr<SurfFeaturesFinder>();

	for(int i = 0; i < NUM_IMAGE; ++i) {
		(*finder)(images[i], features[i]);
		features[i].img_idx = i;
	}
	finder->collectGarbage();
	rdtscll(end);
	printc("feature finder %llu %llu\n", end-start, (end-start)/UHZ);

	rdtscll(start);
	vector<MatchesInfo> pairwise_matches;
	BestOf2NearestMatcher matcher(try_cuda, match_conf);
	matcher(features, pairwise_matches);
	matcher.collectGarbage();
	rdtscll(end);
	printc("feature matcher %llu %llu\n", end-start, (end-start)/UHZ);

	rdtscll(start);
	HomographyBasedEstimator estimator;
	vector<CameraParams> cameras;
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
	rdtscll(end);
	printc("feature estimator %llu %llu\n", end-start, (end-start)/UHZ);
	// Find median focal length

	rdtscll(start);
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

	vector<Point> corners(NUM_IMAGE);
	UMat masks_warped[NUM_IMAGE];
	UMat images_warped[NUM_IMAGE];
	vector<Size> sizes(NUM_IMAGE);
	UMat masks[NUM_IMAGE];

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

		warper->warp(masks[i], K, cameras[i].R, INTER_NEAREST, BORDER_CONSTANT, masks_warped[i]);
	}

	UMat images_warped_s[NUM_IMAGE];
	for (int i = 0; i < NUM_IMAGE; ++i) {
		images_warped[i].convertTo(images_warped_s[i], CV_16S);
	}
	rdtscll(end);
	printc("feature warperer %llu %llu\n", end-start, (end-start)/UHZ);

	rdtscll(start);
	Ptr<Blender> blender;
	blender = Blender::createDefault(blend_type, try_cuda);
	Size dst_sz = resultRoi(corners, sizes).size();
	float blend_width = sqrt(static_cast<float>(dst_sz.area())) * blend_strength / 100.f;
	MultiBandBlender* mb = dynamic_cast<MultiBandBlender*>(blender.get());
	mb->setNumBands(static_cast<int>(ceil(log(blend_width)/log(2.)) - 1.));
	blender->prepare(corners, sizes);

	for (int img_idx = 0; img_idx < NUM_IMAGE; ++img_idx) {
		// Blend the current image
		blender->feed(images_warped_s[img_idx], masks_warped[img_idx], corners[img_idx]);
	}
	if (!timelapse) {
		blender->blend(result, result_mask);
//		exp = imread("exp.bmp");
//		imwrite(result_name, result);
	}
	rdtscll(end);
	printc("feature blender %llu %llu\n", end-start, (end-start)/UHZ);
	printc("res r %d c %d d %d t %d\n", result.rows, result.cols, result.dims, result.type());
	printc("exp r %d c %d d %d t %d\n", exp.rows, exp.cols, exp.dims, exp.type());	
	if (!mycompare(exp, result)) printc("result is right\n");
	else printc("result is wrong\n");
	printc("=========Image stitching finish =======\n");
	check_stack();
	return ;
}
