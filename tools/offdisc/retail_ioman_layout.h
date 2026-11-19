/* EMPRESS IOPRP310 IO/File_Manager 2.3. Disassembly proves a 16-node device
 * list and 32 file objects, unlike the older array assumed by PS2SDK's bridge.
 * Used only when taking ownership at IOMANX startup, with interrupts disabled.
 * Requires the SDK ModuleInfo_t/iomanX types and memcpy/memset/memcmp. */
#ifndef MEGAMIX_RETAIL_IOMAN_LAYOUT_H
#define MEGAMIX_RETAIL_IOMAN_LAYOUT_H
typedef struct { unsigned next,device; } mmix_ioman_node;
typedef struct {
    iomanX_iop_device_t *devices[16];
    iomanX_iop_file_t files[32];
    unsigned count;
} mmix_ioman_snapshot;
static unsigned mmix_phys(unsigned p){return p&0x1fffffffu;}
static int mmix_iop_ptr(unsigned p,unsigned bytes)
{
    unsigned segment=p&0xe0000000u,a=mmix_phys(p);
    return (segment==0 || segment==0x80000000u || segment==0xa0000000u) &&
           a>=0x1000 && a<0x200000 && bytes<=0x200000-a;
}
static int mmix_ioman_capture(const ModuleInfo_t *info,mmix_ioman_snapshot *out)
{
    unsigned base=info->text_start,seen=0;
    if(info->version!=0x0203 || info->text_size!=0x1e60 || info->data_size!=0x1f0 || info->bss_size!=0x290 ||
       !mmix_iop_ptr(base,0x22e0) || (base&15) || sizeof(iomanX_iop_file_t)!=16)return -1;
    if(memcmp((void *)(base+0x1f2c),"ioman   3100",12) ||
       *(unsigned *)(base+0x18e8)!=0x27bdffd8 || *(unsigned *)(base+0x1a10)!=0x27bdffd8 ||
       *(unsigned *)(base+0x2050))return -1;
    memset(out,0,sizeof *out);
    unsigned node=*(unsigned *)(base+0x205c),pool=mmix_phys(base)+0x2260;
    while(node){
        unsigned a=mmix_phys(node);
        if(!mmix_iop_ptr(node,8) || a<pool || a>=pool+16*8 || ((a-pool)&7))return -1;
        unsigned bit=1u<<((a-pool)/8);
        if(seen&bit)return -1;
        seen|=bit;
        mmix_ioman_node *n=(void *)node;
        unsigned d=n->device;
        if(!mmix_iop_ptr(d,sizeof(iomanX_iop_device_t)) || (d&3))return -1;
        iomanX_iop_device_t *device=(void *)d;
        if(!mmix_iop_ptr((unsigned)device->name,1) || !mmix_iop_ptr((unsigned)device->ops,8))return -1;
        for(unsigned i=0;i<out->count;i++)if(mmix_phys((unsigned)out->devices[i])==mmix_phys(d))return -1;
        out->devices[out->count++]=device;node=n->next;
    }
    memcpy(out->files,(void *)(base+0x2060),sizeof out->files);
    for(unsigned f=0;f<32;f++)if(out->files[f].mode){
        unsigned i;for(i=0;i<out->count;i++)if(mmix_phys((unsigned)out->files[f].device)==mmix_phys((unsigned)out->devices[i]))break;
        if(i==out->count)return -1;
    }
    return 0;
}
static void mmix_ioman_takeover(const ModuleInfo_t *info,const mmix_ioman_snapshot *snapshot,
                               iomanX_iop_device_t **devices,iomanX_iop_file_t *files)
{
    unsigned base=info->text_start;
    memcpy(devices,snapshot->devices,sizeof snapshot->devices);
    memcpy(files,snapshot->files,sizeof snapshot->files);
    /* Ownership moved, without calling any device init/deinit callback. Leave
     * the original manager as a consistent empty list for reboot cleanup. */
    memset((void *)(base+0x2060),0,sizeof snapshot->files);
    mmix_ioman_node *pool=(void *)(base+0x2260);
    for(unsigned i=0;i<16;i++){pool[i].next=i<15?(unsigned)&pool[i+1]:0;pool[i].device=0;}
    *(unsigned *)(base+0x2054)=(unsigned)pool;
    *(unsigned *)(base+0x205c)=0;
}
#endif
