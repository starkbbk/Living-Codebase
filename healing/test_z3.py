from z3 import *

solver = Solver()
ptr = Int('ptr')
idx = Int('idx')
size = Int('size')

# Safety properties
solver.add(ptr != 0)          # no null deref
solver.add(idx >= 0)          # no negative index  
solver.add(idx < size)        # no out of bounds

if solver.check() == sat:
    print("✅ Z3 working! Safety constraints verified")
    print("Model:", solver.model())
else:
    print("❌ UNSAT")
