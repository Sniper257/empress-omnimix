#ifndef MEGAMIX_BUILD_CACHE_H
#define MEGAMIX_BUILD_CACHE_H
/* Optional, installation-sealed prepared image. Never stores scores. Importers
 * revoke launcher/source.id BEFORE editing inputs; only publication seals it.
 * Verbose launcher diagnostics build live. Quiet game diagnostics use a distinct
 * prepared image keyed by the selected runtime. Validate before touching EE. */
#define BC_LO 0x100000u
#define BC_BYTES (RES_HI - BC_LO)
#define BC_PAGE 4096u
#define BC_PAGES ((BC_BYTES + BC_PAGE - 1) / BC_PAGE)
#define BC_MAX (BC_BYTES + BC_PAGES * 4 + 65536u)
static unsigned bc_hit, bc_saved;
static unsigned sc_hit, sc_saved;
static void bc_hash(u32 *a, u32 *b, const void *p, unsigned n)
{
    const u8 *s = p;
    while (n--) {
        *a = (*a ^ *s) * 16777619u;
        *b = (*b * 33u) ^ *s++;
    }
}
/* MMCE can signal an EOF probe as an error (or mask that error as zero).
 * Read only checked remaining bytes; no feof/fgetc/read-until-zero contract. */
static int bc_length(FILE *f,unsigned limit,unsigned *bytes)
{
    long n;if(fseek(f,0,SEEK_END)||(n=ftell(f))<0||(unsigned long)n>limit||fseek(f,0,SEEK_SET))return 0;
    *bytes=(unsigned)n;return 1;
}
static int bc_read(FILE *f,void *out,unsigned n)
{
    u8 *p=out;unsigned at=0;
    while(at<n){size_t got=fread(p+at,1,n-at,f);if(!got||got>n-at||ferror(f))return 0;at+=got;}
    return 1;
}
static int bc_file(const char *rel,u32 *a,u32 *b,unsigned *bytes)
{
    char path[256];u8 block[32768];unsigned total=0,at=0;FILE *f;
    if(snprintf(path,sizeof path,"%s%s",DEV,rel)>=(int)sizeof path||!(f=fopen(path,"rb")))return 0;
    int ok=bc_length(f,0x7fffffffu,&total);
    while(ok&&at<total){unsigned n=total-at;if(n>sizeof block)n=sizeof block;
        if(!bc_read(f,block,n)){ok=0;break;}bc_hash(a,b,block,n);at+=n;}
    if(fclose(f))ok=0;if(bytes)*bytes=total;return ok;
}
static int bc_token(char token[65])
{
    FILE *f;
    unsigned n;
#ifdef MMCE_CONTROL
    return 0; /* Matched five-song control keeps its separate discovery contract. */
#endif
    if (launcher_debug)
        return 0;
    if (!(f = fopen(game_path("launcher/source.id"), "rb")))
        return 0;
    int ok=bc_length(f,64,&n)&&n==64&&bc_read(f,token,64);
    if(fclose(f))ok=0;
    if (!ok)
        return 0;
    for (unsigned i = 0; i < 64; i++)
        if (!((token[i] >= '0' && token[i] <= '9') || (token[i] >= 'a' && token[i] <= 'f')))
            return 0;
    return 1;
}
static int bc_key(u32 key[2])
{
    char path[200], token[65];
    key[0] = 2166136261u;
    key[1] = 5381u;
    if (!bc_token(token))
        return 0;
    {char identity[160];snprintf(identity,sizeof identity,"MEGAMIX prepared build R167/wheel1/scalars1/alphabet1/coalesce1/listingfamily1 %s",DEV);bc_hash(key,key+1,identity,strlen(identity)+1);}
    if(debug_enabled)bc_hash(key,key+1,"diagnostic/1",sizeof("diagnostic/1"));
#ifdef USB_HID
    bc_hash(key,key+1,"USB-HID/2",sizeof("USB-HID/2"));
#endif
#ifdef USB_NATIVE_PRELOAD
    /* A normal launcher must not warm-restore an image whose native module
     * loads require the diagnostic pre-Exec step. Cold parsing also checks
     * the pair, and the preloader validates the actual restored instructions. */
    bc_hash(key,key+1,"USB-NATIVE-PRELOAD/1",sizeof("USB-NATIVE-PRELOAD/1"));
#endif
    bc_hash(key, key + 1, token, 64);
    /* Scalars are rebound after restore. Only choices that change generated
     * resources or executable patches belong in the image identity. */
    int opts[] = {video, note_skin, frame_pack,
                  save_batch, input_mode, n_mods,   n_styles,  n_songs};
    bc_hash(key, key + 1, opts, sizeof opts);
    for (int i = 0; i < n_mods; i++) {
        bc_hash(key, key + 1, mods[i].dir, strlen(mods[i].dir) + 1);
        bc_hash(key, key + 1, &mods[i].on, sizeof mods[i].on);
    }
    for (int i = 0; i < n_styles; i++)
        bc_hash(key, key + 1, styles + i, sizeof styles[i]);
    for (int i = 0; i < n_songs; i++) {
        song_t *s = songs + i;
        bc_hash(key, key + 1, s, sizeof *s);
        snprintf(path, sizeof path, "songs/%.47s/%.63s/song.txt", styles[s->style].dir, s->dir);
        lm_entry *e = lm_find(path, 0);
        if (!e || !e->text)
            return 0;
        bc_hash(key, key + 1, e->text, e->size);
    }
    /* Check executable/runtime/catalog bytes even if a manual update omitted the
     * source seal. Other source edits must revoke/reseal like an import. */
    const char *files[] = {"EMPRESS/SLPM_552.21", runtime_bin(),
                           runtime_txt(),
                           "catalog/charts.txt", "catalog/sort.txt"};
    for (unsigned i = 0; i < sizeof files / sizeof *files; i++)
        if (!bc_file(files[i], key, key + 1, 0))
            return 0;
    return 1;
}
static int bc_span(u32 p, unsigned n) { return p >= RES_LO && p <= RES_HI && n <= RES_HI - p; }
static int bc_load(const u32 key[2], u32 *entry)
{
    char rel[96], path[256];
    FILE *f;
    u32 h[16], a = 2166136261u, b = 5381u;
    u8 *blob = 0, *p, *end;
    int hit = 0;unsigned file_bytes;
    snprintf(rel, sizeof rel, "cache/boot_%08x%08x.bin", (unsigned)key[0], (unsigned)key[1]);
    snprintf(path, sizeof path, "%s%s",DEV, rel);
    if (!(f = fopen(path, "rb")))
        return 0;
    if (!bc_length(f,BC_MAX+sizeof h,&file_bytes)||file_bytes<sizeof h||!bc_read(f,h,sizeof h)||h[0] != 0x3743424du || h[1] != 1 ||
        h[2] != key[0] || h[3] != key[1] || h[4] < BC_LO || h[4] >= RES_LO || (h[4] & 3) ||
        !bc_span(h[5], sizeof(sdb_t)) || !bc_span(h[6], 52) || !bc_span(h[7], 24) ||
        h[8] > BC_PAGES || h[9] > 256 || h[10] > BC_MAX || h[11] != BC_BYTES || h[12] || h[13])
        goto done;
    if (!(blob = malloc(h[10] ? h[10] : 1)))
        goto done;
    if(file_bytes!=sizeof h+h[10]||!bc_read(f,blob,h[10]))goto done;
    {int closed=fclose(f);f=0;if(closed)goto done;}
    bc_hash(&a, &b, h, 14 * 4);
    bc_hash(&a, &b, blob, h[10]);
    if (a != h[14] || b != h[15])
        goto done;
    p = blob;
    end = blob + h[10];
    for (unsigned i = 0; i < h[9]; i++) {
        u32 d[4], da = 2166136261u, db = 5381u, bytes;
        if (end - p < 16)
            goto done;
        memcpy(d, p, 16);
        p += 16;
        if (!d[3] || d[3] > 199 || (unsigned)(end - p) < d[3] || p[d[3] - 1] ||
            memchr(p, 0, d[3] - 1) || d[3] < 7 || memcmp(p, "cache/", 6) || strstr((char *)p, ".."))
            goto done;
        if (!bc_file((char *)p, &da, &db, &bytes) || bytes != d[0] || da != d[1] || db != d[2])
            goto done;
        p += d[3];
    }
    u8 *pages = p;
    u32 last = 0;
    for (unsigned i = 0; i < h[8]; i++) {
        u32 k;
        if (end - p < 4)
            goto done;
        memcpy(&k, p, 4);
        p += 4;
        if (k >= BC_PAGES || (i && k <= last))
            goto done;
        last = k;
        unsigned n = BC_BYTES - k * BC_PAGE;
        if (n > BC_PAGE)
            n = BC_PAGE;
        if ((unsigned)(end - p) < n)
            goto done;
        p += n;
    }
    if (p != end)
        goto done;
    /* No mutation above this line. A damaged/incomplete cache is just a miss. */
    memset((void *)BC_LO, 0, BC_BYTES);
    p = pages;
    for (unsigned i = 0; i < h[8]; i++) {
        u32 k;
        memcpy(&k, p, 4);
        p += 4;
        unsigned n = BC_BYTES - k * BC_PAGE;
        if (n > BC_PAGE)
            n = BC_PAGE;
        memcpy((void *)(BC_LO + k * BC_PAGE), p, n);
        p += n;
    }
    *entry = h[4];
    sdb = (sdb_t *)h[5];
    asset_mem = (u32 *)h[6];
    bc_hit = hit = 1;
    logf_("Prepared build cache hit: %u pages, %u checked products\n", h[8], h[9]);
done:
    free(blob);
    if(f)fclose(f);
    return hit;
}
static void bc_store(const u32 key[2], u32 entry)
{
    u32 h[16] = {0x3743424du, 1,        key[0], key[1], entry, (u32)sdb, (u32)asset_mem, 0, 0, 0,
                 0,           BC_BYTES, 0,      0,      0,     0};
    u8 *blob = malloc(BC_MAX), *p = blob, *end;
    u32 *vd = (u32 *)RES_LO;
    char rel[96], path[256], temp[260];
    FILE *f = 0;
    int ok = 0;
    if (!blob)
        return;
    end = blob + BC_MAX;
    while ((u32)vd + 24 <= RES_HI && *vd != 0x4b534456u)
        vd++;
    if ((u32)vd + 24 > RES_HI)
        goto done;
    h[7] = (u32)vd;
    for (unsigned side = 0; side < 2; side++) {
        if (vd[1 + side] > MAXEXT || !bc_span(vd[3 + side], vd[1 + side] * sizeof(ext_t)))
            goto done;
        ext_t *e = (ext_t *)vd[3 + side];
        for (unsigned i = 0; i < vd[1 + side]; i++) {
            if (e[i].path > RES_HI - RES_LO)
                goto done;
            const char *src = (const char *)(RES_LO + e[i].path);
            if (strncmp(src,game_path("cache/"),strlen(DEV)+6))
                continue;
            const char *name = src + strlen(DEV);
            unsigned n = strlen(name) + 1;
            u32 a = 2166136261u, b = 5381u, bytes;
            if (n > 200 || end - p < 16 + n || h[9] >= 256 || !bc_file(name, &a, &b, &bytes) ||
                bytes != e[i].size)
                goto done;
            u32 d[] = {bytes, a, b, n};
            memcpy(p, d, 16);
            p += 16;
            memcpy(p, name, n);
            p += n;
            h[9]++;
        }
    }
    for (u32 k = 0; k < BC_PAGES; k++) {
        unsigned n = BC_BYTES - k * BC_PAGE;
        if (n > BC_PAGE)
            n = BC_PAGE;
        const u8 *src = (const u8 *)(BC_LO + k * BC_PAGE);
        unsigned i;
        for (i = 0; i < n && !src[i]; i++)
            ;
        if (i == n)
            continue;
        if ((unsigned)(end - p) < n + 4)
            goto done;
        memcpy(p, &k, 4);
        p += 4;
        memcpy(p, src, n);
        p += n;
        h[8]++;
    }
    h[10] = p - blob;
    h[14] = 2166136261u;
    h[15] = 5381u;
    bc_hash(h + 14, h + 15, h, 14 * 4);
    bc_hash(h + 14, h + 15, blob, h[10]);
    snprintf(rel, sizeof rel, "cache/boot_%08x%08x.bin", (unsigned)key[0], (unsigned)key[1]);
    snprintf(path, sizeof path, "%s%s",DEV, rel);
    snprintf(temp, sizeof temp, "%s.tmp", path);
    if (!(f = fopen(temp, "wb")))
        goto done;
    ok = fwrite(h, 1, sizeof h, f) == sizeof h && fwrite(blob, 1, h[10], f) == h[10];
    if (fclose(f))
        ok = 0;
    f = 0;
    if (ok && !rename(temp, path)) {
        bc_saved = 1;
        logf_("Prepared build cache saved: %u bytes\n", h[10] + (unsigned)sizeof h);
    }
done:
    if (f)
        fclose(f);
    free(blob);
}

