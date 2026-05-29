#ifndef __FAULT_DETECTOR_H
#define __FAULT_DETECTOR_H

typedef unsigned long long __u64;
typedef unsigned int __u32;

#define FAULT_CLASS_MEMORY_CORRUPTION 0x0A
#define FAULT_CLASS_CONTROL_FLOW      0x0B
#define FAULT_CLASS_INVARIANT         0x0C
#define FAULT_CLASS_RESOURCE_LIMIT    0x0D

struct fault_event {
    __u64 pid;
    __u64 addr;
    __u64 timestamp;
    __u32 fault_class;
    char  func_name[64];
};

#endif /* __FAULT_DETECTOR_H */
