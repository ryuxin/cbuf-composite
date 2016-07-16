#include "opencv2/stitching/detail/autocalib.hpp"
#include "opencv2/stitching/detail/blenders.hpp"
#include "opencv2/stitching/detail/timelapsers.hpp"
#include "opencv2/stitching/detail/util.hpp"
#include <img_stitch.h>
#include <image_blender.h>

vector<Point> corners(NUM_IMAGE);
vector<Size> sizes(NUM_IMAGE);
UMat images_warped_s[NUM_IMAGE];
UMat masks_warped[NUM_IMAGE];
bool try_cuda = false;
int blend_type = Blender::MULTI_BAND;
float blend_strength = 5;
bool timelapse = false;
Mat result, result_mask, exp_img;

// void
// clear_stack()
// {
//         unsigned long sp, top;
// 	int *st, i;
//         asm volatile("mov %%esp, %0" : "=r" (sp));
// 	top = sp & 0xfffff000;
// 	st = (int *)top;
// 	for(i=1; i<2048; i++) {
// 		*st = 0;
// 		st--;
// 	}
// 	printf("clear stack\n");
// 	return ;
// }

int
mycompare(Mat &aa, Mat &bb)
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
        		for(k=0; k<3; k++) {
        			if (t1[k] != t2[k]) {
					printc("pixel error %d %d %d %d %d\n", i, j, k, t1[k], t2[k]);
        				return 1;
        			}
        		}
		}
	}
	return 0;
}

void
receive_warped_img(cbuf_t cbid, unsigned long sz)
{
	Point *p;
	Size *s;
	int i;
	struct umatrix_info *um;

	p = (Point *)cbuf2buf(cbid, sz);
	corners.assign(p, p+NUM_IMAGE);
	s = (Size *)(p+NUM_IMAGE);
	sizes.assign(s, s+NUM_IMAGE);

	um = (struct umatrix_info *)(s+NUM_IMAGE);
	for(i=0; i<NUM_IMAGE; i++) retrieve_umat(&um[i], images_warped_s[i]);
	um = um+NUM_IMAGE;
	for(i=0; i<NUM_IMAGE; i++) retrieve_umat(&um[i], masks_warped[i]);
}

extern "C" void 
img_stitch_blend(cbuf_t cbid, unsigned long sz)
{
	unsigned long long int start, end, ss, ee;

	rdtscll(ss);
	receive_warped_img(cbid, sz);

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
	if (!timelapse) blender->blend(result, result_mask);
	cbuf_free(cbid);
	rdtscll(ee);
	printc("feature blender start %llu end %llu\n", ss, ee);

	// printc("res r %d c %d d %d t %d\n", result.rows, result.cols, result.dims, result.type());
	// exp_img = imread("exp.bmp");
	// printc("exp r %d c %d d %d t %d\n", exp_img.rows, exp_img.cols, exp_img.dims, exp_img.type());	
	// if (!mycompare(exp_img, result)) printc("result is right\n");
	// else printc("result is wrong\n");
	// printc("Image stitching finish\n");
	return ;
}
