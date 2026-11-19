"""Source-only launcher font atlas generation using the portable coverage profile."""
import base64,json,math,zlib
from text_raster import Mask


def launcher_font(atlas):
    sheet=Mask(512,256);glyphs=[];x=y=0
    for code in range(32,127):
        ch=chr(code);g,_=atlas.glyph('launcher',ch)
        im=atlas.coverage(ch,'launcher',60,24,128,pad=4)
        box=im.bbox();dx=0
        if box:
            # Use the actual ink box, retaining a pixel border and native32-row cell.
            l,t,r,b=box;ink=im.crop(box)
            w=max(1,round(ink.w*.81/4));h=max(1,round(ink.h/4))
            glyph=Mask(w+2,32);glyph.paste(ink.resize(w,h),1,round(t/4))
            dx=round((l/4-1)*.81)-1
        else:glyph=Mask(1,32)
        if x+glyph.w>512:x=0;y+=32
        if y+32>256:raise ValueError('Launcher glyph atlas overflow')
        sheet.paste(glyph,x,y)
        advance=max(1,round(g['advance']/64*15/192*.81))
        glyphs.append([x,y,glyph.w,dx,advance]);x+=glyph.w
    height=y+32
    pixels=bytes(min(255,round(v*1.15))for v in sheet.p[:512*height])
    return dict(format=1,width=512,height=height,glyphs=glyphs,profile=atlas.info['profile'],alpha_zlib_base64=base64.b64encode(zlib.compress(pixels,9)).decode('ascii'))


def font_header(info):
    alpha=zlib.decompress(base64.b64decode(info['alpha_zlib_base64']))
    words=[0xffffff|(((a*128+127)//255)<<24)for a in alpha]
    out=f'/* Generated open-font atlas; native logo is filled from the user ISO. */\n#define FONT_W {info["width"]}\n#define FONT_H {info["height"]}\n#define FONT_CELL 32\n'
    out+=f'static const u32 font_px[{len(words)}] __attribute__((aligned(128))) = {{\n'
    out+='\n'.join('    '+','.join(f'0x{v:08x}'for v in words[i:i+8])+','for i in range(0,len(words),8))+'\n};\n'
    out+='static const short glyph[95][5] = {\n'+'\n'.join('    {'+','.join(map(str,g))+'},'for g in info['glyphs'])+'\n};\n'
    return out
