#ifndef MICRO_PONG_H
#define MICRO_PONG_H

void call(void);
void call_cs(void);
int call_cbuf2buf(u32_t cb, int len);
int simple_call_buf2buf(u32_t cb, int len);
int call_free(u32_t cb);
void call_pingpong_prepare(int num, int sz);
u32_t call_cbuf_pingpong(u32_t cb, unsigned long sz);
u32_t call_cbuf_alloc(unsigned long sz);
void call_cbuf_send(u32_t cb, unsigned long sz, int num);
void call_cbuf_resize(unsigned long sz);
void call_cbuf_debug(void);

#endif /* !MICRO_PONG_H */
