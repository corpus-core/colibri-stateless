import re
import sys

def parse_trace(filename):
    steps = {}
    with open(filename, 'r') as f:
        content = f.read()
    
    pattern = re.compile(r"Step (\d+) after (DBL|ADD):\s*\n([0-9a-fA-F]+)")
    matches = pattern.findall(content)
    
    for step, op, hex_val in matches:
        key = f"{step}_{op}"
        steps[key] = hex_val.strip()
        
    return steps

def compare_traces(file_c, file_py):
    trace_c = parse_trace(file_c)
    trace_py = parse_trace(file_py)
    
    print(f"Parsed {len(trace_c)} steps from C trace.")
    print(f"Parsed {len(trace_py)} steps from Python trace.")
    
    # Sort keys: Step descending, then DBL before ADD.
    # DBL > ADD in string comparison. So (step, op) descending works.
    # (64, 'DBL') > (64, 'ADD').
    
    unique_keys = list(set(trace_c.keys()) | set(trace_py.keys()))
    
    def sort_key(k):
        step, op = k.split('_')
        return (int(step), op)
        
    all_keys = sorted(unique_keys, key=sort_key, reverse=True)
    
    first_mismatch = True
    
    for key in all_keys:
        step, op = key.split('_')
        val_c = trace_c.get(key)
        val_py = trace_py.get(key)
        
        print(f"Checking Step {step} {op}...")
        
        if val_c and val_py:
            if val_c != val_py:
                print(f"Mismatch at Step {step} {op}:")
                print(f"C : {val_c}")
                print(f"Py: {val_py}")
                
                # Find first diff index
                min_len = min(len(val_c), len(val_py))
                for i in range(min_len):
                    if val_c[i] != val_py[i]:
                        print(f"Difference starts at char index {i}")
                        # Print surrounding context
                        start = max(0, i - 10)
                        end = min(min_len, i + 40)
                        print(f"C context : ...{val_c[start:end]}...")
                        print(f"Py context: ...{val_py[start:end]}...")
                        break
                if first_mismatch:
                    return # Stop after first mismatch
            else:
                print(f"Step {step} {op}: MATCH")
        else:
            if not val_c:
                print(f"Step {step} {op}: Missing in C trace")
            if not val_py:
                print(f"Step {step} {op}: Missing in Py trace")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python compare_traces.py <trace_c.txt> <trace_py.txt>")
        sys.exit(1)
        
    compare_traces(sys.argv[1], sys.argv[2])
