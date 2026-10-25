"""Is the hot code latency-bound or issue-bound?  An instrument, not a test.

Run by hand when deciding whether reordering instructions could pay:

    python3 test/tools/latency_model.py DIR FUNCINDEX

where DIR holds `mathfn.o` (warmc, `-c --no-function-names`, so that its
functions' symbols are `wasm_func_N` by index) and `mfg.cwasm` (`wasmtime
compile`) for the same module.  It prints, per basic block and for both engines' orders, the
instruction count, the dependence height in cycles, and the cycles the block would
need if only issue width bound it.

Why it exists: a scheduler moves instructions between issue slots and cannot
change what depends on what, so it can only help where a block's cycles are set by
*stalls* rather than by its dependence height.  Asked of `asbench/mathfn`, every
hot block in both engines came out latency-bound by four to eight times -- 68
cycles of chain against a 16-cycle issue bound in the biggest -- which is what
retired the idea of writing one.  See OPTIMIZATION.md.

The model: RAW dependences only, since an out-of-order core renames WAR and WAW
away; a latency per instruction class; 8-wide issue; one block at a time, which
understates a core that reorders across branches.  All three simplifications
flatter the scheduler, and the answer was still no.  It is robust to the latency
guesses -- halving every one of them leaves the same verdict.
"""
import re, subprocess, sys, collections

LAT = {}
def lat(op):
    if op in ("fdiv","fsqrt"): return 12
    if op.startswith("fmadd") or op.startswith("fmsub") or op.startswith("fnmadd"): return 4
    if op.startswith("fmul") or op.startswith("fadd") or op.startswith("fsub") or op.startswith("fcvt") \
       or op.startswith("scvtf") or op.startswith("ucvtf") or op.startswith("fabs") or op.startswith("fneg") \
       or op.startswith("fcmp") or op.startswith("fmax") or op.startswith("fmin"): return 3
    if op.startswith("ld"): return 4
    if op in ("fmov","mov","movz","movk","movn"): return 1
    if op.startswith("mul") or op.startswith("smull") or op.startswith("umull") or op.startswith("madd"): return 3
    if op.startswith("sdiv") or op.startswith("udiv"): return 8
    return 1

DEF_ONLY = ("st","cmp","tst","b","cb","tb","ret","brk","nop","udf")

def parse(path, lo, hi):
    dis = subprocess.run(["objdump","-d",path],capture_output=True,text=True).stdout
    out=[]
    for ln in dis.splitlines():
        m=re.match(r"\s*([0-9a-f]+):\s+[0-9a-f]+\s+(\S+)\s*(.*)",ln)
        if not m: continue
        off=int(m.group(1),16)
        if not (lo<=off<hi): continue
        op=m.group(2); rest=m.group(3).split(';')[0].strip()
        out.append((off,op,rest))
    return out

def regs(rest):
    return re.findall(r"\b([wxdsqv]\d+)\b", rest)
def norm(r):
    return re.sub(r"^[wxdsqv]", "", r)

def blocks(ins):
    targets=set()
    for off,op,rest in ins:
        if op.startswith("b") or op.startswith("cb") or op.startswith("tb"):
            m=re.search(r"0x([0-9a-f]+)",rest)
            if m: targets.add(int(m.group(1),16))
    bl=[]; cur=[]
    for i,(off,op,rest) in enumerate(ins):
        if off in targets and cur: bl.append(cur); cur=[]
        cur.append((off,op,rest))
        if op in ("b","ret","br") or op.startswith("b.") or op.startswith("cb") or op.startswith("tb"):
            bl.append(cur); cur=[]
    if cur: bl.append(cur)
    return bl

def analyse(bl):
    """critical path (cycles) and instruction count for one block"""
    ready={}
    finish=[]
    n=0
    for off,op,rest in bl:
        n+=1
        rs=regs(rest)
        if not rs:
            finish.append(1); continue
        is_store_or_cmp = any(op.startswith(p) for p in DEF_ONLY)
        srcs = rs if is_store_or_cmp else rs[1:]
        start=0
        for r in srcs:
            start=max(start, ready.get(norm(r),0))
        end=start+lat(op)
        finish.append(end)
        if not is_store_or_cmp and rs:
            ready[norm(rs[0])]=end
    crit=max(finish) if finish else 0
    return n, crit

def report(name, path, lo, hi):
    ins=parse(path,lo,hi)
    bl=blocks(ins)
    bl=sorted(bl,key=len,reverse=True)[:6]
    print("  %s: %d instructions in %d blocks; biggest blocks:"%(name,len(ins),len(blocks(ins))))
    tn=tc=0
    for b in bl:
        n,crit=analyse(b)
        tn+=n; tc+=crit
        issue=(n+7)//8
        bound = "latency" if crit>=issue else "issue"
        print("     %4d instr  critical path %4d cy  issue bound %3d cy  -> %s"%(n,crit,issue,bound))
    print("     top-6 total: %d instr, %d cy of critical path (%.2f instr/cy achievable)"%(tn,tc,tn/max(tc,1)))

if __name__=="__main__":
    S=sys.argv[1]; fi=int(sys.argv[2])
    out=subprocess.run(["nm","-n",S+"/mathfn.o"],capture_output=True,text=True).stdout
    fs=[]
    for ln in out.splitlines():
        m=re.match(r"([0-9a-f]+) t _wasm_func_(\d+)",ln)
        if m: fs.append((int(m.group(1),16),int(m.group(2))))
    fs.sort()
    lo=hi=None
    for i,(a,f) in enumerate(fs):
        if f==fi: lo=a; hi=fs[i+1][0] if i+1<len(fs) else 1<<30
    print("warmc:")
    report("f%d"%fi, S+"/mathfn.o", lo, hi)
    out=subprocess.run(["objdump","-t",S+"/mfg.cwasm"],capture_output=True,text=True).stdout
    for ln in out.splitlines():
        m=re.match(r"([0-9a-f]+)\s+l\s+F\s+\.text\s+([0-9a-f]+)\s+wasm\[0\]::function\[(\d+)\]",ln)
        if m and int(m.group(3))==fi:
            a=int(m.group(1),16); sz=int(m.group(2),16)
            print("wasmtime:")
            report("function[%d]"%fi, S+"/mfg.cwasm", a, a+sz)
