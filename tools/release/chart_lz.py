"""Optimal-parse encoder for the chart LZ format (see lzenc.py for the token layout)."""
def lz_encode_opt(data):
    n = len(data)
    INF = 1 << 60
    heads = {}
    longm = [None] * n      # (max_len, dist) for the long form
    shortm = [None] * n     # (max_len, dist) for the short form
    for i in range(n):
        if i + 3 <= n:
            key = bytes(data[i:i + 3])
            bl, bd = 0, 0
            for j in reversed(heads.get(key, [])[-256:]):
                d = i - j
                if d > 1023:
                    break
                l = 3
                while l < 34 and i + l < n and data[i - d + l] == data[i + l]:
                    l += 1
                if l > bl:
                    bl, bd = l, d
                if l == 34:
                    break
            if bl >= 3:
                longm[i] = (bl, bd)
            heads.setdefault(key, []).append(i)
        best = None
        for d in range(1, min(i, 16) + 1):
            l = 0
            while l < 5 and i + l < n and data[i - d + l] == data[i + l]:
                l += 1
            if l >= 2 and (best is None or l > best[0]):
                best = (l, d)
        shortm[i] = best
    cost = [INF] * (n + 1)
    how = [None] * (n + 1)
    cost[n] = 0
    for i in range(n - 1, -1, -1):
        c, h = INF, None
        for k in range(1, min(70, n - i) + 1):          # literals
            ck = 9 * k if k < 8 else 9 + 8 * k
            if cost[i + k] + ck < c:
                c, h = cost[i + k] + ck, ('lit', k)
        if shortm[i]:
            for l in range(2, shortm[i][0] + 1):
                if cost[i + l] + 9 < c:
                    c, h = cost[i + l] + 9, ('short', l, shortm[i][1])
        if longm[i]:
            for l in range(3, longm[i][0] + 1):
                if cost[i + l] + 17 < c:
                    c, h = cost[i + l] + 17, ('long', l, longm[i][1])
        cost[i], how[i] = c, h
    out = bytearray(); st = dict(pos=0, ctrl=0, bit=0)

    def flag(v):
        if st['bit'] == 0:
            st['pos'] = len(out); out.append(0); st['ctrl'] = 0
        if v:
            st['ctrl'] |= 1 << st['bit']; out[st['pos']] = st['ctrl']
        st['bit'] = (st['bit'] + 1) % 8

    i = 0
    while i < n:
        h = how[i]
        if h[0] == 'lit':
            k = h[1]
            if k < 8:
                for b in data[i:i + k]:
                    flag(0); out.append(b)
            else:
                flag(1); out.append(0xb9 + k - 1); out.extend(data[i:i + k])
            i += k
        elif h[0] == 'short':
            flag(1); out.append(((h[1] + 6) << 4) | (h[2] - 1)); i += h[1]
        else:
            flag(1); out.append(((h[1] - 3) << 2) | (h[2] >> 8)); out.append(h[2] & 0xff); i += h[1]
    flag(1); out.append(0xff)
    return bytes(out)
