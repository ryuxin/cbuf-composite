SRC_C := fake_pthread.o
CC := gcc
BUILD := ../../build/objects
MACROFLAG := -D __OPENCV_BUILD -U __SSE2__ -D MINIMAL_LIBCXX
INCLUDE := -I../../include -I../../../libcxx/build/include
MUSLFLAG := -isystem ../../../musl-1.1.11/include 
CXXFLAG := -Wno-narrowing -nostdinc -nostdlib -fno-stack-protector -O3 -ffast-math \
$(INCLUDE) $(MACROFLAG) $(MUSLFLAG)

.PHONY: all clean
all: $(SRC_C)
	@cp *.o $(BUILD)

clean:
	@rm -rf *.o $(BUILD)

$(SRC_C):%.o:%.c
	@$(CC) $< -o $@ -c $(CXXFLAG)

