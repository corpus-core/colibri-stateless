import re
import sys

def extract_lc(filename, label_prefix):
    with open(filename, 'r') as f:
        content = f.read()
    
    if "trace_c.txt" in filename:
        pattern = re.compile(r"DEBUG C DBL: l_c = ([0-9a-fA-F]+), ([0-9a-fA-F]+)")
    else:
        pattern = re.compile(r"DEBUG: l_c = ([0-9a-fA-F]+), ([0-9a-fA-F]+)")
        
    match = pattern.search(content)
    if match:
        return match.group(1), match.group(2)
    return None, None

def compare_lc(file_c, file_py):
    c_re, c_im = extract_lc(file_c, "C DBL")
    py_re, py_im = extract_lc(file_py, "")
    
    if c_re is None or py_re is None:
        print("Could not extract l_c")
        return

    print(f"C  l_c Re: {c_re} (Len: {len(c_re)})")
    print(f"Py l_c Re: {py_re} (Len: {len(py_re)})")
    
    if c_re != py_re:
        print("Mismatch in Real part!")
        for i in range(min(len(c_re), len(py_re))):
            if c_re[i] != py_re[i]:
                print(f"Diff at index {i}: C '{c_re[i]}' vs Py '{py_re[i]}'")
                break
    else:
        print("Real part MATCH")

if __name__ == "__main__":
    compare_lc("trace_c.txt", "trace_py.txt")
