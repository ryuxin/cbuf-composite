#!/bin/sh
#!image_policy.o,a4;
#image_policy.o-fprr.o|cbuf.o|mm.o|eg.o|va.o|l.o|print.o|te.o;\

./cos_loader \
"c0.o, ;llboot.o, ;*fprr.o, ;mm.o, ;print.o, ;boot.o, ;\
\
!mpool.o,a3;!l.o,a1;!te.o,a3;!eg.o,a4;!cbuf.o,a5;!va.o,a2;!initfs.o,a3;!rotar.o,a7;!img_loader.o,a8;!feature_finder.o,a8;!feature_matcher.o,a8;!image_register.o,a8;!image_warper.o,a8;!image_blender.o,a8;!vm.o,a1:\
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
image_blender.o-fprr.o|cbuf.o|mm.o|eg.o|va.o|l.o|print.o|rotar.o;\
image_warper.o-fprr.o|cbuf.o||mm.o|eg.o|va.o|l.o|print.o|image_blender.o|rotar.o;\
image_register.o-fprr.o|cbuf.o||mm.o|eg.o|va.o|l.o|print.o|image_warper.o|rotar.o;\
feature_matcher.o-fprr.o|cbuf.o||mm.o|eg.o|va.o|l.o|print.o|image_register.o|rotar.o;\
feature_finder.o-fprr.o|cbuf.o||mm.o|eg.o|va.o|l.o|print.o|feature_matcher.o|rotar.o;\
img_loader.o-fprr.o|cbuf.o||mm.o|eg.o|te.o|va.o|l.o|print.o|feature_finder.o|rotar.o\
" ./gen_client_stub



