use std::io;

#[cfg(target_os = "linux")]
use nix::{
    sys::{
        ptrace,
        wait::waitpid,
        signal::Signal,
    },
    unistd::Pid,
};

#[cfg(target_os = "linux")]
pub fn hot_swap_patch(pid: u32, fault_addr: u64, patch_bytes: &[u8]) -> io::Result<()> {
    let target_pid = Pid::from_raw(pid as i32);
    println!("[INJECTOR] Attaching to process PID {} using PTRACE_ATTACH...", pid);
    
    ptrace::attach(target_pid).map_err(|e| io::Error::new(io::ErrorKind::PermissionDenied, e))?;
    
    // Wait for the target process to stop
    waitpid(target_pid, None).map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
    println!("[INJECTOR] Target process halted. Fetching current CPU register states...");
    
    // Save original registers
    let mut regs = ptrace::getregs(target_pid)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
    let saved_regs = regs.clone();
    
    println!("[INJECTOR] Registers captured. RIP at 0x{:X}", regs.rip);
    
    // Calculate new mmap size
    let patch_size = patch_bytes.len();
    
    // Injecting `sys_mmap` syscall into target context (x86_64 ABI layout):
    // RAX = 9 (sys_mmap)
    // RDI = 0 (Address hint)
    // RSI = patch_size (Allocation size)
    // RDX = 7 (PROT_READ | PROT_WRITE | PROT_EXEC)
    // R10 = 0x22 (MAP_PRIVATE | MAP_ANONYMOUS)
    // R8  = -1 (File descriptor)
    // R9  = 0 (Offset)
    regs.rax = 9;
    regs.rdi = 0;
    regs.rsi = patch_size as u64;
    regs.rdx = 7;
    regs.r10 = 0x22;
    regs.r8 = u64::MAX; // -1
    regs.r9 = 0;
    
    // Save current instructions at RIP to restore later
    let original_instr = ptrace::read(target_pid, regs.rip as ptrace::AddressType)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
        
    // Write `syscall` (0x0F, 0x05) instruction at RIP
    // Since ptrace writes word-sized blocks (u64 / i64), we write the syscall opcodes
    let syscall_opcode = 0x050F; // Little endian 0x0F, 0x05
    unsafe {
        ptrace::write(
            target_pid,
            regs.rip as ptrace::AddressType,
            syscall_opcode as ptrace::AddressType,
        ).map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
    }
    
    // Update registers to execute mmap syscall
    ptrace::setregs(target_pid, regs)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
        
    // Execute a single step
    ptrace::step(target_pid, None)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
    waitpid(target_pid, None).map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
    
    // Retrieve result register (RAX contains the newly allocated address)
    let new_regs = ptrace::getregs(target_pid)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
    let allocated_addr = new_regs.rax;
    
    println!("[INJECTOR] Successfully allocated executable region at 0x{:X} via remote mmap", allocated_addr);
    
    // Restore the original instruction that was at RIP
    unsafe {
        ptrace::write(
            target_pid,
            saved_regs.rip as ptrace::AddressType,
            original_instr as ptrace::AddressType,
        ).map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
    }
    
    // Write the patch machine code into the allocated address
    println!("[INJECTOR] Writing patch code bytes (len: {}) to 0x{:X}...", patch_size, allocated_addr);
    for i in (0..patch_size).step_by(8) {
        let mut chunk = [0u8; 8];
        let bytes_to_copy = std::cmp::min(8, patch_size - i);
        chunk[..bytes_to_copy].copy_from_slice(&patch_bytes[i..i + bytes_to_copy]);
        let val = u64::from_le_bytes(chunk);
        
        unsafe {
            ptrace::write(
                target_pid,
                (allocated_addr + i as u64) as ptrace::AddressType,
                val as ptrace::AddressType,
            ).map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
        }
    }
    
    // Inject relative JMP from faulty function entry address to our new patched function
    // relative offset = allocated_addr - fault_addr - 5 (size of JMP rel32)
    // Opcode for JMP rel32 is 0xE9 followed by 32-bit signed offset
    println!("[INJECTOR] Redirecting execution: Writing relative JMP at 0x{:X} to point to 0x{:X}", fault_addr, allocated_addr);
    let relative_offset = (allocated_addr as i64) - (fault_addr as i64) - 5;
    let mut jmp_instruction = [0u8; 8];
    jmp_instruction[0] = 0xE9; // JMP opcode
    jmp_instruction[1..5].copy_from_slice(&(relative_offset as i32).to_le_bytes());
    
    // Keep trailing bytes of original word to avoid garbage writes
    let original_word = ptrace::read(target_pid, fault_addr as ptrace::AddressType)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e))? as u64;
    jmp_instruction[5..8].copy_from_slice(&original_word.to_le_bytes()[5..8]);
    
    let jmp_val = u64::from_le_bytes(jmp_instruction);
    unsafe {
        ptrace::write(
            target_pid,
            fault_addr as ptrace::AddressType,
            jmp_val as ptrace::AddressType,
        ).map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
    }
    
    // Restore registers & resume
    println!("[INJECTOR] Detaching and resuming target process...");
    ptrace::setregs(target_pid, saved_regs)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
    ptrace::detach(target_pid, None)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
        
    println!("[SUCCESS] Hot-swap injection complete.");
    Ok(())
}

#[cfg(not(target_os = "linux"))]
pub fn hot_swap_patch(pid: u32, fault_addr: u64, _patch_bytes: &[u8]) -> io::Result<()> {
    println!("[INJECTOR] [MOCK] Interfacing with PID {} memory table", pid);
    println!("[INJECTOR] [MOCK] Attaching to thread structures...");
    println!("[INJECTOR] [MOCK] Injecting mmap memory allocation request for 256 bytes.");
    println!("[INJECTOR] [MOCK] Overwriting faulty address 0x{:X} with jump instruction: `jmp rel32 0x{:X}`", fault_addr, fault_addr + 0x4000);
    println!("[INJECTOR] [MOCK] Resuming application threads.");
    Ok(())
}
