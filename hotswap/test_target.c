#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>

// Buggy pointer trigger
int* global_ptr = NULL;

/*
 * vulnerable_function
 * Standard target function which executes a null dereference under certain inputs.
 * We print its address at startup to make hot-swapping simple.
 */
void vulnerable_function() {
    printf("[TARGET] Entering vulnerable_function()...\n");
    // Dereferencing global_ptr (which is NULL at start)
    int value = *global_ptr; 
    printf("[TARGET] Successfully dereferenced! Value: %d\n", value);
}

/*
 * sigsegv_handler
 * Rescues the process from crash-termination by raising SIGSTOP.
 * This suspends the process and alerts the daemon (via waitpid) to begin healing.
 */
void sigsegv_handler(int sig) {
    printf("\n[TARGET] 🚨 Intercepted Segmentation Fault (SIGSEGV). Raising SIGSTOP to suspend thread...\n");
    printf("[TARGET] Waiting for Self-Healing Daemon to hot-swap patch...\n");
    
    // Stop the process in place so ptrace can attach safely
    raise(SIGSTOP);
    
    printf("[TARGET] 🧬 Process resumed! Re-executing function...\n");
}

int main() {
    // Register custom fault suspension handler
    signal(SIGSEGV, sigsegv_handler);

    printf("=============================================================\n");
    printf("        🎯 VULNERABLE TARGET APPLICATION STARTED 🎯        \n");
    printf("=============================================================\n");
    printf("  ├─ Process PID:      %d\n", getpid());
    printf("  ├─ Target Function:  vulnerable_function at %p\n", (void*)&vulnerable_function);
    printf("  └─ Trigger Pointer:  global_ptr value at %p\n", (void*)&global_ptr);
    printf("=============================================================\n");

    int iterations = 0;
    while (1) {
        iterations++;
        printf("\n[TARGET] Iteration %d: Doing work...\n", iterations);
        sleep(2);

        if (iterations == 3) {
            printf("[TARGET] Iteration 3: Triggering Null Pointer Dereference...\n");
            vulnerable_function();
        }
        
        // Simulating the effect of a successful heal (global_ptr is set)
        if (global_ptr == NULL) {
            static int fixed_val = 42;
            printf("[TARGET] Note: pointer is NULL. Real daemon will inject direct JMP past entry.\n");
        }
    }

    return 0;
}
