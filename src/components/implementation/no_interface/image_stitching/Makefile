C_OBJS=image_stitch.o
ASM_OBJS=
COMPONENT=img_stitch.o
INTERFACES=
DEPENDENCIES=sched printc timed_blk torrent cbuf_mgr evt valloc lock mem_mgr_large
IF_LIB=./libopencv.a ./libstdc++.a ./libsupc++.a $(MUSLDIR)/lib/libc.a
OBJLIBS += $(POSIX_LIB)

pre: first all
first:
	$(info |     [CP]   copying libsupc++.a from libcxx)
	@cp $(LIBSUPCXXOBJ) .
	$(info |     [CP]   copying libstdc++.a from libcxx)
	@cp $(LIBSTDCXXOBJ) .
	$(info |     [CP]   copying libopencv.a from libccv)
	@cp $(LIBOPENCVOBJ) .

include ../../Makefile.subsubdir
