/* Canonical song rows never enter a native byte array. Only this bounded,
 * cyclic view is handed to the original wheel/selection animation code. */
typedef struct { u16 key; u8 diff,pad; s32 order; } song_row_t;
static struct {
    song_row_t *rows;
    s32 capacity,n,header,view,origin,initial,initial_base,active,id,diff;
    u8 *ctx,*songs,*diffs,*wheel;
} window;

static void window_reset(void) { window.active=0;window.wheel=0; }

/* Reserve storage before reporting rows to a native caller. An allocation
 * failure yields an empty folder, never an advertised but unwritten tail.
 * Session saves are reserved here too, so alias eviction cannot fail mid-scroll. */
static s32 window_storage(void)
{
    s32 capacity=sdb.n_new*4+LIST_MAX;song_row_t *rows;
    if (!ensure_saves())return 0;
    if (window.capacity>=capacity)return 1;
    rows=malloc_(capacity*sizeof(song_row_t));
    if (!rows)return 0;
    if (window.rows)free_(window.rows);
    window.rows=rows;window.capacity=capacity;return 1;
}

static s32 wrap_row(s32 i,s32 n)
{
    i%=n;return i<0?i+n:i;
}

/* native rows: the song list record; added rows: their summary (records are off the block) */
static s32 row_level(s32 key,s32 dp,s32 diff)
{
    if(key>=SONG_KEY)return slot_summary(key-SONG_KEY)[SUM_LEVELS+(dp?5:0)+diff];
    return rec_of(song_ctx(),key)[0x4a+(dp?5:0)+diff];
}

static s32 row_bpm(s32 key)
{
    const u8 *b=key>=SONG_KEY?slot_summary(key-SONG_KEY)+SUM_BPM:rec_of(song_ctx(),key)+0x42;
    s32 lo=(short)(b[0]|b[1]<<8),hi=(short)(b[2]|b[3]<<8);
    return hi<0?lo:hi;
}

/* the title bytes compared when there are no sort ranks: an added row has only its first byte */
static const u8 *row_title(s32 key,u8 *one)
{
    if(key<SONG_KEY)return rec_of(song_ctx(),key);
    one[0]=slot_summary(key-SONG_KEY)[SUM_TITLE];one[1]=0;return one;
}

static u8 *row_save(s32 key,s32 mode,s32 diff)
{
    return key>=SONG_KEY?slot_save(key-SONG_KEY,mode,diff):save_rec(key,mode,diff);
}

static s32 alias_key(s32 id)
{
    s32 row=song_catalog_row(id);
    return row<0?-1:row<FIRST?row:SONG_KEY+row-FIRST;
}

static s32 title_compare(song_row_t *a,song_row_t *b)
{
    u8 xo[2],yo[2];const u8 *x=row_title(a->key,xo),*y=row_title(b->key,yo);s32 i;
    if(sdb.sort_version==1 && sdb.sort_rank){
        u32 ar=a->key>=SONG_KEY?FIRST+a->key-SONG_KEY:a->key;
        u32 br=b->key>=SONG_KEY?FIRST+b->key-SONG_KEY:b->key;
        if(ar<FIRST+sdb.n_new && br<FIRST+sdb.n_new){
            if(sdb.sort_rank[ar]!=sdb.sort_rank[br])return (s32)sdb.sort_rank[ar]-sdb.sort_rank[br];
            if(a->key!=b->key)return (s32)a->key-b->key;
            return (s32)a->diff-b->diff;
        }
    }
    for (i=0;i<64;i++) {
        if (x[i]!=y[i])return (s32)x[i]-y[i];
        if (!x[i])break;
        LOOP_PAD();
    }
    if (a->key!=b->key)return (s32)a->key-b->key;
    return (s32)a->diff-b->diff;
}

static s32 row_compare(song_row_t *a,song_row_t *b)
{
    if (a->order!=b->order)return a->order<b->order?-1:1;
    return title_compare(a,b);
}

