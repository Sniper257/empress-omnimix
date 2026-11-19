#ifndef MEGAMIX_ASSET_HEAP_H
#define MEGAMIX_ASSET_HEAP_H
/* Bounded, passive native free-list snapshot. No allocation, locking or RPC.
 * Not a stop-the-world heap dump: a concurrent owner may change the list. */
#ifndef ASSET_HEAP_WORD
#define ASSET_HEAP_WORD(a) (*(volatile u32 *)(unsigned long)(a))
#endif
static int asset_heap_pointer(u32 p,u32 n){return n<=0x2000000u&&!(p&3)&&p>=0x944700&&p<=0x2000000u-n;}
static u32 asset_heap_walk(u32 *free_bytes,u32 *largest_bytes,u32 (*gaps)[2],u32 capacity,u32 *gap_count)
{
    u32 obj=ASSET_HEAP_WORD(0x2ad000),head=0,total=0,largest=0,count=0,status=0,used=0,prev=0;
    if(obj<0x100000 || obj>0x2000000-12 || (obj&3))status=1;
    else {head=ASSET_HEAP_WORD(obj+8);if(!asset_heap_pointer(head,0x70))status=2;}
    if(!status){
        u32 a=head+0x30;
        while(a){
            if(!asset_heap_pointer(a,24)||++count>4096){status=3;break;}
            u32 base=ASSET_HEAP_WORD(a),size=ASSET_HEAP_WORD(a+4),next=ASSET_HEAP_WORD(a+8);
            if(ASSET_HEAP_WORD(a+12)!=prev){status=6;break;}
            if(ASSET_HEAP_WORD(a+16)!=1){status=7;break;}
            if(size){
                if(!asset_heap_pointer(base,size)||size>0x2000000u-total){status=4;break;}
                if(gaps && used<capacity){gaps[used][0]=base;gaps[used][1]=size;}
                used++;total+=size;if(size>largest)largest=size;
            }
            if(next==a){status=5;break;}prev=a;a=next;LOOP_PAD();
        }
    }
    if(gap_count)*gap_count=used;
    *free_bytes=status?~0u:total;*largest_bytes=status?~0u:largest;return status;
}
static u32 asset_heap_sample(u32 *total,u32 *largest)
{return asset_heap_walk(total,largest,0,0,0);}
#endif
