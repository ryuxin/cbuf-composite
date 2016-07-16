#include <img_stitch.h>
#include <feature_finder.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <sched.h>
#include <timed_blk.h>

#ifdef __cplusplus
}
#endif

#define IMG_HEADER_SZ 4096

vector<String> img_names;
Mat images[NUM_IMAGE];

static int
parseCmdArgs(int argc)
{
	img_names.push_back("temp0.bmp");
	img_names.push_back("temp1.bmp");
	return 0;
}

static cbuf_t
send_image()
{
	cbuf_t cbid;
	void *buf;

	buf = cbuf_alloc(IMG_HEADER_SZ, &cbid);
	serialize_image(images, buf);
	cbuf_send_free(cbid);
	return cbid;
}

extern "C" void 
cos_init(void)
{
	printc("Image stitching begin\n");
	unsigned long long int start, end, ee, final;
	cbuf_t cbid;

//	timed_event_block(cos_spd_id(), 1);
	rdtscll(start);
	int retval = parseCmdArgs(2);
	if (retval) return ;
	// Check if have enough images
	assert(NUM_IMAGE == static_cast<int>(img_names.size()));

	for(int i = 0; i < NUM_IMAGE; ++i) {
		images[i] = imread(img_names[i]);
		if (images[i].empty()) return ;
	}
	rdtscll(end);

	cbid = send_image();
	rdtscll(ee);
	printc("read prepare image start %llu end %llu\n", start, ee);
	img_stitch_feature_find(cbid, IMG_HEADER_SZ);

	rdtscll(final);
	printc("image stitching finish total %llu %lluus\n", final-start, (final-start)/UHZ);

	return ;
}
