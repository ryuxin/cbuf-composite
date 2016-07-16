#udp_conn.o-print.o|mm.o|fprr.o|va.o|l.o|tnet.o|cbuf.o|eg.o;\
#cp.o-fprr.o|cbuf.o|mm.o|eg.o|va.o|l.o|print.o|te.o;\
#!udp_conn.o, ;!cp.o,a4;

./cos_loader \
"c0.o, ;llboot.o, ;*fprr.o, ;mm.o, ;print.o, ;boot.o, ;\
\
!tcp_conn.o, ;!mpool.o, ;!cbuf.o, ;!va.o, ;!vm.o, ;!tif.o,a5;!tip.o, ;!port.o, ;!l.o,a4;!te.o,a3;!tnet.o, ;!eg.o,a5:\
\
c0.o-llboot.o;\
fprr.o-print.o|[parent_]mm.o|[faulthndlr_]llboot.o;\
mm.o-[parent_]llboot.o|print.o;\
boot.o-fprr.o|print.o|mm.o|llboot.o;\
l.o-fprr.o|print.o|mm.o;\
te.o-fprr.o|print.o|cbuf.o|mm.o|va.o;\
eg.o-fprr.o|print.o|cbuf.o|mm.o|l.o|va.o;\
mpool.o-print.o|fprr.o|mm.o|boot.o|va.o|l.o;\
cbuf.o-fprr.o|boot.o|print.o|l.o|mm.o|va.o|mpool.o;\
vm.o-fprr.o|print.o|mm.o|l.o|boot.o;\
tnet.o-fprr.o|mm.o|print.o|l.o|te.o|eg.o|[parent_]tip.o|port.o|va.o|cbuf.o;\
tip.o-[parent_]tif.o|va.o|fprr.o|print.o|l.o|eg.o|cbuf.o|mm.o;\
tif.o-fprr.o|print.o|mm.o|l.o|va.o|eg.o|cbuf.o;\
port.o-fprr.o|print.o|cbuf.o|l.o;\
tcp_conn.o-print.o|mm.o|fprr.o|va.o|l.o|tnet.o|cbuf.o|te.o|eg.o;\
va.o-fprr.o|print.o|mm.o|l.o|boot.o|vm.o\
" ./gen_client_stub