static s32 row_order(u8 *ctx,song_row_t *r,s32 mode,s32 dp,s32 smode)
{
    u8 *sv;
    s32 native=r->key<99,score,valid,miss;
    switch(mode) {
    case 0:return row_level(r->key,dp,r->diff);
    case 2:sv=row_save(r->key,smode,r->diff);miss=(short)(*(u16 *)(sv+12)<<1);miss>>=1;return miss<0?65535:miss;
    case 3:sv=row_save(r->key,smode,r->diff);score=(I32(sv+12)&0x1fffffff)>>15;valid=sv[15]>>5;return valid?-score:1;
    case 4:return native?I32(ctx+0x20ef4+r->diff*0x18c+r->key*4):-1;
    case 7:return row_bpm(r->key);
    case 8:return row_save(r->key,smode,r->diff)[11]>>4;
    case 9:
    case 10:
#ifndef HOST_TEST
        if (native) {
            if (mode==9)return ((s32 (*)(u8 *,s32,s32))0x216d50)(ctx,r->key,r->diff);
            return ((s32 (*)(u8 *,s32,s32,s32))0x216cb0)(ctx,rival_idx()-2,r->key,r->diff);
        }
#endif
        return 0;
    default:return 0;
    }
}

static void row_sift(s32 root,s32 end)
{
    song_row_t v=window.rows[root];s32 child;
    while ((child=root*2+1)<end) {
        if (child+1<end && row_compare(&window.rows[child],&window.rows[child+1])<0)child++;
        if (row_compare(&v,&window.rows[child])>=0)break;
        window.rows[root]=window.rows[child];root=child;LOOP_PAD();
    }
    window.rows[root]=v;
}

static void rows_sort(void)
{
    s32 i; song_row_t v;
    for (i=window.n/2;i>0;i--) {row_sift(i-1,window.n);LOOP_PAD();}
    for (i=window.n-1;i>0;i--) {
        v=window.rows[0];window.rows[0]=window.rows[i];window.rows[i]=v;
        row_sift(0,i);LOOP_PAD();
    }
}

static void pin_alias(s32 id,u32 stamp)
{
    s32 a=id&255;
    if (a!=PROBE && a!=VARIANT && song_catalog_row(id)>=FIRST)sdb.stamp[a]=stamp;
}

static s32 bind_keys(u16 *keys,s32 n)
{
#ifdef SELECT_PROFILE
    unsigned profile_start=sp_span();
#endif
    s32 i,a,best,k,j;u32 now=++sdb.clock;
    for (i=0;i<n;i++) {
        if (keys[i]!=NOSLOT && keys[i]>=SONG_KEY && (a=sdb.alias_of[keys[i]-SONG_KEY]))sdb.stamp[a]=now;
        LOOP_PAD();
    }
    if (MS_STATE) {
        pin_alias(I32(MS_STATE+0x24),now);pin_alias(I32(MS_STATE+0x28),now);
    }
    for (i=0;i<n;i++) {
        if (keys[i]==NOSLOT || keys[i]<SONG_KEY || sdb.alias_of[keys[i]-SONG_KEY])continue;
        best=-1;
        for (a=FIRST;a<255;a++) {
            if (a==VARIANT)a=128;
            if (sdb.stamp[a]!=now && (best<0 || sdb.stamp[a]<sdb.stamp[best]))best=a;
            LOOP_PAD();
        }
        if (best<0 || !stash_alias(best))return 0;
        k=keys[i]-SONG_KEY;
        if (sdb.slot_of[best]<sdb.n_new)sdb.alias_of[sdb.slot_of[best]]=0;
        sdb.slot_of[best]=k;sdb.alias_of[k]=best;sdb.stamp[best]=now;
        memcpy_(save_of(best,0,0),slot_saved+k*0x230,0x230);
        LOOP_PAD();
    }
#ifdef SELECT_PROFILE
    if(profile_start)sp_note(6,n,sp_span()-profile_start,sdb.clock,0,0,0,0,0,0);
#endif
    return 1;
}

