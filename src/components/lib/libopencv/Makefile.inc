CXX := g++
BUILD := ../../build/objects
LIBDIR := ../../..
COMPDIR := $(LIBDIR)/..
INCLUDE := -I../../include -I$(LIBDIR)/libcxx/build/include -I$(LIBDIR)/ck/include -I$(COMPDIR)/include -I$(COMPDIR)/../kernel/include/shared -I$(COMPDIR)/interface/cbuf_mgr
MACROFLAG := -D __OPENCV_BUILD=1 -D MINIMAL_LIBCXX 
#-U __SSE2__
MUSLFLAG := -isystem ../../../musl-1.1.11/include 
CXXFLAG := -Wno-narrowing -nostdinc -nostdlib -fno-stack-protector -fno-threadsafe-statics -fno-exceptions -O3 -ffast-math -fpermissive \
$(INCLUDE) $(MACROFLAG) $(MUSLFLAG)

.PHONY: all clean
all: $(SRC_CPP)
	@cp *.o $(BUILD)
clean:
	@rm -rf *.o $(BUILD)

$(SRC_CPP):%.o:%.cpp
	@$(CXX) $< -o $(PREFIX)$@ -c $(CXXFLAG)
