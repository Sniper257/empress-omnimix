"""EMPRESS off-disc USB: omit the obsolete sceCdStInit IOP ring allocation.

The queue/worker remains native. All libcdvd streaming operations must already
be redirected to vdisc; vd_st_init has no IOP-ring consumer. Keep opt-in until
hardware qualification. Never apply to a retail disc or unknown executable.
"""
ALLOC_SITE=0x14d300
ALLOC_BYTES=0x28010
GLOBAL=0x78b6f0
ZERO_RETURN=0x0000102d # daddu v0,zero,zero; delay-slot size setup stays native
STREAM={0x14ca90:(0x11e2b0,'vd_st_init'),0x14caa4:(0x11e2e0,'vd_st_start'),
        0x14c984:(0x11e318,'vd_st_seek'),0x14c9b0:(0x11e3b0,'vd_st_read'),
        0x14cab4:(0x11e378,'vd_st_stop'),0x14cad8:(0x11e378,'vd_st_stop'),
        0x14cda8:(0x11e378,'vd_st_stop'),0x14cdc8:(0x11e378,'vd_st_stop')}
def patches(word,jals):
    jal=lambda a:0x0c000000|a>>2
    # Allocation modeFIRST/address0, request160KiB+16B; alignment code remains.
    expected={0x14d2e8:0x0000202d,0x14d2f8:0x0000302d,
              0x14d2fc:0x3c020002,ALLOC_SITE:jal(0x117190),0x14d304:0x34458010,
              0x14d308:0x2444000f,0x14d318:0x00822024,
              0x14d324:0xac64b6f0,
              0x14ca54:0x3c110079,0x14ca5c:0x2631b6f0,0x14ca88:0x8e260000,
              0x14ca8c:0x24040050,0x14ca94:0x24050005}
    for a,w in expected.items():assert word(a)==w,('unknown optical-ring layout',hex(a),hex(word(a)),hex(w))
    for a,(target,name)in STREAM.items():
        assert word(a)==jal(target)and jals.get(a)==(target,name),('ring consumer is not redirected',hex(a))
    return {ALLOC_SITE:(jal(0x117190),ZERO_RETURN)}
