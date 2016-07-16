C_OBJS=find.o
COMPONENT=feature_finder.o
INTERFACES=feature_finder
DEPENDENCIES=printc sched mem_mgr_large valloc torrent evt cbuf_mgr lock feature_matcher

IF_LIB=$(LIBOPENCVOBJ) $(LIBSTDCXXOBJ) $(LIBSUPCXXOBJ) $(MUSLDIR)/lib/libc.a
OBJLIBS += $(POSIX_LIB)

include ../../Makefile.subsubdir

