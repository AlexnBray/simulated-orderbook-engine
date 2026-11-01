"""Generate a large SYNTHETIC OrderFilled dataset (decoded + raw-log CSV) for ingest throughput tests.

Usage: python3 bench/gen_synthetic_fills.py <num_events> <output_prefix>
  -> <output_prefix>_decoded.csv and <output_prefix>_raw.csv  (25% exchange taker legs)
"""
import random, sys
random.seed(1); n=int(sys.argv[1]); out=sys.argv[2]
T0="0xd0a08e8c493f9c94f29311604c9de1b4e8c8d4c06bd0c789af57f2d65bfec0f6"; EX="4bfb41d5b3570defd03c39a9a4d8de6bd8b8982e"
assets=[str(random.getrandbits(255)) for _ in range(50)]
def a(): return "%040x"%random.getrandbits(160)
def h(): return "%064x"%random.getrandbits(256)
with open(out+"_decoded.csv","w") as d, open(out+"_raw.csv","w") as r:
    d.write("timestamp,orderHash,maker,taker,makerAssetId,takerAssetId,makerAmountFilled,takerAmountFilled,fee\n")
    r.write("timestamp,topic0,topic1,topic2,topic3,data\n")
    ts=1730000000
    for i in range(n):
        ts+=random.randint(0,3); asset=random.choice(assets)
        shares=random.randint(1,5000)*10_000; px=random.randint(1,99)
        usdc=shares*px//100
        taker=EX if random.random()<0.25 else a(); maker=a()
        if random.random()<0.5: ma,ta,mam,tam="0",asset,usdc,shares
        else: ma,ta,mam,tam=asset,"0",shares,usdc
        oh=h()
        d.write(f"{ts},0x{oh},0x{maker},0x{taker},{ma},{ta},{mam},{tam},0\n")
        w=lambda v:"%064x"%int(v)
        r.write(f"{ts},{T0},0x{oh},0x{'0'*24}{maker},0x{'0'*24}{taker},0x{w(ma)}{w(ta)}{w(mam)}{w(tam)}{w(0)}\n")
