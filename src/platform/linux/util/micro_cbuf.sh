#!/bin/sh

# ping pong
#(!po_last.o=micro_ppong_last.o), ;(!po1.o=micro_ppong.o), ;(!po2.o=micro_ppong.o), ;(!po3.o=micro_ppong.o), ;(!po4.o=micro_ppong.o), ;(!po5.o=micro_ppong.o), ;
#po.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o|[next_]po1.o;\
#po1.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o|[next_]po2.o;\
#po2.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o|[next_]po3.o;\
#po3.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o|[next_]po4.o;\
#po4.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o|[next_]po5.o;\
#po5.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o|[next_]po_last.o;\
#po_last.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o\

./cos_loader \
"c0.o, ;llboot.o, ;*fprr.o, ;mm.o, ;print.o, ;boot.o, ;\
\
!mpool.o,a3;!trans.o,a6;!l.o,a1;!te.o,a3;!e.o,a4;!stat.o,a25;!cbuf.o, ;(!po_last.o=micro_ppong_last.o), ;(!po1.o=micro_ppong.o), ;(!po2.o=micro_ppong.o), ;(!po3.o=micro_ppong.o), ;(!po4.o=micro_ppong.o), ;(!po5.o=micro_ppong.o), ;(!po.o=micro_ppong.o), ;(!pi.o=micro_pingp.o),a9;!va.o,a2;!vm.o,a1:\
\
c0.o-llboot.o;\
fprr.o-print.o|[parent_]mm.o|[faulthndlr_]llboot.o;\
mm.o-[parent_]llboot.o|print.o;\
boot.o-print.o|fprr.o|mm.o|llboot.o;\
l.o-fprr.o|mm.o|print.o;\
te.o-cbuf.o|print.o|fprr.o|mm.o|va.o;\
e.o-cbuf.o|fprr.o|print.o|mm.o|l.o|va.o;\
stat.o-cbuf.o|te.o|fprr.o|l.o|print.o|e.o;\
\
cbuf.o-boot.o|fprr.o|print.o|l.o|mm.o|va.o|mpool.o;\
mpool.o-print.o|fprr.o|mm.o|boot.o|va.o|l.o;\
vm.o-fprr.o|print.o|mm.o|l.o|boot.o;\
va.o-fprr.o|print.o|mm.o|l.o|boot.o|vm.o;\
trans.o-fprr.o|l.o|cbuf.o|mm.o|va.o|e.o|print.o;\
\
pi.o-fprr.o|va.o|po.o|print.o|mm.o|l.o|cbuf.o;\
po.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o|[next_]po1.o;\
po1.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o|[next_]po2.o;\
po2.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o|[next_]po3.o;\
po3.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o|[next_]po4.o;\
po4.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o|[next_]po5.o;\
po5.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o|[next_]po_last.o;\
po_last.o-fprr.o|print.o|mm.o|va.o|cbuf.o|l.o\
" ./gen_client_stub
