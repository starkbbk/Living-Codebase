#ifndef __MOCK_BPF_HELPERS_H
#define __MOCK_BPF_HELPERS_H

#define BPF_KPROBE(name, args...) int name(struct pt_regs *ctx, ##args)
#define BPF_KRETPROBE(name, args...) int name(struct pt_regs *ctx, ##args)

static void *(*bpf_map_lookup_elem)(void *map, const void *key) = (void *)1;
static int (*bpf_map_update_elem)(void *map, const void *key, const void *value, unsigned long long flags) = (void *)2;
static int (*bpf_map_delete_elem)(void *map, const void *key) = (void *)3;
static unsigned long long (*bpf_get_current_pid_tgid)(void) = (void *)4;
static unsigned long long (*bpf_ktime_get_ns)(void) = (void *)5;

static inline void *bpf_ringbuf_reserve(void *ringbuf, unsigned long long size, unsigned long long flags) {
    (void)ringbuf; (void)size; (void)flags;
    return (void *)0;
}
static inline void bpf_ringbuf_submit(void *data, unsigned long long flags) {
    (void)data; (void)flags;
}
static inline int bpf_probe_read_kernel_str(void *dst, unsigned int size, const void *unsafe_ptr) {
    (void)dst; (void)size; (void)unsafe_ptr;
    return 0;
}

#endif /* __MOCK_BPF_HELPERS_H */
