use std::sync::mpsc;
use std::thread;
use std::time::Duration;
use crate::FaultEvent;

pub fn run_darwin_simulator(tx: mpsc::Sender<FaultEvent>) {
    println!("[SIMULATOR] Darwin Mock Host initialized.");
    println!("[SIMULATOR] Awaiting mock client startup...");
    
    // Sleep briefly to simulate the target process starting up and running normal operations
    thread::sleep(Duration::from_secs(3));
    
    println!("\n[SIMULATOR] Target Process PID 94812 started executing `vulnerable_service`...");
    thread::sleep(Duration::from_secs(2));
    
    println!("[SIMULATOR] Normal execution paths checked. Golden path verified.");
    thread::sleep(Duration::from_secs(2));

    // Scenario 1: A Null Pointer Dereference in process memory
    println!("[SIMULATOR] 💥 Target PID 94812 attempted pointer dereference at NULL page!");
    
    let event = FaultEvent {
        pid: 94812,
        addr: 0x1004018A0, // Hypothetical address of `process_packet` function
        timestamp: 1685361284000,
        fault_class: 0x0A, // Memory corruption
        func_name: "process_packet".to_string(),
    };
    
    tx.send(event).unwrap();
    
    // Keep thread alive for future mock events
    loop {
        thread::sleep(Duration::from_secs(3600));
    }
}
