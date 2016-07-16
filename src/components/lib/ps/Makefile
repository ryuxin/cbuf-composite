CINC    = -I../../include/ -I. -I./plat/ -I../../interface/cbuf_mgr/ -I../../../kernel/include -I../../../kernel/include/shared -I ../ck/include
CC      = gcc
LD      = $(CC)
LDFLAGS = -L..
COPT    = -O3
CWARN   = -Wno-long-long
CFLAGS  = -Wall -Wpedantic -Wextra $(CWARN) $(COPT)

# library
CFILES  = $(wildcard ./*.c)
COBJS   = $(patsubst %.c,%.o,$(CFILES))
CDEPS   = $(patsubst %.c,%.d,$(CFILES))
CNAME   = ps
CLIB    = ./lib$(CNAME).a

# platform
#PCFILES = $(wildcard ../plat/*.c)
#PCOBJS  = $(patsubst %.c,%.o,$(PCFILES))
#PCDEPS  = $(patsubst %.c,%.d,$(PCFILES))

# tests
#TCFILES = $(wildcard *.c) # test .c files
#TCOBJS  = $(patsubst %.c,%.o,$(TCFILES))
#TCDEPS  = $(patsubst %.c,%.d,$(TCFILES))
#EXECS   = $(patsubst %.c,%.test,$(TCFILES))

.PHONY: setup setup-cos clean all

all: setup $(CLIB)

setup-cos:
	rm -f ps_plat.h ; ln -s plat/ps_plat_cos.h ps_plat.h

setup: setup-cos

%.d:%.c
	@set -e; rm -f $@; \
	$(CC) -M $(CINC) $(CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

%.o:%.c
	$(CC) $(CINC) $(CFLAGS) -o $@ -c $<

$(CLIB):$(COBJS)
	$(AR) cr $@ $^

%.test:%.o $(CLIB)
	$(LD) $(LDFLAGS) -o $@ $< -l$(CNAME)

clean:
	rm -f $(EXECS) $(TCOBJS) $(COBJS) $(PCOBJS) $(CLIB) *.d* ./*.d* ./plat/*.d*

-include $(CDEPS)