static s32 key_alias(s32 key)
{
    return key<SONG_KEY?key:sdb.alias_of[key-SONG_KEY];
}

s32 fold_total(u8 *ctx,s32 id,s32 diff)
{
    pool_t *p=pool_of(ctx,0);s32 vf=vf_of(id),n;
    if (vf<0 || !p || !I32(ctx+0x2072c))return g_count(ctx,id,diff);
    n=p->cnt[vf][one_list(vf)?0:dix(diff)];
    if (vf>=VF_LEVEL)n+=g_count(ctx,id,diff);
    return n;
}

void fold_copy(u8 *ctx,s32 id,s32 diff,u8 *songs,u8 *diffs)
{
#ifdef SELECT_PROFILE
    unsigned profile_start=sp_span(),fill_ticks=0,sort_ticks=0,bind_ticks=0,stage;
#endif
    pool_t *p=pool_of(ctx,0);u8 *l; s32 vf=vf_of(id),d=dix(diff),i,k,e,n=0,
        dp=I32(ctx+0x20728),smode=I32(ctx+0x20724),mode=I32(ctx+0x20734),
        keep=MS_STATE?alias_key(I32(MS_STATE+0x24)):-1;
    /* Retail A-Z folders ignore LEVEL/BPM/save sorting. */
    if(vf>=VF_ALPHA && vf<VF_NOPLAY)mode=1;
    window_reset();
    if (vf<0 || !p || !I32(ctx+0x2072c)) {
        g_open(ctx,id,diff,songs,diffs);
#ifdef SELECT_PROFILE
        if(profile_start)sp_note(10,3,sp_span()-profile_start,id,d,0,0,0,0,0);
#endif
        return;
    }
    if (!window_storage()) {I32(ctx+0x1fb84)=0;return;}
    l=game_list(ctx,vf,d);
    if (!l)l=ctx;
    for (i=0;i<I32(l+4);i++) {
        s32 s=l[0x1f8+i],ed=l[9+i];
        if (s<FIRST && (vf>=VF_LEVEL || (sdb.emp_fold[s]==vf && ed==d))) {
            window.rows[n].key=s;window.rows[n].diff=ed;n++;
        }
        LOOP_PAD();
    }
    for (k=0;k<(s32)sdb.n_new;k++) {
        for (e=one_list(vf)?0:d;e<(one_list(vf)?5:d+1);e++) {
            if (in_vf(p,k,e,vf,d,dp,smode)) {
                window.rows[n].key=SONG_KEY+k;window.rows[n].diff=e;n++;
            }
            LOOP_PAD();
        }
        LOOP_PAD();
    }
    window.n=n;window.ctx=ctx;window.id=id;window.diff=d;
#ifdef SELECT_PROFILE
    if(profile_start)fill_ticks=sp_span()-profile_start;stage=sp_span();
#endif
    for (i=0;i<n;i++) {window.rows[i].order=row_order(ctx,window.rows+i,mode,dp,smode);LOOP_PAD();}
    rows_sort();
#ifdef SELECT_PROFILE
    if(stage)sort_ticks=sp_span()-stage;
#endif
    window.initial_base=0;window.view=n>SONG_VIEW?SONG_VIEW:n;
    if (n>SONG_VIEW && keep>=0) {
        for (i=0;i<n;i++) {
            if (window.rows[i].key==keep) {
                k=i-SONG_VIEW/2;if (k<0)k=0;if (k>n-SONG_VIEW)k=n-SONG_VIEW;
                window.initial_base=k;break;
            }
            LOOP_PAD();
        }
    }
    {
        u16 keys[SONG_VIEW];
        for (i=0;i<window.view;i++) {keys[i]=window.rows[window.initial_base+i].key;LOOP_PAD();}
#ifdef SELECT_PROFILE
        stage=sp_span();
#endif
        if (!bind_keys(keys,window.view))return;
#ifdef SELECT_PROFILE
        if(stage)bind_ticks=sp_span()-stage;
#endif
        for (i=0;i<window.view;i++) {
            songs[i]=key_alias(keys[i]);diffs[i]=window.rows[window.initial_base+i].diff;LOOP_PAD();
        }
    }
    /* Native callers own the one/two special rows preceding these outputs. */
    window.header=d==3?0:n>1?2:1;window.songs=songs-window.header;
    window.diffs=diffs-window.header;window.view+=window.header;
    window.initial=1;window.active=1;
    I32(ctx+0x1fb84)=window.view-window.header;
    memcpy_(ctx+0x1fb80+0x1f8,songs,window.view-window.header);
    memcpy_(ctx+0x1fb80+9,diffs,window.view-window.header);
#ifdef SELECT_PROFILE
    if(profile_start)sp_note(7,id,d,window.n,mode,sp_span()-profile_start,window.view,fill_ticks,sort_ticks,bind_ticks);
#endif
}

