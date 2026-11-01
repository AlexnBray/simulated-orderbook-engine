"""Interleaved A/B benchmark runner (pinned to CPU 2, medians over rounds).

Usage: python3 bench/compare.py <dir-with-binaries> <prefills,comma,sep> <rounds> <binA,binB,...> [cpu=2]
Example: python3 bench/compare.py build-linux 0,10000,100000,1000000 5 orderbook_bench
"""
import subprocess, statistics, sys
S=sys.argv[1]; prefills=[int(x) for x in sys.argv[2].split(',')]; rounds=int(sys.argv[3]); bins=sys.argv[4].split(','); cpu=sys.argv[5] if len(sys.argv)>5 else '2'
cols=["prefill","ops","secs","ops_s","allocs","L50","L99","L999","M50","M99","M999","C50","C99","C999"]
res={}
for pf in prefills:
    for r in range(rounds):
        for b in bins:
            out=subprocess.run(["taskset","-c",cpu,f"{S}/{b}","1000000","42",str(pf),"--csv"],capture_output=True,text=True,check=True).stdout.strip()
            res.setdefault((pf,b),[]).append(dict(zip(cols,map(float,out.split(',')))))
med=lambda rows,k: statistics.median(r[k] for r in rows)
print(f"{'prefill':>8} {'binary':>13} {'Mops/s':>7} {'alloc/op':>8} | {'lim p50':>7} {'p99':>5} {'p99.9':>6} | {'mkt p50':>7} {'p99':>5} {'p99.9':>6} | {'cxl p50':>7} {'p99':>5} {'p99.9':>6}")
for pf in prefills:
    for b in bins:
        rows=res[(pf,b)]
        print(f"{pf:>8} {b:>13} {med(rows,'ops_s')/1e6:7.2f} {med(rows,'allocs'):8.3f} | "+" | ".join(" ".join(f"{med(rows,k):{w}.0f}" for k,w in zip([p+'50',p+'99',p+'999'],[7,5,6])) for p in "LMC"))
    # spread across runs for throughput
    for b in bins:
        v=[r['ops_s']/1e6 for r in res[(pf,b)]]
        print(f"{'':>8} {b:>13} throughput range {min(v):.2f}-{max(v):.2f} Mops/s over {len(v)} runs")
