/* CamadamOS FAT16 / FAT32 driver for USB sticks (read + write, long file names) */
#include "kernel.h"

fatinfo_t fat;
static struct { u32 part, spc, rsvd, nfats, fatsz, root_ents, root_lba, root_secs, data_lba, nclus, root_clus, fsinfo; int type; u32 hint; int fsinfo_done; } V;
static u8 fatbuf[512]; static u32 fatbuf_lba=0xFFFFFFFF;

static int rd(u32 lba,u32 n,void *b){ return usb_msc_read(V.part+lba,n,b); }
static int wr(u32 lba,u32 n,const void *b){ return usb_msc_write(V.part+lba,n,b); }
static u16 g16(const u8 *p){ return p[0]|(p[1]<<8); }
static u32 g32(const u8 *p){ return p[0]|(p[1]<<8)|(p[2]<<16)|((u32)p[3]<<24); }
static void p16(u8 *p,u16 v){ p[0]=v; p[1]=v>>8; }
static void p32(u8 *p,u32 v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

/* ---------- FAT table ---------- */
static int fat_load(u32 lba){ if(lba==fatbuf_lba) return 1; if(!rd(lba,1,fatbuf)){ fatbuf_lba=0xFFFFFFFF; return 0; } fatbuf_lba=lba; return 1; }
static u32 fget(u32 c){
    u32 off = V.type==32 ? c*4 : c*2;
    if(!fat_load(V.rsvd+off/512)) return 0x0FFFFFFF;
    return V.type==32 ? g32(fatbuf+off%512)&0x0FFFFFFF : g16(fatbuf+off%512);
}
static int fset(u32 c,u32 v){
    u32 off = V.type==32 ? c*4 : c*2, lba=V.rsvd+off/512;
    if(!fat_load(lba)) return 0;
    if(V.type==32) p32(fatbuf+off%512,(g32(fatbuf+off%512)&0xF0000000)|(v&0x0FFFFFFF)); else p16(fatbuf+off%512,v);
    for(u32 i=0;i<V.nfats;i++) if(!wr(lba+i*V.fatsz,1,fatbuf)) return 0;
    return 1;
}
static int is_end(u32 c){ return V.type==32 ? (c>=0x0FFFFFF8 || c<2) : (c>=0xFFF8 || c<2); }
static u32 eoc(void){ return V.type==32 ? 0x0FFFFFFF : 0xFFFF; }
static u32 clus_lba(u32 c){ return V.data_lba+(c-2)*V.spc; }
static u32 csize(void){ return V.spc*512; }
static void fsinfo_invalidate(void){
    if(V.type!=32 || V.fsinfo_done || !V.fsinfo || V.fsinfo==0xFFFF) return;
    u8 b[512]; if(rd(V.fsinfo,1,b) && g32(b)==0x41615252){ p32(b+488,0xFFFFFFFF); p32(b+492,0xFFFFFFFF); wr(V.fsinfo,1,b); }
    V.fsinfo_done=1;
}
static u32 alloc_clus(u32 prev){
    fsinfo_invalidate();
    u32 n=V.nclus+2;
    for(u32 k=0;k<V.nclus;k++){
        u32 c=V.hint+k; if(c>=n) c=2+(c-n);
        if(c<2) continue;
        if(fget(c)==0){
            if(!fset(c,eoc())) return 0;
            if(prev && !fset(prev,c)) return 0;
            V.hint=c+1; return c;
        }
    }
    return 0;
}
static void free_chain(u32 c){
    int guard=0;
    while(!is_end(c) && c<V.nclus+2 && guard++<4000000){ u32 n=fget(c); fset(c,0); c=n; }
}

/* ---------- directories ---------- */
typedef struct { u32 clus, last; u8 *buf; u32 n; u32 *lba; u32 nsec; } dir_t;
static void dir_free(dir_t *d){ kfree(d->buf); kfree(d->lba); d->buf=0; d->lba=0; }
static int dir_load(u32 clus,dir_t *d){
    memset(d,0,sizeof *d); d->clus=clus;
    if(clus==0){
        d->nsec=V.root_secs; d->buf=kmalloc(d->nsec*512); d->lba=kmalloc(d->nsec*4);
        if(!d->buf||!d->lba) return 0;
        for(u32 i=0;i<d->nsec;i++) d->lba[i]=V.root_lba+i;
        if(!rd(V.root_lba,d->nsec,d->buf)) return 0;
    } else {
        u32 cnt=0, c=clus; while(!is_end(c) && cnt<512){ cnt++; c=fget(c); }
        d->nsec=cnt*V.spc; d->buf=kmalloc(d->nsec*512+512); d->lba=kmalloc(d->nsec*4+4);
        if(!d->buf||!d->lba) return 0;
        c=clus; u32 s=0;
        while(!is_end(c) && s<d->nsec){
            if(!rd(clus_lba(c),V.spc,d->buf+s*512)) return 0;
            for(u32 i=0;i<V.spc;i++) d->lba[s+i]=clus_lba(c)+i;
            s+=V.spc; d->last=c; c=fget(c);
        }
    }
    d->n=d->nsec*512/32;
    return 1;
}
static int dir_flush(dir_t *d,u32 idx){ u32 s=idx*32/512; return wr(d->lba[s],1,d->buf+s*512); }
static int dir_grow(dir_t *d){
    if(d->clus==0) return 0;                          /* FAT16 root is fixed size */
    u32 c=alloc_clus(d->last); if(!c) return 0;
    u8 *z=kzalloc(csize()); if(!z) return 0; wr(clus_lba(c),V.spc,z); kfree(z);
    u8 *nb=kmalloc((d->nsec+V.spc)*512+512); u32 *nl=kmalloc((d->nsec+V.spc)*4+4);
    if(!nb||!nl) return 0;
    memcpy(nb,d->buf,d->nsec*512); memset(nb+d->nsec*512,0,V.spc*512); memcpy(nl,d->lba,d->nsec*4);
    for(u32 i=0;i<V.spc;i++) nl[d->nsec+i]=clus_lba(c)+i;
    kfree(d->buf); kfree(d->lba); d->buf=nb; d->lba=nl; d->nsec+=V.spc; d->n=d->nsec*512/32; d->last=c;
    return 1;
}
static u8 lfn_sum(const u8 *n){ u8 s=0; for(int i=0;i<11;i++) s=((s&1)<<7)+(s>>1)+n[i]; return s; }
static int lower(int c){ return (c>='A'&&c<='Z')?c+32:c; }
static int ieq(const char *a,const char *b){ while(*a && lower(*a)==lower(*b)){ a++; b++; } return !*a && !*b; }
static void short_name(const u8 *e,char *o){
    int k=0;
    for(int i=0;i<8 && e[i]!=' ';i++) o[k++]= (e[12]&8) ? lower(e[i]) : e[i];
    if(e[8]!=' '){ o[k++]='.'; for(int i=8;i<11 && e[i]!=' ';i++) o[k++]= (e[12]&16) ? lower(e[i]) : e[i]; }
    o[k]=0; if(o[0]==0x05) o[0]=(char)0xE5;
}
/* walk entries: callback gets (index of short entry, index of first LFN entry, name) */
typedef int (*dir_cb)(dir_t *d,u32 idx,u32 first,const char *name,void *ctx);
static void dir_walk(dir_t *d,dir_cb cb,void *ctx){
    char lname[264]; int have=0; u8 sum=0; u32 first=0;
    for(u32 i=0;i<d->n;i++){
        u8 *e=d->buf+i*32;
        if(e[0]==0) return;
        if(e[0]==0xE5){ have=0; continue; }
        if(e[11]==0x0F){
            int seq=e[0]&0x1F;
            if(e[0]&0x40){ have=1; sum=e[13]; first=i; memset(lname,0,sizeof lname); }
            if(!have || seq<1 || seq>20 || e[13]!=sum){ have=0; continue; }
            static const u8 off[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
            for(int k=0;k<13;k++){
                u16 ch=g16(e+off[k]); int pos=(seq-1)*13+k;
                if(pos>=255) break;
                if(ch==0||ch==0xFFFF){ if(!lname[pos]) lname[pos]=0; continue; }
                lname[pos]= ch<128 ? (char)ch : (ch==0xE7?'c': ch==0xF6?'o': ch==0xFC?'u': ch==0xC7?'C': ch==0xD6?'O': ch==0xDC?'U': ch==0x131?'i': ch==0x130?'I': ch==0x15F?'s': ch==0x15E?'S': ch==0x11F?'g': ch==0x11E?'G':'_');
            }
            continue;
        }
        if(e[11]&8){ have=0; continue; }                 /* volume label */
        char nm[264];
        int lf = have && lfn_sum(e)==sum && lname[0];
        if(lf){ int k=0; while(k<255 && lname[k]) k++; memcpy(nm,lname,k); nm[k]=0; }
        else short_name(e,nm);
        u32 f = lf ? first : i;
        have=0;
        if(nm[0]=='.' && (nm[1]==0 || (nm[1]=='.' && nm[2]==0))) continue;
        if(cb(d,i,f,nm,ctx)) return;
    }
}
static u32 ent_clus(const u8 *e){ return g16(e+26) | (V.type==32 ? (u32)g16(e+20)<<16 : 0); }

typedef struct { const char *want; int idx, first; } find_t;
static int find_cb(dir_t *d,u32 idx,u32 first,const char *name,void *c){ find_t *f=c; (void)d; if(ieq(name,f->want)){ f->idx=idx; f->first=first; return 1; } return 0; }
static int dir_find(dir_t *d,const char *name,u32 *first){ find_t f={name,-1,-1}; dir_walk(d,find_cb,&f); if(first) *first=f.first; return f.idx; }

static u32 root_clus(void){ return V.type==32 ? V.root_clus : 0; }
/* resolve a directory path to its first cluster; returns -1 if missing */
static long long resolve_dir(const char *path){
    u32 c=root_clus(); char part[256];
    while(*path=='/') path++;
    while(*path){
        int n=0; while(*path && *path!='/' && n<255) part[n++]=*path++; part[n]=0; while(*path=='/') path++;
        dir_t d; if(!dir_load(c,&d)){ dir_free(&d); return -1; }
        int i=dir_find(&d,part,0);
        if(i<0 || !(d.buf[i*32+11]&0x10)){ dir_free(&d); return -1; }
        c=ent_clus(d.buf+i*32); dir_free(&d);
        if(c==0) c=root_clus();
    }
    return c;
}
/* split "/a/b/name" into dir cluster + name */
static long long split(const char *path,char *name){
    char dirp[300]; strncpy0(dirp,path,sizeof dirp);
    int l=strlen(dirp); while(l>1 && dirp[l-1]=='/') dirp[--l]=0;
    char *slash=0; for(char *q=dirp;*q;q++) if(*q=='/') slash=q;
    if(slash){ strncpy0(name,slash+1,256); *slash=0; } else { strncpy0(name,dirp,256); dirp[0]=0; }
    return resolve_dir(dirp);
}

/* ---------- public API ---------- */
typedef struct { fat_ent_t *out; int max, n; } list_t;
static int list_cb(dir_t *d,u32 idx,u32 first,const char *name,void *c){
    list_t *L=c; (void)first;
    if(L->n>=L->max) return 1;
    u8 *e=d->buf+idx*32; fat_ent_t *o=&L->out[L->n++];
    strncpy0(o->name,name,sizeof o->name); o->size=g32(e+28); o->dir=(e[11]&0x10)!=0;
    return 0;
}
int fat_list(const char *path,fat_ent_t *out,int max){
    if(!fat.mounted) return -1;
    long long c=resolve_dir(path); if(c<0) return -1;
    dir_t d; if(!dir_load((u32)c,&d)){ dir_free(&d); return -1; }
    list_t L={out,max,0}; dir_walk(&d,list_cb,&L); dir_free(&d);
    /* folders first, then by name */
    for(int i=1;i<L.n;i++){ fat_ent_t t=out[i]; int j=i-1;
        while(j>=0 && (out[j].dir<t.dir || (out[j].dir==t.dir && strcmp(out[j].name,t.name)>0))){ out[j+1]=out[j]; j--; } out[j+1]=t; }
    return L.n;
}
int fat_read(const char *path,u8 **data,u32 *len){
    if(!fat.mounted) return 0;
    char name[256]; long long dc=split(path,name); if(dc<0) return 0;
    dir_t d; if(!dir_load((u32)dc,&d)){ dir_free(&d); return 0; }
    int i=dir_find(&d,name,0); if(i<0 || (d.buf[i*32+11]&0x10)){ dir_free(&d); return 0; }
    u32 size=g32(d.buf+i*32+28), c=ent_clus(d.buf+i*32); dir_free(&d);
    if(size>24*1024*1024) return 0;
    u8 *b=kmalloc(size+1); if(!b) return 0;
    u8 *cb=kmalloc(csize()); if(!cb){ kfree(b); return 0; }
    u32 got=0;
    while(got<size && !is_end(c)){
        if(!rd(clus_lba(c),V.spc,cb)){ kfree(cb); kfree(b); return 0; }
        u32 n=size-got; if(n>csize()) n=csize();
        memcpy(b+got,cb,n); got+=n; c=fget(c);
    }
    kfree(cb);
    b[got]=0; *data=b; *len=got; return 1;
}
static void stamp(u8 *e){
    int h,m,s,dd,mo,y; rtc_time(&h,&m,&s); rtc_date(&dd,&mo,&y); if(y<100) y+=2000; if(y<1980) y=1980;
    u16 t=(h<<11)|(m<<5)|(s/2), dt=((y-1980)<<9)|(mo<<5)|dd;
    p16(e+14,t); p16(e+16,dt); p16(e+18,dt); p16(e+22,t); p16(e+24,dt);
}
static int valid83(int c){ return (c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='~'||c=='!'||c=='#'||c=='$'||c=='%'||c=='&'||c=='('||c==')'||c=='@'||c=='^'||c=='{'||c=='}'; }
static int short_exists(dir_t *d,const u8 *sn){
    for(u32 i=0;i<d->n;i++){ u8 *e=d->buf+i*32; if(e[0]==0) break; if(e[0]==0xE5||e[11]==0x0F) continue; if(!memcmp(e,sn,11)) return 1; }
    return 0;
}
static int make_short(dir_t *d,const char *name,u8 *sn){
    memset(sn,' ',11);
    const char *dot=0; for(const char *q=name;*q;q++) if(*q=='.') dot=q;
    if(dot==name) dot=0;
    int lossy=0, bl=0, el=0;
    char base[64], ext[8];
    for(const char *q=name; *q && q!=dot; q++){ int c=*q; if(c=='.'||c==' '){ lossy=1; continue; }
        if(c>='a'&&c<='z'){ c-=32; lossy=1; } if(!valid83(c)){ c='_'; lossy=1; } if(bl<63) base[bl++]=c; }
    if(dot) for(const char *q=dot+1;*q;q++){ int c=*q; if(c==' '||c=='.'){ lossy=1; continue; }
        if(c>='a'&&c<='z'){ c-=32; lossy=1; } if(!valid83(c)){ c='_'; lossy=1; } if(el<3) ext[el++]=c; else lossy=1; }
    if(bl>8) lossy=1;
    if(!bl){ base[0]='_'; bl=1; lossy=1; }
    for(int i=0;i<el;i++) sn[8+i]=ext[i];
    if(!lossy){ for(int i=0;i<bl;i++) sn[i]=base[i]; if(!short_exists(d,sn)) return 0; }
    for(int n=1;n<1000;n++){
        char num[8]; itoa10(n,num); int nl=strlen(num);
        int keep = 8-1-nl; if(keep>bl) keep=bl;
        memset(sn,' ',8);
        for(int i=0;i<keep;i++) sn[i]=base[i];
        sn[keep]='~'; for(int i=0;i<nl;i++) sn[keep+1+i]=num[i];
        if(!short_exists(d,sn)) return 1;
    }
    return 1;
}
/* find (or make) k consecutive free entries; returns first index or -1 */
static int find_free(dir_t *d,int k){
    for(;;){
        int run=0;
        for(u32 i=0;i<d->n;i++){
            u8 *e=d->buf+i*32;
            if(e[0]==0xE5 || e[0]==0){ if(++run==k) return i-k+1; } else run=0;
        }
        if(!dir_grow(d)) return -1;
    }
}
/* create a directory entry for name; returns index of short entry or -1 */
static int create_entry(dir_t *d,const char *name,int attr){
    u8 sn[11]; int need=make_short(d,name,sn);
    int nlen=strlen(name); if(nlen>255) return -1;
    int nl = need ? (nlen+12)/13 : 0;
    int at=find_free(d,nl+1); if(at<0) return -1;
    u8 sum=lfn_sum(sn);
    static const u8 off[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    for(int k=0;k<nl;k++){
        int seq=nl-k; u8 *e=d->buf+(at+k)*32; memset(e,0,32);
        e[0]=seq|(k==0?0x40:0); e[11]=0x0F; e[13]=sum;
        for(int j=0;j<13;j++){ int pos=(seq-1)*13+j; u16 ch = pos<nlen ? (u8)name[pos] : pos==nlen ? 0 : 0xFFFF; p16(e+off[j],ch); }
    }
    u8 *e=d->buf+(at+nl)*32; memset(e,0,32); memcpy(e,sn,11); e[11]=attr; stamp(e);
    for(int k=0;k<=nl;k++) dir_flush(d,at+k);
    return at+nl;
}
static u32 write_data(const u8 *data,u32 len){
    if(!len) return 0;
    u8 *cb=kmalloc(csize()); if(!cb) return 0;
    u32 first=0, prev=0, done=0;
    while(done<len){
        u32 c=alloc_clus(prev); if(!c){ kfree(cb); if(first) free_chain(first); return 0; }
        if(!first) first=c;
        u32 n=len-done; if(n>csize()) n=csize();
        memcpy(cb,data+done,n); if(n<csize()) memset(cb+n,0,csize()-n);
        if(!wr(clus_lba(c),V.spc,cb)){ kfree(cb); free_chain(first); return 0; }
        done+=n; prev=c;
    }
    kfree(cb); return first;
}
int fat_write(const char *path,const u8 *data,u32 len){
    if(!fat.mounted) return 0;
    char name[256]; long long dc=split(path,name); if(dc<0 || !name[0]) return 0;
    dir_t d; if(!dir_load((u32)dc,&d)){ dir_free(&d); return 0; }
    int i=dir_find(&d,name,0);
    if(i>=0){
        if(d.buf[i*32+11]&0x10){ dir_free(&d); return 0; }
        free_chain(ent_clus(d.buf+i*32));
    } else {
        i=create_entry(&d,name,0x20);
        if(i<0){ dir_free(&d); return 0; }
    }
    u32 c=write_data(data,len);
    if(len && !c){ dir_free(&d); return 0; }
    u8 *e=d.buf+i*32;
    p16(e+26,c&0xFFFF); p16(e+20,V.type==32?c>>16:0); p32(e+28,len); e[11]=0x20; stamp(e);
    int ok=dir_flush(&d,i); dir_free(&d);
    return ok;
}
int fat_mkdir(const char *path){
    if(!fat.mounted) return 0;
    char name[256]; long long dc=split(path,name); if(dc<0 || !name[0]) return 0;
    dir_t d; if(!dir_load((u32)dc,&d)){ dir_free(&d); return 0; }
    if(dir_find(&d,name,0)>=0){ dir_free(&d); return 0; }
    u32 c=alloc_clus(0); if(!c){ dir_free(&d); return 0; }
    u8 *cb=kzalloc(csize());
    memset(cb,' ',11); cb[0]='.'; cb[11]=0x10; p16(cb+26,c&0xFFFF); p16(cb+20,V.type==32?c>>16:0); stamp(cb);
    u8 *e2=cb+32; memset(e2,' ',11); e2[0]='.'; e2[1]='.'; e2[11]=0x10;
    u32 pc = ((u32)dc==root_clus()) ? 0 : (u32)dc; p16(e2+26,pc&0xFFFF); p16(e2+20,V.type==32?pc>>16:0); stamp(e2);
    wr(clus_lba(c),V.spc,cb); kfree(cb);
    int i=create_entry(&d,name,0x10);
    if(i<0){ free_chain(c); dir_free(&d); return 0; }
    u8 *e=d.buf+i*32; p16(e+26,c&0xFFFF); p16(e+20,V.type==32?c>>16:0);
    int ok=dir_flush(&d,i); dir_free(&d); return ok;
}
static int count_cb(dir_t *d,u32 i,u32 f,const char *n,void *c){ (void)d;(void)i;(void)f;(void)n; (*(int*)c)++; return 0; }
int fat_delete(const char *path){
    if(!fat.mounted) return 0;
    char name[256]; long long dc=split(path,name); if(dc<0 || !name[0]) return 0;
    dir_t d; if(!dir_load((u32)dc,&d)){ dir_free(&d); return 0; }
    u32 first; int i=dir_find(&d,name,&first); if(i<0){ dir_free(&d); return 0; }
    u32 c=ent_clus(d.buf+i*32);
    if(d.buf[i*32+11]&0x10){
        dir_t sub; int n=0;
        if(dir_load(c,&sub)) dir_walk(&sub,count_cb,&n);
        dir_free(&sub);
        if(n){ dir_free(&d); return -2; }               /* folder not empty */
    }
    for(u32 k=first;k<=(u32)i;k++){ d.buf[k*32]=0xE5; dir_flush(&d,k); }
    free_chain(c);
    dir_free(&d); return 1;
}

void fat_unmount(void){
    if(fat.mounted) wm_toast("USB stick removed");
    memset(&fat,0,sizeof fat); fatbuf_lba=0xFFFFFFFF;
    strncpy0(fat.status,"No USB stick",sizeof fat.status);
}
static int bpb_ok(const u8 *b){
    int bps=g16(b+11), spc=b[13];
    return bps==512 && spc && !(spc&(spc-1)) && g16(b+14) && (b[16]==1||b[16]==2) && (b[0]==0xEB||b[0]==0xE9);
}
void fat_mount(void){
    memset(&fat,0,sizeof fat); memset(&V,0,sizeof V); fatbuf_lba=0xFFFFFFFF;
    u8 *b=kmalloc(512); if(!b) return;
    if(!usb_msc_read(0,1,b)){ strncpy0(fat.status,"Can't read the stick",sizeof fat.status); kfree(b); wm_toast(fat.status); return; }
    u32 part=0;
    int superfloppy = bpb_ok(b) && (!memcmp(b+54,"FAT",3) || !memcmp(b+82,"FAT",3));
    if(!superfloppy && b[510]==0x55 && b[511]==0xAA){
        for(int i=0;i<4;i++){
            u8 *pe=b+446+i*16; u8 t=pe[4]; u32 lba=g32(pe+8);
            if((pe[0]!=0 && pe[0]!=0x80) || !lba || lba>=usbstick.sectors) continue;
            if(t==0xEE){                                   /* GPT: use the first partition */
                u8 *h=kmalloc(512);
                if(usb_msc_read(1,1,h) && !memcmp(h,"EFI PART",8)){
                    u32 el=g32(h+72); u32 esz=g32(h+84);
                    if(usb_msc_read(el,1,h)) for(u32 k=0;k*esz<512 && esz;k++){ u8 *ge=h+k*esz; int z=1; for(int q=0;q<16;q++) if(ge[q]) z=0; if(!z){ part=g32(ge+32); break; } }
                }
                kfree(h); break;
            }
            if(t==0x04||t==0x06||t==0x0E||t==0x0B||t==0x0C||t==0x07||t==0x01){ part=lba; break; }
        }
    }
    V.part=part;
    if(part && !usb_msc_read(part,1,b)){ strncpy0(fat.status,"Can't read the stick",sizeof fat.status); kfree(b); wm_toast(fat.status); return; }
    if(!memcmp(b+3,"EXFAT   ",8)){ strncpy0(fat.status,"exFAT stick - format it as FAT32 to use it here",sizeof fat.status); kfree(b); wm_toast("USB stick is exFAT (not supported yet)"); return; }
    if(!memcmp(b+3,"NTFS    ",8)){ strncpy0(fat.status,"NTFS stick - format it as FAT32 to use it here",sizeof fat.status); kfree(b); wm_toast("USB stick is NTFS (not supported)"); return; }
    if(!bpb_ok(b)){ strncpy0(fat.status,"Unknown format - format it as FAT32",sizeof fat.status); kfree(b); wm_toast("USB stick: unknown format"); return; }
    V.spc=b[13]; V.rsvd=g16(b+14); V.nfats=b[16]; V.root_ents=g16(b+17);
    u32 total = g16(b+19) ? g16(b+19) : g32(b+32);
    V.fatsz = g16(b+22) ? g16(b+22) : g32(b+36);
    V.root_secs=(V.root_ents*32+511)/512;
    V.root_lba=V.rsvd+V.nfats*V.fatsz; V.data_lba=V.root_lba+V.root_secs;
    V.nclus=(total-V.data_lba)/V.spc;
    V.type = V.nclus<4085 ? 12 : V.nclus<65525 ? 16 : 32;
    V.root_clus=g32(b+44); V.fsinfo=g16(b+48); V.hint=2;
    if(V.type==12){ strncpy0(fat.status,"FAT12 is not supported",sizeof fat.status); kfree(b); wm_toast("USB stick: FAT12 not supported"); return; }
    const u8 *lab = V.type==32 ? b+71 : b+43;
    char l[12]; memcpy(l,lab,11); l[11]=0; for(int i=10;i>=0 && l[i]==' ';i--) l[i]=0;
    if(!l[0] || !strcmp(l,"NO NAME")) strncpy0(l,usbstick.product[0]?usbstick.product:"USB stick",12);
    strncpy0(fat.label,l,sizeof fat.label);
    fat.type=V.type; fat.total_mb=total/2048; fat.mounted=1;
    strncpy0(fat.status,V.type==32?"FAT32":"FAT16",sizeof fat.status);
    kfree(b);
    char t[64]="USB stick connected: "; strcat0(t,fat.label,sizeof t); wm_toast(t);
}
