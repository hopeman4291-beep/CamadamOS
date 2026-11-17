/* CamadamOS saving: keeps files and settings on the installed hard disk.
 * The installer creates a data partition (type 0xDA). Its first sector holds
 * a header; after it comes a snapshot of the file system and settings. */
#include "kernel.h"

persist_t store = { -1, 0, 0, 0, 0, "" };
typedef struct {
    char magic[8]; u32 version, length, checksum, count;
} __attribute__((packed)) dhdr_t;
typedef struct {
    int wallpaper, accent, mouse, clk_off, clk12, volume, inst; char name[32];
} __attribute__((packed)) dset_t;
typedef struct {
    u8 used, type, readonly, pad; s16 parent, song; char name[FS_NAME]; u32 size; s32 w, h;
} __attribute__((packed)) dnode_t;

static u32 hash_state(void){
    u32 h=2166136261u;
    #define H(v) do{ u32 _v=(u32)(v); for(int _i=0;_i<4;_i++){ h^=(_v>>(8*_i))&255; h*=16777619u; } }while(0)
    H(fs_rev);
    for(int i=0;i<FS_MAX;i++){ fsnode_t *n=&fs_nodes[i]; H(n->used); if(!n->used) continue; H(n->type); H(n->parent); H(n->size); H((u32)n->data); H(n->readonly); for(int k=0;n->name[k];k++) H(n->name[k]); }
    H(theme_wallpaper); H(theme_accent_idx); H(mouse_speed); H(clock_offset); H(clock_12h); H(snd.volume); H(snd.instrument);
    for(int k=0;user_name[k];k++) H(user_name[k]);
    return h;
    #undef H
}
static u32 last_hash; static u32 changed_at;

/* ---- streaming writer: never needs a second copy of all files in memory ---- */
#define WBUF (64*1024)
static u8 *wbuf, first_sec[512]; static u32 wfill, wlba, wsum; static int wok, wfirst;
static void w_flush(void){
    if(!wfill || !wok) { wfill=0; return; }
    u32 secs=(wfill+511)/512; if(wfill%512) memset(wbuf+wfill,0,secs*512-wfill);
    if(wfirst){ memcpy(first_sec,wbuf,512); wfirst=0; }
    if(!disk_write(store.disk,wlba,secs,wbuf)) wok=0;
    wlba+=secs; wfill=0;
}
static void w_put(const void *d,u32 n,int sum){
    const u8 *p=d;
    if(sum) for(u32 i=0;i<n;i++) wsum=wsum*31+p[i];
    while(n){ u32 c=WBUF-wfill; if(c>n) c=n; memcpy(wbuf+wfill,p,c); wfill+=c; p+=c; n-=c; if(wfill==WBUF) w_flush(); }
}
int store_save(void){
    if(store.disk<0) return 0;
    u32 total=sizeof(dhdr_t)+sizeof(dset_t);
    for(int i=0;i<FS_MAX;i++){ fsnode_t *n=&fs_nodes[i]; total+=sizeof(dnode_t); if(n->used && !n->readonly && n->data) total+=n->size; }
    u32 secs=(total+511)/512;
    if(secs>store.sectors){ strncpy0(store.status,"Disk space for saving is full",sizeof store.status); return 0; }
    if(!wbuf) wbuf=kmalloc(WBUF); if(!wbuf) return 0;
    wfill=0; wlba=store.lba; wsum=0; wok=1; wfirst=1;
    dhdr_t hd; memset(&hd,0,sizeof hd); w_put(&hd,sizeof hd,0);           /* placeholder, patched at the end */
    dset_t st; memset(&st,0,sizeof st);
    st.wallpaper=theme_wallpaper; st.accent=theme_accent_idx; st.mouse=mouse_speed; st.clk_off=clock_offset; st.clk12=clock_12h;
    st.volume=snd.volume; st.inst=snd.instrument; strncpy0(st.name,user_name,32);
    w_put(&st,sizeof st,1);
    for(int i=0;i<FS_MAX;i++){
        fsnode_t *n=&fs_nodes[i]; dnode_t d; memset(&d,0,sizeof d);
        d.used=n->used; d.type=n->type; d.readonly=n->readonly; d.parent=n->parent; d.song=n->song;
        memcpy(d.name,n->name,FS_NAME); d.w=n->img_w; d.h=n->img_h;
        d.size = (n->used && !n->readonly && n->data) ? n->size : 0;
        w_put(&d,sizeof d,1);
        if(d.size) w_put(n->data,d.size,1);
    }
    w_flush();
    memcpy(hd.magic,"CAMADATA",8); hd.version=1; hd.count=FS_MAX; hd.length=total-sizeof(dhdr_t); hd.checksum=wsum;
    memcpy(first_sec,&hd,sizeof hd);
    if(wok && !disk_write(store.disk,store.lba,1,first_sec)) wok=0;     /* header last: a half-written save is ignored on boot */
    int ok = wok && disk_flush(store.disk);
    if(ok){ store.saves++; strncpy0(store.status,"Your files are saved on the hard disk",sizeof store.status); }
    else strncpy0(store.status,"Saving to the disk failed",sizeof store.status);
    return ok;
}

