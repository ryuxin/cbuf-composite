#include <cos_component.h>
#include <stdio.h>
#include <print.h>
#include <valloc.h>
#include "../../../lib/libccv/ccv.h"
#include <print.h>

#define X_SLICE 1
#define Y_SLICE 1

void check_stack()
{
        unsigned long sp, top;
        asm volatile("mov %%esp, %0" : "=r" (sp));
	top = sp & 0xfffff000;
	if (*(char *)top != '$') {
		printc("stack overflow spd\n");
		*(char *)0 = 1;
	} else {
		printc("stack ok\n");
	}
}

inline static void free_matrix(ccv_dense_matrix_t ** f)
{
	assert(*f);
	ccv_matrix_free(*f);
	*f = 0;
}

inline static void free_array(ccv_array_t **a)
{
	assert(*a);
	ccv_array_free(*a);
	*a = 0;
}

void
cos_init(void)
{
        unsigned long sp, top;
	int i;
	void *hp1, *hp2;
        asm volatile("mov %%esp, %0" : "=r" (sp));
	top = sp & 0xfffff000;
	printc("sp %x top %x\n", sp, top);
	*(char *)top = '$';

	hp1 = valloc_alloc(cos_spd_id(), cos_spd_id(), 1024*10);
	hp2 = valloc_alloc(cos_spd_id(), cos_spd_id(), 1024*10);
	valloc_free(cos_spd_id(), cos_spd_id(), hp1, 4096*1024*10/PAGE_SIZE);
	valloc_free(cos_spd_id(), cos_spd_id(), hp2, 4096*1024*10/PAGE_SIZE);

        printc("CCV Test\n");

	ccv_disable_cache();
	printc("ccv disable cache\n");

	ccv_array_t* rearray = 0;
	ccv_dense_matrix_t *image = 0, *re = 0, *temp = 0;
	/* COLOR IMAGE */
	ccv_read("photo.bmp", &image, CCV_IO_RGB_COLOR | CCV_IO_ANY_FILE);

	ccv_saturation(image, &re, 0, 0.2);
	free_matrix(&re);
	printc("saturation done\n");

	ccv_contrast(image, &re, 0, 0.2);
	free_matrix(&re);
	printc("contrast done\n");

	ccv_resample(image, &re, 0, image->rows / 5, image->cols / 5, CCV_INTER_AREA);
	free_matrix(&re);
	printc("resample done\n");

	ccv_sample_down(image, &re, 0, 10, 10);
	free_matrix(&re);
	printc("sample down done\n");

	ccv_blur(image, &re, 0, sqrt(10));
	free_matrix(&re);
	printc("blur done\n");

	ccv_flip(image, &re, 0, CCV_FLIP_X | CCV_FLIP_Y);
	free_matrix(&re);
	printc("flip done\n");

	/* scd face detect */
	/* 4 */
	/* issue sqlite3 somehow use linux file path */
	/* ccv_scd_classifier_cascade_t* scd_cascade = ccv_scd_classifier_cascade_read("face.sqlite3"); */
	/* rearray = ccv_scd_detect_objects(image, &scd_cascade, 1, ccv_scd_default_params); */
        /* printc("scd detect %d faces\n", rearray->rnum); */
	/* free_array(&rearray); */
	/* ccv_scd_classifier_cascade_free(scd_cascade); */

	/* icf pedestrian detect */
	/* 1 */
	ccv_icf_classifier_cascade_t* icf_cascade = ccv_icf_read_classifier_cascade("pedestrian.icf");
	rearray = ccv_icf_detect_objects(image, &icf_cascade, 1, ccv_icf_default_params);
	for (i = 0; i < rearray->rnum; i++)
	{
		ccv_comp_t* comp = (ccv_comp_t*)ccv_array_get(rearray, i);
		printc("%d %d %d %d %f\n", comp->rect.x, comp->rect.y, comp->rect.width, comp->rect.height, comp->classification.confidence);
	}
        printc("icf detect %d pedestrian\n", rearray->rnum);
	free_array(&rearray);
 	ccv_icf_classifier_cascade_free(icf_cascade);

	/* dpm pedestrian detect */
	/* 2 */
	/* ccv_dpm_mixture_model_t* dpm_model = ccv_dpm_read_mixture_model("pedestrian.m"); */
	/* float ta = -0.001, tb = -0x1.bfe2c2p+0; */
	/* double tc = 0.00353; */
	/* printc("ta %f tb %f tc %lf\n", ta, tb, tc); */
	/* printc("after dpm read\n"); */
	/* rearray = ccv_dpm_detect_objects(image, &dpm_model, 1, ccv_dpm_default_params); */
        /* printc("dpm detect %d pedestrian\n", rearray->rnum); */
	/* free_array(&rearray); */
	/* ccv_dpm_mixture_model_free(dpm_model); */

	/* GRAY IMAGE */
	free_matrix(&image);
	ccv_read("photo.bmp", &image, CCV_IO_GRAY | CCV_IO_ANY_FILE);

	ccv_sobel(image, &re, 0, 1, 1);
	free_matrix(&re);
	printc("sobel filter done\n");

	/* canny edge detect */
	ccv_canny(image, &re, 0, 3, 36, 36 * 3);
	free_matrix(&re);
	printc("canny edge detect done\n");

	/* bbf face detect */
	ccv_bbf_classifier_cascade_t* bbf_cascade = ccv_bbf_read_classifier_cascade("face");
	rearray = ccv_bbf_detect_objects(image, &bbf_cascade, 1, ccv_bbf_default_params);
        printc("bbf detect %d faces\n", rearray->rnum);
	free_array(&rearray);

	/* swt detect text */
	/* 180 426 288 123 */
	/* rearray = ccv_swt_detect_words(image, ccv_swt_default_params); */
	/* if (rearray) */
	/* { */
	/* 	for (i = 0; i < rearray->rnum; i++) */
	/* 	{ */
	/* 		ccv_rect_t* rect = (ccv_rect_t*)ccv_array_get(rearray, i); */
	/* 		printc("%d %d %d %d\n", rect->x, rect->y, rect->width, rect->height); */
	/* 	} */
	/* 	free_array(&rearray); */
	/* } */

	/* SIFT key point test */
	/* 1987 */
	ccv_sift_param_t params = {
		.noctaves = 3,
		.nlevels = 6,
		.up2x = 1,
		.edge_threshold = 10,
		.norm_threshold = 0,
		.peak_threshold = 0,
	};
	ccv_sift(image, &rearray, &temp, 0, params);
	printc("key point num %d\n", rearray->rnum);
	free_array(&rearray);

        int sliced_total = 0;
        int slice_rows = image->rows / Y_SLICE;
        int slice_cols = image->cols / X_SLICE;
        int count = X_SLICE * Y_SLICE;
        for (i = 0; i < count; i++) {
                int y = i / X_SLICE;
                int x = i - X_SLICE * y;
                ccv_dense_matrix_t* slice = 0;
                ccv_slice(image, (ccv_matrix_t**)&slice, 0, slice_rows * y, slice_cols * x, slice_rows, slice_cols);
                ccv_array_t* sseq = ccv_bbf_detect_objects(slice, &bbf_cascade, 1, ccv_bbf_default_params);
                sliced_total += sseq->rnum;
        }
        printc("bbf detect %d faces\n", sliced_total);
	ccv_bbf_classifier_cascade_free(bbf_cascade);

        printc("done\n");
	check_stack();
	return;
}
