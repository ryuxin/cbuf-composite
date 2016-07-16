C_OBJS=warp.o
COMPONENT=image_warper.o
INTERFACES=image_warper
DEPENDENCIES=printc sched mem_mgr_large valloc torrent evt cbuf_mgr lock image_blender

IF_LIB=$(LIBOPENCVOBJ) $(LIBSTDCXXOBJ) $(LIBSUPCXXOBJ) $(MUSLDIR)/lib/libc.a
OBJLIBS += $(POSIX_LIB)

include ../../Makefile.subsubdir

