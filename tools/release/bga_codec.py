"""Native PS2 LZSS encoder, unchanged optimal parser from tools/lanetex.py."""
def lzss_encode(data, chain_limit=2048):
    """Optimal-parse LZSS matching decode_lzss (ring buffer 0x1000 starting at 0xfee, len 3..18)."""
    n = len(data)
    # longest match at every position, searching back up to 4095 bytes
    heads = {}
    best_len = [0] * n
    best_pos = [0] * n
    for i in range(n):
        if i + 3 <= n:
            key = data[i:i + 3]
            chain = heads.setdefault(key, [])
            bl, bp = 0, 0
            for j in reversed(chain[-chain_limit:]):
                if i - j > 4095:
                    break
                l = 3
                while l < 18 and i + l < n and data[j + l] == data[i + l]:
                    l += 1
                if l > bl:
                    bl, bp = l, j
                    if l == 18:
                        break
            best_len[i], best_pos[i] = bl, bp
            chain.append(i)
    # shortest path in bits: literal 9, match 17
    INF = 1 << 60
    cost = [INF] * (n + 1)
    step = [0] * (n + 1)
    cost[n] = 0
    for i in range(n - 1, -1, -1):
        c, st = cost[i + 1] + 9, 1
        for l in range(3, best_len[i] + 1):
            cl = cost[i + l] + 17
            if cl < c:
                c, st = cl, l
        cost[i], step[i] = c, st
    out = bytearray()
    ctrl_pos, ctrl, bit = 0, 0, 0
    i = 0
    while i < n:
        if bit == 0:
            ctrl_pos = len(out)
            out.append(0)
            ctrl = 0
        l = step[i]
        if l == 1:
            ctrl |= 1 << bit
            out[ctrl_pos] = ctrl
            out.append(data[i])
        else:
            ring = (0xfee + best_pos[i]) & 0xfff
            out.append(ring & 0xff)
            out.append(((ring >> 4) & 0xf0) | (l - 3))
        bit = (bit + 1) % 8
        i += l
    return bytes(out)