static s32 view_global(s32 physical)
{
    if (window.initial)return physical<window.header?physical:physical+window.initial_base;
    return wrap_row(window.origin+physical,window.n+window.header);
}

/* The native scene updates selection and input before drawing. Menu/back
 * handlers deliberately keep scene cursor0 for the heading, while the cyclic
 * wheel is centred at48. Translate on entry to song-list update as well as at
 * draw time, before UP can wrap the wrong cursor over the small native view. */
#ifndef FOLDER_WHEEL
#ifdef HOST_TEST
#define FOLDER_WHEEL ((u8 *)0)
#else
#define FOLDER_WHEEL (*(u8 **)0x2ad330)
#endif
#endif
static void song_folder_anchor(void)
{
    u8 *parent=FOLDER_WHEEL;
    if(!MS_STATE || I32(MS_STATE+0x3c)!=8 || !parent || I32(parent+0x60)!=5)return;
    /* Retail parent modes4/6 suspend scroll interpolation while the child is
     * open. Closing mode5 can then draw its retained +70 Y, but the first
     * normal mode1 draw snaps to217 because +144 has already expired. Settle
     * that invisible parent before it returns; keep identity/order/close
     * bounds/counters and the child wheel's geometry untouched. */
    I32(parent+0x70)=I32(parent+0x140)=217;
    I32(parent+0x144)=8;I32(parent+0x154)=0;
}
void song_window_update(u8 *scene)
{
#ifdef LOAD_PROFILE
    lp_phase(LP_SELECT,6);
#endif
    song_folder_anchor();
    u8 *wheel=MS_WHEEL;s32 physical;
    if (!window.active || window.initial || window.n<=SONG_VIEW || !MS_STATE || !wheel)return;
    if (I32(MS_STATE+0x3c)!=3 || (u8 *)I32(scene+0xd4)!=window.ctx)return;
    if (window.wheel!=wheel || I32(wheel+0x5f4)!=window.view)return;
    physical=I32(wheel+0x5f0);
    if ((u32)physical<(u32)window.view)I32(MS_STATE+8)=physical;
}

/* The stock MUSIC NUMBER callback has three digit elements. For a larger
 * interpolated value, draw the complete number once from its units element,
 * right-aligned in the same three-cell area. Reuse the native17px glyphs and
 * parent opacity/Y scale; smaller values stay on the original renderer. */
#ifndef COUNT_GLYPHS
#define COUNT_GLYPHS ((u32 *)0x25dae0)
#define COUNT_DRAW ((void (*)(u32,s32,u8 *))0x12f9b0)
#endif
void song_count_wide(u8 *params,s32 value)
{
    u8 copy[36];s32 n=value,digits=0,i,zoom=I32(params+0x10),right;
    if (I32(params)!=0x1c)return;
    do {digits++;n/=10;LOOP_PAD();} while(n);
    memcpy_(copy,params,sizeof copy);
    right=I32(params+8)+(17-I32(params+0x18))*zoom/100;
    zoom=zoom*3/digits;I32(copy+0x10)=zoom;I32(copy+0x18)=0;
    for (i=0;i<digits;i++) {
        I32(copy+8)=right-17*zoom*(i+1)/100;
        COUNT_DRAW(COUNT_GLYPHS[value%10],2,copy);value/=10;LOOP_PAD();
    }
}

