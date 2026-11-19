"""Lossless aligned XOR/zlib deltas for fixed inputs. Standard library only.

Some audio migrations change only a few bytes in each ADPCM frame; a compressed
XOR mask is much smaller than encoding each short unchanged run as VCDIFF.
"""
import zlib

CHUNK=1024*1024


def difference(target,source,shift=0):
    out=zlib.compressobj(9);parts=[]
    for at in range(0,len(target),CHUNK):
        chunk=target[at:at+CHUNK];base=aligned(source,at+shift,len(chunk))
        delta=(int.from_bytes(chunk,'little')^int.from_bytes(base,'little')).to_bytes(len(chunk),'little')
        parts.append(out.compress(delta))
    parts.append(out.flush());return b''.join(parts)


def aligned(source,start,count):
    left=min(count,max(0,-start));at=max(0,start)
    n=min(count-left,max(0,len(source)-at))
    return bytes(left)+source[at:at+n]+bytes(count-left-n)


def decode(patch,source,source_size,target_size,shift=0):
    d=zlib.decompressobj();pending=patch;done=0
    while pending or not d.eof:
        chunk=d.decompress(pending,CHUNK);pending=d.unconsumed_tail
        if not chunk:
            if not pending:break
            continue
        if done+len(chunk)>target_size:raise ValueError('XOR delta exceeds target')
        start=done+shift;left=min(len(chunk),max(0,-start));at=max(0,start)
        n=min(len(chunk)-left,max(0,source_size-at))
        source.seek(min(at,source_size));raw=source.read(n)
        if len(raw)!=n:raise ValueError('Short XOR source read')
        base=bytes(left)+raw+bytes(len(chunk)-left-n)
        yield (int.from_bytes(chunk,'little')^int.from_bytes(base,'little')).to_bytes(len(chunk),'little')
        done+=len(chunk)
        if d.eof:break
    if not d.eof or d.unused_data or pending or done!=target_size:
        raise ValueError('Truncated/trailing XOR delta')
