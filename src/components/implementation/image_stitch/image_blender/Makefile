C_OBJS=blend.o
COMPONENT=image_blender.o
INTERFACES=image_blender
DEPENDENCIES=printc sched mem_mgr_large valloc torrent evt cbuf_mgr lock

IF_LIB=$(LIBOPENCVOBJ) $(LIBSTDCXXOBJ) $(LIBSUPCXXOBJ) $(MUSLDIR)/lib/libc.a
OBJLIBS += $(POSIX_LIB)

include ../../Makefile.subsubdir

