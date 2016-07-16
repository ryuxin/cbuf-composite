C_OBJS=match.o
COMPONENT=feature_matcher.o
INTERFACES=feature_matcher
DEPENDENCIES=printc sched mem_mgr_large valloc torrent evt cbuf_mgr lock image_register

IF_LIB=$(LIBOPENCVOBJ) $(LIBSTDCXXOBJ) $(LIBSUPCXXOBJ) $(MUSLDIR)/lib/libc.a
OBJLIBS += $(POSIX_LIB)

include ../../Makefile.subsubdir