/* ---- streaming reader ---- */
static u8 *rbuf; static u32 rlba, rleft_secs, rpos, rlen;
static void r_start(u32 lba,u32 secs,u32 skip){ rlba=lba; rleft_secs=secs; rpos=rlen=0;
    while(skip){ if(rpos==rlen){ u32 n=rleft_secs>128?128:rleft_secs; if(!n||!disk_read(store.disk,rlba,n,rbuf)) return; rlba+=n; rleft_secs-=n; rpos=0; rlen=n*512; }
        u32 c=rlen-rpos; if(c>skip) c=skip; rpos+=c; skip-=c; } }
static int r_get(void *d,u32 n){
    u8 *o=d;
    while(n){
        if(rpos==rlen){ u32 k=rleft_secs>128?128:rleft_secs; if(!k || !disk_read(store.disk,rlba,k,rbuf)) return 0; rlba+=k; rleft_secs-=k; rpos=0; rlen=k*512; }
        u32 c=rlen-rpos; if(c>n) c=n; if(o){ memcpy(o,rbuf+rpos,c); o+=c; } rpos+=c; n-=c;
    }
    return 1;
}
static void store_load(const dhdr_t *hd){
    if(memcmp(hd->magic,"CAMADATA",8) || hd->version!=1) return;
    u32 secs=(hd->length+sizeof(dhdr_t)+511)/512;
    if(secs>store.sectors) return;
    if(!rbuf) rbuf=kmalloc(128*512); if(!rbuf) return;
    /* pass 1: verify the checksum */
    r_start(store.lba,secs,sizeof(dhdr_t));
    u32 sum=0, left=hd->length; u8 tmp[512];
    while(left){ u32 c=left>512?512:left; if(!r_get(tmp,c)) return; for(u32 i=0;i<c;i++) sum=sum*31+tmp[i]; left-=c; }
    if(sum!=hd->checksum){ strncpy0(store.status,"Saved data was damaged and was ignored",sizeof store.status); return; }
    /* pass 2: apply */
    r_start(store.lba,secs,sizeof(dhdr_t));
    dset_t st; r_get(&st,sizeof st);
    theme_wallpaper=clampi(st.wallpaper,0,N_WALLPAPERS-1); theme_accent_idx=clampi(st.accent,0,N_ACCENTS-1);
    mouse_speed=clampi(st.mouse,1,10); clock_offset=clampi(st.clk_off,-12,14); clock_12h=st.clk12?1:0;
    snd.volume=clampi(st.volume,10,100); snd.instrument=clampi(st.inst,0,INST_COUNT-1); strncpy0(user_name,st.name,32);
    if(!user_name[0]) strncpy0(user_name,"Camadam",32);
    for(u32 i=0;i<hd->count;i++){
        dnode_t d; if(!r_get(&d,sizeof d)) break;
        if(i>=FS_MAX){ r_get(0,d.size); continue; }
        fsnode_t *n=&fs_nodes[i];
        if(d.used && d.readonly){ r_get(0,d.size); continue; }      /* built-in file: keep the copy in the OS */
        if(!d.used){ if(n->used && i>0){ if(!n->readonly) kfree(n->data); n->used=0; } r_get(0,d.size); continue; }
        if(n->used && !n->readonly) kfree(n->data);
        memset(n,0,sizeof *n);
        n->used=1; n->type=d.type; n->parent=d.parent; n->song=d.song; memcpy(n->name,d.name,FS_NAME); n->name[FS_NAME-1]=0;
        n->img_w=d.w; n->img_h=d.h; n->size=d.size;
        if(d.size){ n->data=kmalloc(d.size+1); if(n->data){ r_get(n->data,d.size); n->data[d.size]=0; } else { n->size=0; r_get(0,d.size); } }
    }
    store.loaded=1;
    strncpy0(store.status,"Your files are saved on the hard disk",sizeof store.status);
}
void store_init(void){
    store.disk=-1;
    for(int d=0;d<ndisks;d++){
        u8 *mbr=kmalloc(512); if(!mbr) return;
        if(!disk_read(d,0,1,mbr) || mbr[510]!=0x55 || mbr[511]!=0xAA){ kfree(mbr); continue; }
        for(int i=0;i<4;i++){
            u8 *e=mbr+446+i*16;
            if(e[4]!=0xDA) continue;
            u32 lba=e[8]|(e[9]<<8)|(e[10]<<16)|((u32)e[11]<<24), cnt=e[12]|(e[13]<<8)|(e[14]<<16)|((u32)e[15]<<24);
            u8 *h=kmalloc(512); if(!h) break;
            if(disk_read(d,lba,1,h) && !memcmp(h,"CAMADATA",8)){
                store.disk=d; store.lba=lba; store.sectors=cnt;
                dhdr_t *hd=(dhdr_t*)h;
                if(hd->length) store_load(hd);
                else strncpy0(store.status,"Your files are saved on the hard disk",sizeof store.status);
            }
            kfree(h);
            if(store.disk>=0) break;
        }
        kfree(mbr);
        if(store.disk>=0) break;
    }
    if(store.disk<0) strncpy0(store.status,"Running from USB: files are kept in memory only",sizeof store.status);
    last_hash=hash_state();
}
void store_tick(void){
    if(store.disk<0) return;
    static u32 last; if(ticks-last<1000) return; last=ticks;
    u32 h=hash_state();
    if(h!=last_hash){ if(!changed_at) changed_at=ticks; if(ticks-changed_at>2000){ if(store_save()) last_hash=h; changed_at=0; } }
    else changed_at=0;
}
