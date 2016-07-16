C_OBJS=load.o
COMPONENT=img_loader.o
INTERFACES=
DEPENDENCIES=printc sched mem_mgr_large valloc torrent evt timed_blk cbuf_mgr lock feature_finder

IF_LIB=$(LIBOPENCVOBJ) $(LIBSTDCXXOBJ) $(LIBSUPCXXOBJ) $(MUSLDIR)/lib/libc.a
OBJLIBS += $(POSIX_LIB)

include ../../Makefile.subsubdir

