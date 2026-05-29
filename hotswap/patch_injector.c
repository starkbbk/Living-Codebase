#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef __linux__
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/syscall.h>

#if defined(__x86_64__)
#define SYSCALL_OPCODE_SIZE 2
static const unsigned char syscall_ins[] = {0x0f, 0x05}; // syscall
#elif defined(__aarch64__)
#define SYSCALL_OPCODE_SIZE 4
static const unsigned char syscall_ins[] = {0x01, 0x00, 0x00, 0xD4}; // svc #0
#endif

#else
// Mock definitions to satisfy macOS C/C++ IDE linters and compile stubs cleanly
#define MAP_FAILED ((void *)-1)
struct user_regs_struct {
    unsigned long rip;
    unsigned long rax;
    unsigned long pc;
    unsigned long regs[31];
};
#endif

/*
 * HotSwapContext structure definition
 */
typedef struct {
    pid_t target_pid;
    void* fault_addr;      // Address of faulty function to override
    void* patch_code;      // Compiled patch code binary array
    size_t patch_size;     // Size of patch bytes
} HotSwapContext;

/*
 * inject_patch
 * Attaches to target_pid, executes an anonymous mmap allocation inside the target context
 * via ptrace syscall injection, copies the patch bytes, and writes a JMP redirection hook.
 */
int inject_patch(HotSwapContext* ctx) {
#ifdef __linux__
    pid_t pid = ctx->target_pid;
    printf("[INJECTOR] Attaching to PID %d...\n", pid);

    // Step 1: Attach to the process
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
        perror("[ERROR] ptrace attach failed");
        return -1;
    }
    
    int status;
    waitpid(pid, &status, 0);
    printf("[INJECTOR] Attached. Target process halted.\n");

    // Step 2: Save original register context
    struct user_regs_struct regs, saved_regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &saved_regs) < 0) {
        perror("[ERROR] Failed to get registers");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return -1;
    }
    regs = saved_regs;

#if defined(__x86_64__)
    printf("[INJECTOR] Original RIP: %p\n", (void*)regs.rip);
    unsigned long target_ip = regs.rip;
#elif defined(__aarch64__)
    printf("[INJECTOR] Original PC: %p\n", (void*)regs.pc);
    unsigned long target_ip = regs.pc;
#else
    unsigned long target_ip = 0;
#endif

    // Step 3: Inject mmap syscall into the target's instruction pointer
    long saved_text = ptrace(PTRACE_PEEKTEXT, pid, (void*)target_ip, NULL);
    
    // Temporarily overwrite memory at current instruction pointer with syscall instructions
    long temp_text = saved_text;
    memcpy(&temp_text, syscall_ins, SYSCALL_OPCODE_SIZE);
    if (ptrace(PTRACE_POKETEXT, pid, (void*)target_ip, (void*)temp_text) < 0) {
        perror("[ERROR] Failed to write syscall instruction");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return -1;
    }

    // Set up registers for mmap(NULL, size, PROT_READ|WRITE|EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
#if defined(__x86_64__)
    // x86_64 ABI Syscall layout
    regs.rax = SYS_mmap; // 9
    regs.rdi = 0;
    regs.rsi = ctx->patch_size;
    regs.rdx = PROT_READ | PROT_WRITE | PROT_EXEC;
    regs.r10 = MAP_PRIVATE | MAP_ANONYMOUS;
    regs.r8  = -1;
    regs.r9  = 0;
#elif defined(__aarch64__)
    // aarch64 (ARM64) ABI Syscall layout:
    // X8 is syscall number (mmap on arm64 is 222)
    // X0-X5 are arguments
    regs.regs[8] = 222; // SYS_mmap on aarch64
    regs.regs[0] = 0;   // addr
    regs.regs[1] = ctx->patch_size;
    regs.regs[2] = PROT_READ | PROT_WRITE | PROT_EXEC;
    regs.regs[3] = MAP_PRIVATE | MAP_ANONYMOUS;
    regs.regs[4] = -1;
    regs.regs[5] = 0;
#endif

    if (ptrace(PTRACE_SETREGS, pid, NULL, &regs) < 0) {
        perror("[ERROR] Failed to set registers for mmap");
        goto restore_and_fail;
    }

    // Single step to execute mmap syscall
    if (ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL) < 0) {
        perror("[ERROR] ptrace single step failed");
        goto restore_and_fail;
    }
    waitpid(pid, &status, 0);

    // Get the address allocated by mmap
    struct user_regs_struct new_regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &new_regs) < 0) {
        perror("[ERROR] Failed to read registers after mmap");
        goto restore_and_fail;
    }
    
#if defined(__x86_64__)
    void* remote_addr = (void*)new_regs.rax;
