# =====================================================================
#  🧬 SELF-HEALING SOFTWARE RUNTIME - GLOBAL COMPILATION SYSTEM 🧬
# =====================================================================

CC = gcc
CLANG = clang
CARGO = cargo
CFLAGS = -Wall -Wextra -O2 -g
BPF_CFLAGS = -target bpf -O2 -g -I/usr/include

# Targets
DAEMON_DIR = daemon
HOTSWAP_DIR = hotswap
EBPF_DIR = ebpf

TARGET_BIN = $(HOTSWAP_DIR)/test_target
INJECTOR_BIN = $(HOTSWAP_DIR)/patch_injector
BPF_OBJ = $(EBPF_DIR)/fault_detector.bpf.o

.PHONY: all clean run-daemon run-target run-benchmark build-bpf

all: $(TARGET_BIN) $(INJECTOR_BIN) build-daemon

# Compile vulnerable target client
$(TARGET_BIN): $(HOTSWAP_DIR)/test_target.c
	@mkdir -p $(HOTSWAP_DIR)
	@echo "[BUILD] Compiling vulnerable target: $(TARGET_BIN)"
	$(CC) $(CFLAGS) $< -o $@

# Compile low-level ptrace injector
$(INJECTOR_BIN): $(HOTSWAP_DIR)/patch_injector.c
	@mkdir -p $(HOTSWAP_DIR)
	@echo "[BUILD] Compiling hot-swapper injector: $(INJECTOR_BIN)"
	$(CC) $(CFLAGS) $< -o $@

# Compile kernel space eBPF program (Linux only)
build-bpf: $(EBPF_DIR)/fault_detector.bpf.c
	@mkdir -p $(EBPF_DIR)
	@echo "[BUILD] Compiling eBPF program: $(BPF_OBJ)"
	@if [ "$$(uname)" = "Linux" ]; then \
		$(CLANG) $(BPF_CFLAGS) -c $< -o $(BPF_OBJ); \
	else \
		echo "[BUILD] [WARN] OS is not Linux, skipping actual kernel eBPF compilation."; \
	fi

# Compile userspace Rust orchestrator daemon
build-daemon:
	@echo "[BUILD] Compiling Rust userspace daemon in $(DAEMON_DIR)..."
	@cd $(DAEMON_DIR) && $(CARGO) build --release

# Execution commands
run-daemon: all
	@echo "[RUN] Executing Self-Healing userspace daemon..."
	@cd $(DAEMON_DIR) && $(CARGO) run --release

run-target: $(TARGET_BIN)
	@echo "[RUN] Launching vulnerable target process..."
	@./$(TARGET_BIN)

run-benchmark:
	@echo "[RUN] Executing MTTR recovery comparison benchmark suite..."
	@python3 benchmark/mttr_comparison.py

clean:
	@echo "[CLEAN] Purging compiled binaries and build artifacts..."
	@rm -rf $(HOTSWAP_DIR)/test_target $(HOTSWAP_DIR)/patch_injector $(EBPF_DIR)/*.o
	@cd $(DAEMON_DIR) && $(CARGO) clean
	@rm -rf klee-out klee-last golden_paths.json
