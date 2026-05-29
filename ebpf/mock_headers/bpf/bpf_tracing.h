#ifndef __MOCK_BPF_TRACING_H
#define __MOCK_BPF_TRACING_H

struct pt_regs {
    unsigned long rip;
    unsigned long rax;
    unsigned long rdi;
};

#define PT_REGS_RC(x) ((x)->rax)
#define PT_REGS_PARM1(x) ((x)->rdi)

#endif /* __MOCK_BPF_TRACING_H */
