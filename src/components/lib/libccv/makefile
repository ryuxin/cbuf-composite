CC := ../musl-1.1.11/bin/musl-gcc
AR := ar
CFLAGS := -O3 -Wall -fno-stack-protector -ffast-math -nostdinc -nostdlib -Os
.PHONY: all clean

all: cos_libccv.a

clean:
	rm -f *.o 3rdparty/sha1/sha1.o 3rdparty/kissfft/*.o 3rdparty/sqlite3/*.o cos_libccv.a

3rdparty/sqlite3/sqlite3.o: 3rdparty/sqlite3/sqlite3.c
	$(CC) $< -o $@ -c -O3 -fno-stack-protector -Os -D SQLITE_THREADSAFE=0 -D SQLITE_OMIT_LOAD_EXTENSION 

ccv_io.o: ccv_io.c ccv.h ccv_internal.h io/*.c
	$(CC) $< -o $@ -c $(CFLAGS) 

%.o: %.c ccv.h ccv_internal.h
	$(CC) $< -o $@ -c $(CFLAGS)

cos_libccv.a:ccv_basic.o ccv_classic.o ccv_bbf.o ccv_dpm.o ccv_icf.o ccv_sift.o ccv_swt.o ccv_util.o ccv_memory.o ccv_cache.o ccv_resample.o ccv_io.o ccv_math.o ccv_algebra.o ccv_numeric.o  ccv_image_processing.o ccv_scd.o 3rdparty/sqlite3/sqlite3.o 3rdparty/sha1/sha1.o 3rdparty/kissfft/kiss_fft.o 3rdparty/kissfft/kiss_fftnd.o 3rdparty/kissfft/kiss_fftr.o 3rdparty/kissfft/kiss_fftndr.o 3rdparty/kissfft/kissf_fft.o 3rdparty/kissfft/kissf_fftnd.o 3rdparty/kissfft/kissf_fftr.o 3rdparty/kissfft/kissf_fftndr.o
	$(AR) rcs $@ $^