/* Called immediately before the native wheel draws. Keep its interpolation,
 * positions and per-key input logic, rebase only the bounded row arrays. */
void song_window_tick(u8 *wheel)
{
    s32 i,j,n,focus,physical,delta,changed,menu;u16 keys[SONG_VIEW+2];
    s32 xpos[SONG_VIEW+2];u8 lamps[SONG_VIEW+2];
    if (!window.active || !MS_STATE || wheel!=MS_WHEEL || I32(wheel+0x5f4)!=window.view)return;
    n=window.view;physical=I32(wheel+0x5f0);
    if (physical<0 || physical>=n)return;
    if (window.wheel && window.wheel!=wheel) {window_reset();return;}
    window.wheel=wheel;
    if (window.n<=SONG_VIEW)return;
    menu=I32(MS_STATE+0x3c)!=3;
    /* Several native menu/back paths explicitly use rows0/1 as shortcuts to
     * the folder heading/RANDOM. Keep those scene slots valid offscreen. */
    focus=(!window.initial && physical<window.header)?physical:view_global(physical);
    if (menu && I32(MS_STATE+8)==0)focus=0;
    delta=physical-n/2;
    changed=window.initial || delta!=0 || focus!=view_global(physical);
    if (!window.initial && window.header && window.diffs[0]!=window.diff) {
        window.diff=window.diffs[0];changed=1;
    }
    if (!changed) {if (!menu)I32(MS_STATE+8)=physical;return;}
    for (i=0;i<n;i++) {
        j=wrap_row(focus+i-n/2,window.n+window.header);
        keys[i]=j<window.header?NOSLOT:window.rows[j-window.header].key;
        xpos[i]=I32(wheel+0x634+wrap_row(i+delta,n)*4);
        lamps[i]=wheel[0x3e2+wrap_row(i+delta,n)];LOOP_PAD();
    }
    if (!bind_keys(keys,n))return;
    for (i=0;i<n;i++) {
        j=wrap_row(focus+i-n/2,window.n+window.header);
        window.songs[i]=j<window.header?(j==0?125:124):key_alias(keys[i]);
        window.diffs[i]=j<window.header?window.diff:window.rows[j-window.header].diff;
        wheel[i]=window.songs[i];wheel[0x1f1+i]=window.diffs[i];wheel[0x3e2+i]=lamps[i];
        I32(wheel+0x634+i*4)=xpos[i];LOOP_PAD();
    }
    window.origin=wrap_row(focus-n/2,window.n+window.header);window.initial=0;
    if (window.header) {window.songs[0]=125;window.diffs[0]=window.diff;}
    if (window.header>1) {window.songs[1]=124;window.diffs[1]=window.diff;}
    if (!menu)I32(MS_STATE+8)=n/2;
    I32(wheel+0x5f0)=n/2;
    /* Scrollbar follows the complete canonical folder, not the small view. */
    if (!menu)wheel_scroll(focus,window.n+window.header);
}

/* RANDOM's two native call sites add two to this result and read a row. Use
 * one extra, non-visible array slot so RANDOM samples the full folder without
 * moving the RANDOM wheel row or disturbing its selection/animation. */
s32 song_window_random(s32 range)
{
    s32 k,j;u16 keys[SONG_VIEW+3];
    if (!window.active || window.n<=SONG_VIEW || !MS_STATE || range!=window.view-2)
        return wheel_random(range);
    for (j=0;j<window.view;j++) {k=alias_key((s8)(window.wheel?window.wheel[j]:window.songs[j]));keys[j]=k<0?NOSLOT:k;LOOP_PAD();}
    k=wheel_random(window.n);keys[window.view]=window.rows[k].key;
    if (!bind_keys(keys,window.view+1))return 0;
    window.songs[window.view]=key_alias(keys[window.view]);
    window.diffs[window.view]=window.rows[k].diff;
    return window.view-2;
}
