"""Bounded original chart/tile LZ decoder; no cipher dependency."""
MAX_BYTES=32*1024*1024

def decode_lz(data):
    """Bounded chart-LZ reader; preserve all decompressed bytes through EOF token."""
    at=0;out=bytearray();control=0
    try:
        while True:
            control>>=1
            if not control&0x100:control=data[at]|0xff00;at+=1
            token=data[at];at+=1
            if not control&1:out.append(token)
            elif token==255:return bytes(out)
            elif token<0xc0:
                if token<0x80:distance=((token&3)<<8)|data[at];at+=1;length=(token>>2)+3
                else:distance=(token&15)+1;length=(token>>4)-6
                if not 0<distance<=len(out):raise ValueError('Invalid chart LZ back reference')
                for _ in range(length):out.append(out[-distance])
            else:
                length=token-0xb9+1
                if at+length>len(data):raise ValueError('Truncated chart LZ literals')
                out.extend(data[at:at+length]);at+=length
            if len(out)>MAX_BYTES:raise ValueError('Chart LZ output exceeds limit')
    except IndexError as e:raise ValueError('Truncated chart LZ data')from e

