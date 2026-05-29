#ifndef __MOCK_LINUX_BPF_H
#define __MOCK_LINUX_BPF_H

#define SEC(name) __attribute__((section(name), used))

enum bpf_map_type {
    BPF_MAP_TYPE_UNSPEC,
    BPF_MAP_TYPE_HASH,
    BPF_MAP_TYPE_RINGBUF,
};

#define BPF_ANY 0

#endif /* __MOCK_LINUX_BPF_H */
