#!/bin/sh

./cos_loader \
"c0.o, ;llboot.o, ;*fprr.o, ;mm.o, ;print.o, ;boot.o, ;\
!initfs.o,a3;!vm.o,a1;!l.o,a1;!mpool.o,a3;!eg.o,a4;!cbuf.o,a5;!rotar.o,a7;!va.o,a2;!cxx_test.o,a6:\
c0.o-llboot.o;\
fprr.o-print.o|[parent_]mm.o|[faulthndlr_]llboot.o;\
mm.o-[parent_]llboot.o|print.o;\
boot.o-print.o|fprr.o|mm.o|llboot.o;\
l.o-fprr.o|mm.o|print.o;\
vm.o-fprr.o|print.o|mm.o|l.o|boot.o;\
va.o-fprr.o|print.o|mm.o|l.o|boot.o|vm.o;\
eg.o-cbuf.o|fprr.o|print.o|mm.o|l.o|va.o;\
mpool.o-print.o|fprr.o|mm.o|boot.o|va.o|l.o;\
cbuf.o-boot.o|fprr.o|print.o|l.o|mm.o|va.o|mpool.o;\
initfs.o-fprr.o|print.o|cbuf.o||va.o|l.o|mm.o;\
rotar.o-fprr.o|print.o|mm.o|cbuf.o||l.o|eg.o|va.o|initfs.o;\
cxx_test.o-print.o|fprr.o|mm.o|cbuf.o|eg.o|l.o|va.o|rotar.o\
" ./gen_client_stub