/* The menu cache contains only launcher structs and manifest text. A source
 * publication seal covers in-place asset edits; live styles/style/index files
 * still contribute to its key, including changes made outside import scripts. */
static int sc_key(u32 key[2])
{
    char token[65], *text, *p, *l, path[200];
    key[0] = 2166136261u;
    key[1] = 5381u;
    if (!bc_token(token))
        return 0;
    {char identity[160];snprintf(identity,sizeof identity,"R27 menu/1 %s",DEV);bc_hash(key,key+1,identity,strlen(identity)+1);}
#ifdef USB_HID
    bc_hash(key,key+1,"USB-HID/2",sizeof("USB-HID/2"));
#endif
    bc_hash(key, key + 1, token, 64);
    unsigned layout[] = {sizeof(style_t), sizeof(song_t), MAXSTYLE, MAXSONG};
    bc_hash(key, key + 1, layout, sizeof layout);
    if (!(text = slurp("songs/styles.txt", 0)))
        return 0;
    bc_hash(key, key + 1, text, strlen(text));
    p = text;
    while ((l = line(&p))) {
        if (!*l || *l == '#')
            continue;
        const char *names[] = {"style.txt", "index.txt"};
        for (unsigned i = 0; i < 2; i++) {
            snprintf(path, sizeof path, "songs/%s/%s", l, names[i]);
            bc_hash(key, key + 1, path, strlen(path) + 1);
            u32 have = bc_file(path, key, key + 1, 0);
            bc_hash(key, key + 1, &have, 4);
        }
    }
    free(text);
    return 1;
}
static int sc_load(void)
{
    u32 key[2], h[10], a = 2166136261u, b = 5381u;
    u8 *blob = 0, *p, *end;
    char rel[128];
    FILE *f = 0;
    int hit = 0;unsigned file_bytes;
    if (!sc_key(key))
        return 0;
    snprintf(rel, sizeof rel, "%scache/menu_%08x%08x.bin",DEV, (unsigned)key[0], (unsigned)key[1]);
    if (!(f = fopen(rel, "rb")))
        return 0;
    if(!bc_length(f,LM_LIMIT+sizeof h,&file_bytes)||file_bytes<sizeof h||!bc_read(f,h,sizeof h)||h[0] != 0x374d434du || h[1] != 1 ||
        h[2] != key[0] || h[3] != key[1] || h[4] > MAXSTYLE || h[5] > MAXSONG || h[6] > LM_LIMIT ||
        h[7] || h[6] < h[4] * sizeof(style_t) + h[5] * sizeof(song_t))
        goto done;
    if (!(blob = malloc(h[6] ? h[6] : 1)))
        goto done;
    if(file_bytes!=sizeof h+h[6]||!bc_read(f,blob,h[6]))goto done;
    {int closed=fclose(f);f=0;if(closed)goto done;}
    bc_hash(&a, &b, h, 8 * 4);
    bc_hash(&a, &b, blob, h[6]);
    if (a != h[8] || b != h[9])
        goto done;
    style_t *st = (style_t *)blob;
    song_t *sg = (song_t *)(blob + h[4] * sizeof(style_t));
    for (unsigned i = 0; i < h[4]; i++)
        if (!memchr(st[i].dir, 0, sizeof st[i].dir) || !memchr(st[i].name, 0, sizeof st[i].name) ||
            !memchr(st[i].label, 0, sizeof st[i].label) ||
            !memchr(st[i].caption, 0, sizeof st[i].caption))
            goto done;
    p = (u8 *)(sg + h[5]);
    end = blob + h[6];
    for (unsigned i = 0; i < h[5]; i++) {
        u32 n;
        if (!memchr(sg[i].dir, 0, sizeof sg[i].dir) || sg[i].style < 0 ||
            sg[i].style >= (int)h[4] || end - p < 4)
            goto done;
        memcpy(&n, p, 4);
        p += 4;
        if (n > 16384 || (unsigned)(end - p) < n + 1 || p[n] || memchr(p, 0, n))
            goto done;
        p += n + 1;
    }
    if (p != end)
        goto done;
    p = (u8 *)(sg + h[5]);
    for (unsigned i = 0; i < h[5]; i++) {
        u32 n;
        memcpy(&n, p, 4);
        p += 4;
        snprintf(rel, sizeof rel, "songs/%.47s/%.63s/song.txt", st[sg[i].style].dir, sg[i].dir);
        lm_text_keep(rel, (char *)p, n);
        p += n + 1;
        lm_entry *e = lm_find(rel, 0);
        if (!e || !e->text)
            goto done; /* Cache budget exhaustion uses live discovery. */
    }
    memcpy(styles, st, h[4] * sizeof *styles);
    memcpy(songs, sg, h[5] * sizeof *songs);
    n_styles = h[4];
    n_songs = h[5];
    sc_hit = hit = 1;
done:
    free(blob);
    if (f)
        fclose(f);
    return hit;
}
static void sc_store(void)
{
    u32 key[2], h[10] = {0x374d434du, 1, 0, 0, n_styles, n_songs, 0, 0, 0, 0};
    u8 *blob = 0, *p;
    unsigned bytes = n_styles * sizeof(style_t) + n_songs * sizeof(song_t);
    char path[256], tmp[260];
    FILE *f = 0;
    int ok;
    if (!sc_key(key))
        return;
    h[2] = key[0];
    h[3] = key[1];
    for (int i = 0; i < n_songs; i++) {
        snprintf(path, sizeof path, "songs/%.47s/%.63s/song.txt", styles[songs[i].style].dir,
                 songs[i].dir);
        lm_entry *e = lm_find(path, 0);
        if (!e || !e->text || e->size < 0 || e->size > 16384 || bytes > LM_LIMIT - e->size - 5)
            return;
        bytes += e->size + 5;
    }
    if (!(blob = malloc(bytes ? bytes : 1)))
        return;
    p = blob;
    memcpy(p, styles, n_styles * sizeof *styles);
    p += n_styles * sizeof *styles;
    memcpy(p, songs, n_songs * sizeof *songs);
    p += n_songs * sizeof *songs;
    for (int i = 0; i < n_songs; i++) {
        snprintf(path, sizeof path, "songs/%.47s/%.63s/song.txt", styles[songs[i].style].dir,
                 songs[i].dir);
        lm_entry *e = lm_find(path, 0);
        u32 n = e->size;
        memcpy(p, &n, 4);
        p += 4;
        memcpy(p, e->text, n + 1);
        p += n + 1;
    }
    h[6] = bytes;
    h[8] = 2166136261u;
    h[9] = 5381u;
    bc_hash(h + 8, h + 9, h, 8 * 4);
    bc_hash(h + 8, h + 9, blob, bytes);
    snprintf(path, sizeof path, "%scache/menu_%08x%08x.bin",DEV, (unsigned)key[0], (unsigned)key[1]);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if ((f = fopen(tmp, "wb"))) {
        ok = fwrite(h, 1, sizeof h, f) == sizeof h && fwrite(blob, 1, bytes, f) == bytes;
        if (fclose(f))
            ok = 0;
        if (ok && !rename(tmp, path))
            sc_saved = 1;
    }
    free(blob);
}
#endif
