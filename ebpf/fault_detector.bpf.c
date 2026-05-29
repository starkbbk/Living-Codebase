#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "fault_detector.h"

// Ring buffer for userspace daemon messaging
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} fault_events SEC(".maps");

// Trace current allocations (maps: heap_address -> allocator_thread_pid)
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 65536);
    __type(key, __u64);
    __type(value, __u64);
} heap_allocations SEC(".maps");

// Track transient active malloc request sizes for uretprobe mapping
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __u64); // thread_id
    __type(value, __u64); // requested_size
} active_malloc_requests SEC(".maps");

// Hook libc malloc: entry to record request size
SEC("uprobe/libc.so.6:malloc")
int BPF_KPROBE(malloc_entry, size_t size) {
    __u64 tid = bpf_get_current_pid_tgid();
    __u64 requested = (__u64)size;
    bpf_map_update_elem(&active_malloc_requests, &tid, &requested, BPF_ANY);
    return 0;
}

// Hook libc malloc: return to record block assignment
SEC("uretprobe/libc.so.6:malloc")
int BPF_KRETPROBE(malloc_ret, void *ret) {
    __u64 addr = (__u64)ret;
    if (addr == 0) {
        return 0;
    }
    __u64 tid = bpf_get_current_pid_tgid();
    
    // Associate allocated pointer with the requesting PID
    bpf_map_update_elem(&heap_allocations, &addr, &tid, BPF_ANY);
    bpf_map_delete_elem(&active_malloc_requests, &tid);
    return 0;
}

// Hook libc free: track double-free and invalid address frees
SEC("uprobe/libc.so.6:free")
int BPF_KPROBE(free_entry, void *addr) {
    __u64 key = (__u64)addr;
    if (key == 0) {
        return 0; // free(NULL) is legal in C
    }
    
    __u64 *pid_val = bpf_map_lookup_elem(&heap_allocations, &key);
    
    if (!pid_val) {
        // Address not found in heap mapping = Double-Free or wild pointer manipulation!
        struct fault_event *e = bpf_ringbuf_reserve(&fault_events, sizeof(*e), 0);
        if (e) {
            e->pid = bpf_get_current_pid_tgid() >> 32;
            e->addr = key;
            e->fault_class = FAULT_CLASS_MEMORY_CORRUPTION;
            e->timestamp = bpf_ktime_get_ns();
            bpf_probe_read_kernel_str(&e->func_name, sizeof(e->func_name), "libc:free_anomaly");
            bpf_ringbuf_submit(e, 0);
        }
    } else {
        // Cleanly remove allocation tracker
        bpf_map_delete_elem(&heap_allocations, &key);
    }
    return 0;
}

// Hook: Page Fault interceptor (do_user_addr_fault in x86_64 Linux kernels)
SEC("kprobe/do_user_addr_fault")
int BPF_KPROBE(page_fault_entry, struct pt_regs *regs, unsigned long error_code) {
    // In x86_64, CR2 register contains the memory address that triggered the page fault.
    // In do_user_addr_fault, this is typically extracted or passed.
    // To make it portable, we can inspect register structures or standard PT_REGS helpers.
    // We check if target is an address within the NULL page boundary (< 4096 bytes).
    unsigned long fault_addr = PT_REGS_PARM1(regs); // often passed or resolved

    if (fault_addr < 4096) {
        struct fault_event *e = bpf_ringbuf_reserve(&fault_events, sizeof(*e), 0);
        if (e) {
            e->pid = bpf_get_current_pid_tgid() >> 32;
            e->addr = (__u64)fault_addr;
            e->fault_class = FAULT_CLASS_INVARIANT; // Null Pointer violation
            e->timestamp = bpf_ktime_get_ns();
            bpf_probe_read_kernel_str(&e->func_name, sizeof(e->func_name), "kernel:null_deref");
            bpf_ringbuf_submit(e, 0);
        }
    }
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
