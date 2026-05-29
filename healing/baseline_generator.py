#!/usr/bin/env python3
import os
import sys
import subprocess
import json
from pathlib import Path

class BaselineGenerator:
    def __init__(self, binary_path: str):
        self.binary = binary_path
        self.db_path = "golden_paths.json"
    
    def generate_klee_bc(self) -> bool:
        """Compile to LLVM bitcode for KLEE"""
        c_file = f"{self.binary}.c"
        bc_file = f"{self.binary}.bc"
        
        if not os.path.exists(c_file):
            print(f"[ERROR] Source file {c_file} not found.")
            return False
            
        print(f"[BASELINE] Compiling {c_file} to LLVM bitcode...")
        try:
            subprocess.run([
                "clang", "-emit-llvm", "-c", "-g",
                "-O0", "-Xclang", "-disable-O0-optnone",
                c_file, "-o", bc_file
            ], check=True, capture_output=True)
            print(f"[SUCCESS] LLVM bitcode generated at {bc_file}")
            return True
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            print(f"[WARN] Local clang/LLVM environment not available or compile failed: {e}")
            return False
    
    def run_klee(self, max_time: int = 300) -> list:
        """Run KLEE symbolic execution and extract paths"""
        bc_file = f"{self.binary}.bc"
        if not os.path.exists(bc_file):
            print("[BASELINE] LLVM bitcode missing. Generating simulated baseline paths...")
            return self.generate_mock_baseline()
            
        print(f"[BASELINE] Invoking KLEE Symbolic Execution (Timeout: {max_time}s)...")
        try:
            subprocess.run([
                "klee",
                f"--max-time={max_time}",
                "--write-paths",
                "--output-dir=klee-out",
                "--optimize",
                bc_file
            ], check=True, capture_output=True)
            return self.parse_klee_paths("klee-out")
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            print(f"[WARN] KLEE not available on host system: {e}. Falling back to simulated golden paths...")
            return self.generate_mock_baseline()
    
    def parse_klee_paths(self, klee_dir: str) -> list:
        """Extract path constraints in SMT2 format from KLEE output files"""
        paths = []
        klee_path = Path(klee_dir)
        
        for path_file in klee_path.glob("*.path"):
            try:
                with open(path_file, 'r') as f:
                    paths.append({
                        "id": path_file.stem,
                        "constraints": f.read(),
                        "status": "VALID_PATH"
                    })
            except Exception as e:
                print(f"[ERROR] Failed reading KLEE path file: {e}")
                
        return paths
    
    def generate_mock_baseline(self) -> list:
        """Generate static symbolic constraints baseline for proof-of-concept"""
        return [
            {
                "id": "path_0",
                "constraints": "(assert (> x 0))\n(assert (< x 100))\n(assert (not (= ptr 0)))",
                "status": "VALID_PATH",
                "function": "process_packet"
            },
            {
                "id": "path_1",
                "constraints": "(assert (<= x 0))\n(assert (not (= ptr 0)))",
                "status": "VALID_PATH",
                "function": "process_packet"
            }
        ]
        
    def store_baseline(self, paths: list):
        print(f"[BASELINE] Saving {len(paths)} golden execution paths to {self.db_path}...")
        with open(self.db_path, "w") as f:
            json.dump(paths, f, indent=4)
        print("[SUCCESS] Baseline database stored successfully.")

if __name__ == "__main__":
    binary_target = "vulnerable_service"
    if len(sys.argv) > 1:
        binary_target = sys.argv[1]
        
    generator = BaselineGenerator(binary_target)
    generator.generate_klee_bc()
    paths = generator.run_klee()
    generator.store_baseline(paths)
