C_OBJS=registration.o
COMPONENT=image_register.o
INTERFACES=image_register
DEPENDENCIES=printc sched mem_mgr_large valloc torrent evt cbuf_mgr lock image_warper

IF_LIB=$(LIBOPENCVOBJ) $(LIBSTDCXXOBJ) $(LIBSUPCXXOBJ) $(MUSLDIR)/lib/libc.a
OBJLIBS += $(POSIX_LIB)

include ../../Makefile.subsubdir

