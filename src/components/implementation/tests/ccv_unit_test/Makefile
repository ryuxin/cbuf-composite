C_OBJS=ccv_unit_test.o
ASM_OBJS=
COMPONENT=ccv_unit.o
INTERFACES=
DEPENDENCIES=sched printc timed_blk torrent cbuf_mgr evt mem_mgr_large valloc lock
IF_LIB=./cos_libccv.a 
OBJLIBS += $(POSIX_LIB)

pre: first all
first:
	$(info |     [CP]   copying cos_libccv.a from libccv)
	@cp $(CCVOBJ) .

include ../../Makefile.subsubdir
