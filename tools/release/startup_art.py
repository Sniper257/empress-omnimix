"""Build the welcome screen from owning-ISO art and the author's text overlay.

The overlay is a bounded zlib RGBA image in JSON, without a retail background.
Native metadata, other sprites and decoded allocation sizes remain unchanged.
"""
import base64,hashlib,json,struct,zlib
from text_native import Container,offset,swizzle
from native_sprite import sprite
from bga_quantize import quantize
from bga_codec import lzss_encode

FORMAT='megamix/authored-rgba-overlay-v1'

def overlay(data):
    if len(data)>2*1024*1024:raise ValueError('Overlay document exceeds bounds')
    doc=json.loads(data)
    if doc.get('format')!=FORMAT or doc.get('size')!=[640,480]:raise ValueError('Unsupported startup overlay')
    packed=base64.b64decode(doc['rgba_zlib'],validate=True)
    dec=zlib.decompressobj();raw=dec.decompress(packed,640*480*4+1)
    if len(raw)!=640*480*4 or not dec.eof or dec.unused_data or dec.unconsumed_tail:raise ValueError('Invalid startup overlay length')
    if hashlib.sha256(raw).hexdigest()!=doc['rgba_sha256']:raise ValueError('Startup overlay hash mismatch')
    return raw

def blend(background,text):
    if len(background)*4!=len(text):raise ValueError('Startup image sizes differ')
    out=bytearray(len(text))
    for i,p in enumerate(background):
        if p>>24!=128:raise ValueError('Expected opaque native startup background')
        at=i*4;a=text[at+3]
        for k in range(3):out[at+k]=(((p>>(k*8))&255)*(255-a)+text[at+k]*a+127)//255
        out[at+3]=255
    return bytes(out)

def compose(original,background,overlay_data,capacity):
    if type(capacity)is not int or not 0<capacity<=2*1024*1024 or capacity%2048:raise ValueError('Invalid startup extent capacity')
    native=Container(original)
    if len(native.chunks)!=6 or native.rects[4]!=(0,4096,640,480) or native.sizes[4]!=(640,512):raise ValueError('Unexpected startup sprite geometry')
    w,h,pixels=sprite(background,2)
    if (w,h)!=(640,480):raise ValueError('Unexpected startup background geometry')
    original_sheet=native.chunk(5)
    if len(original_sheet)!=1024+640*512:raise ValueError('Unexpected startup sheet format')
    pal=native.palette(4)
    if any(pal[original_sheet[1024+offset(x,y,640)]]!=(0,0,0,128)for y in range(480,512)for x in range(640)):raise ValueError('Startup tail is not opaque black')
    indices,palette=quantize(blend(pixels,overlay(overlay_data)))
    # Reserve the final colour as exact opaque black for the unused32 rows.
    palette=palette[:1020]+bytes((0,0,0,128));sheet=bytearray(1024+640*512)
    for i in range(256):sheet[swizzle(i)*4:swizzle(i)*4+4]=palette[i*4:i*4+4]
    for y in range(512):
        for x in range(640):sheet[1024+offset(x,y,640)]=indices[y*640+x]if y<480 else 255
    chunks=[native.chunk(i)for i in range(6)];chunks[5]=bytes(sheet)
    compressed=[original[a:a+n]for _,a,n in native.chunks]
    compressed[5]=lzss_encode(chunks[5],256)
    # Minimize storage; decoded unrelated chunks stay byte-exact.
    for i in range(1,5):
        candidate=lzss_encode(chunks[i],256)
        if len(candidate)<len(compressed[i]):compressed[i]=candidate
    table=native.chunks[0][1]-6*8
    result=original[:table]+b''.join(struct.pack('<II',len(c),len(p))for c,p in zip(chunks,compressed))+b''.join(compressed)
    if len(result)>capacity:raise ValueError('Composed startup image exceeds assigned extent')
    result+=bytes(capacity-len(result));check=Container(result)
    if any(check.chunk(i)!=native.chunk(i)for i in range(5)) or check.sizes!=native.sizes or check.rects!=native.rects:raise ValueError('Unrelated startup data changed')
    return result

def from_iso(spec,read,overlay_data):
    inputs=[]
    for name in('original','background'):
        ref=spec[name];at,n=ref['offset'],ref['bytes']
        if type(at)is not int or at<0 or type(n)is not int or not 0<n<=4*1024*1024:raise ValueError('Invalid startup source bounds')
        data=read(at,n)
        if len(data)!=n or hashlib.sha256(data).hexdigest()!=ref['sha256']:raise ValueError('Startup ISO source hash mismatch')
        inputs.append(data)
    if hashlib.sha256(overlay_data).hexdigest()!=spec['overlay']:raise ValueError('Startup overlay blob hash mismatch')
    return compose(*inputs,overlay_data,spec['capacity'])
