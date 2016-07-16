#!/bin/sh

./cos_loader \
"c0.o, ;llboot.o, ;*fprr.o, ;mm.o, ;print.o, ;boot.o, ;\
\
!mpool.o,a3;!l.o,a1;!te.o,a3;!eg.o,a4;!cbuf.o,a5;!va.o,a2;!initfs.o,a3;!rotar.o,a7;!ccv_unit.o,a8;!vm.o,a1:\
\
c0.o-llboot.o;\
fprr.o-print.o|[parent_]mm.o|[faulthndlr_]llboot.o;\
mm.o-[parent_]llboot.o|print.o;\
boot.o-print.o|fprr.o|mm.o|llboot.o;\
l.o-fprr.o|mm.o|print.o;\
te.o-print.o|fprr.o|mm.o|va.o;\
eg.o-cbuf.o|fprr.o|print.o|mm.o|l.o|va.o;\
cbuf.o-fprr.o|print.o|l.o|mm.o|va.o|mpool.o;\
mpool.o-print.o|fprr.o|mm.o|boot.o|va.o|l.o;\
vm.o-fprr.o|print.o|mm.o|l.o|boot.o;\
va.o-fprr.o|print.o|mm.o|l.o|boot.o|vm.o;\
initfs.o-fprr.o|print.o|cbuf.o||va.o|l.o|mm.o;\
rotar.o-fprr.o|print.o|mm.o|cbuf.o||l.o|eg.o|va.o|initfs.o;\
ccv_unit.o-fprr.o|cbuf.o||mm.o|eg.o|va.o|l.o|print.o|rotar.o\
" ./gen_client_stub



