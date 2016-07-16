.PHONY: all clean 

all:
	@mkdir -p build
	@mkdir -p ./build/objects
	@make -C calib3d/src
	@make -C core/src
	@make -C fake/src
	@make -C features2d/src
	@make -C flann/src
	@make -C hal/src
	@make -C imgcodecs/src
	@make -C imgproc/src
	@make -C stitching/src
	@make -C xfeatures2d/src
	@$(AR) rcs libopencv.a ./build/objects/*.o

clean: 
	rm -rf *.a *.o ./build
	@make -C calib3d/src clean
	@make -C core/src clean
	@make -C fake/src clean
	@make -C flann/src clean
	@make -C features2d/src clean
	@make -C hal/src clean
	@make -C imgcodecs/src clean
	@make -C imgproc/src clean
	@make -C stitching/src clean
	@make -C xfeatures2d/src clean

