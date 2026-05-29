# Simulated UAF detector - no kernel needed
heap = {}

def malloc(addr, size):
    heap[addr] = {"size": size, "freed": False}
    print(f"  malloc({addr}) → allocated {size} bytes")

def free(addr):
    if addr not in heap:
        print(f"  ❌ FAULT: double-free or invalid free at {addr}")
        return False
    heap[addr]["freed"] = True
    print(f"  free({addr}) → ok")
    return True

def write(addr, val):
    if addr in heap and heap[addr]["freed"]:
        print(f"  🚨 USE-AFTER-FREE DETECTED at {addr}!")
        print(f"     → daemon would patch this NOW")
        return False
    print(f"  write({addr}) = {val} → ok")
    return True

print("=== Simulating buggy program ===")
malloc(0x1000, 64)
free(0x1000)
write(0x1000, 'X')   # UAF — daemon pakdega

print("\n=== Simulating healthy program ===")
malloc(0x2000, 64)
write(0x2000, 'Y')   # ok
free(0x2000)