#elif defined(__aarch64__)
    void* remote_addr = (void*)new_regs.regs[0]; // X0 contains return value on arm64
#else
    void* remote_addr = MAP_FAILED;
#endif

    if (remote_addr == MAP_FAILED || (long)remote_addr < 0) {
        fprintf(stderr, "[ERROR] Remote mmap failed inside target context (returned %p)\n", remote_addr);
        goto restore_and_fail;
    }
    printf("[INJECTOR] Executable page allocated inside target at: %p\n", remote_addr);

    // Step 4: Restore original instruction code at IP
    if (ptrace(PTRACE_POKETEXT, pid, (void*)target_ip, (void*)saved_text) < 0) {
        perror("[ERROR] Failed to restore original instructions");
        goto restore_and_fail;
    }

    // Step 5: Write the compiled patch bytes into the allocated executable memory page
    unsigned char* patch = (unsigned char*)ctx->patch_code;
    printf("[INJECTOR] Writing patch binary payload (size: %zu)...\n", ctx->patch_size);
    for (size_t i = 0; i < ctx->patch_size; i += sizeof(long)) {
        long word = 0;
        size_t chunk_size = (ctx->patch_size - i >= sizeof(long)) ? sizeof(long) : (ctx->patch_size - i);
        memcpy(&word, patch + i, chunk_size);
        
        if (ptrace(PTRACE_POKETEXT, pid, (void*)((uintptr_t)remote_addr + i), (void*)word) < 0) {
            perror("[ERROR] Failed to write patch instruction words");
            goto restore_and_fail;
        }
    }

    // Step 6: Overwrite entry point of the faulty function with a relative JMP instruction
    // Rel JMP rel32 opcode: 0xE9 followed by 4-byte offset
    // offset = remote_addr - fault_addr - 5 (size of JMP instruction)
    unsigned char jmp[8] = {0xE9};
    long offset = (long)remote_addr - (long)ctx->fault_addr - 5;
    memcpy(jmp + 1, &offset, 4);

    long original_func_word = ptrace(PTRACE_PEEKTEXT, pid, ctx->fault_addr, NULL);
    long target_jmp_word = 0;
    memcpy(&target_jmp_word, jmp, 5);
    // Preserving the remaining bytes of the original word
    memcpy((unsigned char*)&target_jmp_word + 5, (unsigned char*)&original_func_word + 5, sizeof(long) - 5);

    printf("[INJECTOR] Patching entry point at %p with relative JMP to %p\n", ctx->fault_addr, remote_addr);
    if (ptrace(PTRACE_POKETEXT, pid, ctx->fault_addr, (void*)target_jmp_word) < 0) {
        perror("[ERROR] Failed to inject JMP branch redirection hook");
        goto restore_and_fail;
    }

    // Step 7: Restore original registers and detach
    if (ptrace(PTRACE_SETREGS, pid, NULL, &saved_regs) < 0) {
        perror("[ERROR] Failed to restore registers");
    }
    
    ptrace(PTRACE_DETACH, pid, NULL, NULL);
    printf("[SUCCESS] Hot-swap injection successfully completed.\n");
    return 0;

restore_and_fail:
    ptrace(PTRACE_POKETEXT, pid, (void*)saved_regs.rip, (void*)saved_text);
    ptrace(PTRACE_SETREGS, pid, NULL, &saved_regs);
    ptrace(PTRACE_DETACH, pid, NULL, NULL);
    return -1;
#else
    printf("[INJECTOR] [MOCK] Attaching to PID %d...\n", ctx->target_pid);
    printf("[INJECTOR] [MOCK] Remote allocation mapped at: %p\n", (void*)0x7FFF004000);
    printf("[INJECTOR] [MOCK] Patch instructions written successfully.\n");
    printf("[SUCCESS] Hot-swap injection simulated on non-Linux platform.\n");
    return 0;
#endif
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printf("Usage: %s <pid> <hex_fault_addr> <hex_patch_bytes>\n", argv[0]);
        return 1;
    }

    pid_t pid = (pid_t)strtol(argv[1], NULL, 10);
    void* fault_addr = (void*)strtoul(argv[2], NULL, 16);
    char* raw_hex = argv[3];

    // Decode hex patch bytes
    size_t len = strlen(raw_hex) / 2;
    unsigned char* patch_bytes = malloc(len);
    for (size_t i = 0; i < len; i++) {
        unsigned int byte;
        sscanf(raw_hex + (i * 2), "%2x", &byte);
        patch_bytes[i] = (unsigned char)byte;
    }

    HotSwapContext ctx = {
        .target_pid = pid,
        .fault_addr = fault_addr,
        .patch_code = patch_bytes,
        .patch_size = len
    };

    int ret = inject_patch(&ctx);
    free(patch_bytes);
    return ret;
}
