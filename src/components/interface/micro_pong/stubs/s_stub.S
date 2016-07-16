/**
 * Copyright 2009 by Gabriel Parmer, gabep1@cs.bu.edu
 *
 * Redistribution of this file is permitted under the GNU General
 * Public License v2.
 */

#include <cos_asm_server_stub.h>
//#include <cos_asm_server_stub_simple_stack.h>

.text

cos_asm_server_stub(call)
cos_asm_server_stub(call_cs)
cos_asm_server_stub(simple_call_buf2buf)
cos_asm_server_stub(call_cbuf2buf)
cos_asm_server_stub(call_free)
cos_asm_server_stub(call_pingpong_prepare)
cos_asm_server_stub(call_cbuf_pingpong)
cos_asm_server_stub(call_cbuf_alloc)
cos_asm_server_stub(call_cbuf_send)
cos_asm_server_stub(call_cbuf_resize)
cos_asm_server_stub(call_cbuf_debug)
