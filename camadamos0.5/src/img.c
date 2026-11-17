/* CamadamOS image decoders: GIF, PNG (with a DEFLATE decompressor),
 * baseline JPEG and BMP. Output is 0xRRGGBB pixels; IMG_CLEAR marks
 * transparent pixels. */
#include "kernel.h"

#define MAXPIX (2400u*2400u)
static u32 *alloc_px(int w,int h){
    if(w<=0||h<=0||w>8000||h>8000||(u32)w*(u32)h>MAXPIX) return 0;
    return kmalloc((u32)w*(u32)h*4);
}

/* ======================= GIF ======================= */
static u32 *gif_decode(const u8 *d,u32 n,int *ow,int *oh){
    if(n<13) return 0;
    int W=d[6]|(d[7]<<8), H=d[8]|(d[9]<<8); u8 fl=d[10];
    u32 p=13; u32 gct[256]; int gsize=0;
    if(fl&0x80){ gsize=2<<(fl&7); if(p+gsize*3>n) return 0; for(int i=0;i<gsize;i++) gct[i]=RGB(d[p+i*3],d[p+i*3+1],d[p+i*3+2]); p+=gsize*3; }
    int trans=-1;
    u32 *px=alloc_px(W,H); if(!px) return 0;
    for(u32 i=0;i<(u32)W*H;i++) px[i]=IMG_CLEAR;
    while(p<n){
        u8 b=d[p++];
        if(b==0x3B) break;
        if(b==0x21){
            if(p>=n) break;
            u8 lab=d[p++];
            if(lab==0xF9 && p+5<n && d[p]>=4){ if(d[p+1]&1) trans=d[p+4]; }
            while(p<n && d[p]){ p+=d[p]+1; }
            p++; continue;
        }
        if(b!=0x2C) break;
        if(p+9>n) break;
        int ix=d[p]|(d[p+1]<<8), iy=d[p+2]|(d[p+3]<<8), iw=d[p+4]|(d[p+5]<<8), ih=d[p+6]|(d[p+7]<<8); u8 f=d[p+8]; p+=9;
        u32 lct[256]; u32 *ct=gct; int csize=gsize;
        if(f&0x80){ csize=2<<(f&7); if(p+csize*3>n) break; for(int i=0;i<csize;i++) lct[i]=RGB(d[p+i*3],d[p+i*3+1],d[p+i*3+2]); p+=csize*3; ct=lct; }
        int interlace=f&0x40;
        if(p>=n) break;
        int mcs=d[p++]; if(mcs<2||mcs>8) break;
        /* gather sub-blocks */
        u32 start=p, total=0; while(p<n && d[p]){ total+=d[p]; p+=d[p]+1; } p++;
        u8 *data=kmalloc(total+4); if(!data) break;
        u32 q=start, k=0; while(q<n && d[q]){ u32 l=d[q]; if(q+1+l>n) break; memcpy(data+k,d+q+1,l); k+=l; q+=l+1; }
        /* LZW */
        static u16 prefix[4096]; static u8 suffix[4096], stack[4097];
        int clear=1<<mcs, eoi=clear+1, size=mcs+1, next=clear+2, old=-1; u8 first=0;
        u32 bitpos=0, bits=k*8; u32 pix=0, total_pix=(u32)iw*ih;
        int pass=0, row=0, col=0;
        static const int istart[4]={0,4,2,1}, istep[4]={8,8,4,2};
        for(int i=0;i<clear;i++){ prefix[i]=0xFFFF; suffix[i]=i; }
        while(bitpos+size<=bits && pix<total_pix){
            u32 code=0; for(int b2=0;b2<size;b2++,bitpos++) code|=((data[bitpos>>3]>>(bitpos&7))&1)<<b2;
            if((int)code==clear){ size=mcs+1; next=clear+2; old=-1; continue; }
            if((int)code==eoi) break;
            int sp=0, c=code;
            if(old<0){ if(c>=clear) break; stack[sp++]=(u8)c; first=(u8)c; }
            else {
                if(c>=next){ if(c>next) break; stack[sp++]=first; c=old; }
                while(c>=clear && sp<4096){ stack[sp++]=suffix[c]; c=prefix[c]; }
                stack[sp++]=(u8)c; first=(u8)c;
                if(next<4096){ prefix[next]=old; suffix[next]=first; next++; if(next==(1<<size) && size<12) size++; }
            }
            old=code;
            while(sp && pix<total_pix){
                u8 idx=stack[--sp];
                int y = interlace ? row : (int)(pix/iw);
                int x = interlace ? col : (int)(pix%iw);
                int X=ix+x, Y=iy+y;
                if(X<W && Y<H && idx<csize) px[Y*W+X] = (idx==trans) ? IMG_CLEAR : ct[idx];
                pix++;
                if(interlace){ if(++col>=iw){ col=0; row+=istep[pass]; while(row>=ih && pass<3){ pass++; row=istart[pass]; } } }
            }
        }
        kfree(data);
        break; /* first frame only */
    }
    *ow=W; *oh=H; return px;
}

/* ======================= DEFLATE (inflate) ======================= */
typedef struct { const u8 *in; u32 inlen, pos; u32 bitbuf; int bitcnt; u8 *out; u32 outlen, outpos; int err; } inf_t;
typedef struct { short count[16]; short sym[320]; } huff_t;
static int bits(inf_t *s,int need){
    u32 v=s->bitbuf;
    while(s->bitcnt<need){ if(s->pos>=s->inlen){ s->err=1; return 0; } v|=(u32)s->in[s->pos++]<<s->bitcnt; s->bitcnt+=8; }
    s->bitbuf=v>>need; s->bitcnt-=need; return (int)(v&((1u<<need)-1));
}
static int decode(inf_t *s,huff_t *h){
    int code=0, first=0, index=0;
    for(int len=1;len<=15;len++){
        code|=bits(s,1); int count=h->count[len];
        if(code-count<first) return h->sym[index+(code-first)];
        index+=count; first+=count; first<<=1; code<<=1;
        if(s->err) return -1;
    }
    s->err=1; return -1;
}
static int construct(huff_t *h,const short *len,int n){
    short offs[16];
    for(int i=0;i<16;i++) h->count[i]=0;
    for(int i=0;i<n;i++) h->count[len[i]]++;
    if(h->count[0]==n) return 0;
    offs[1]=0; for(int i=1;i<15;i++) offs[i+1]=offs[i]+h->count[i];
    for(int i=0;i<n;i++) if(len[i]) h->sym[offs[len[i]]++]=i;
    return 0;
}
static const short lbase[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const short lext[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const short dbase[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const short dext[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static int codes(inf_t *s,huff_t *lc,huff_t *dc){
    for(;;){
        int sym=decode(s,lc); if(sym<0) return -1;
        if(sym<256){ if(s->outpos>=s->outlen) return -1; s->out[s->outpos++]=(u8)sym; }
        else if(sym==256) return 0;
        else {
            sym-=257; if(sym>=29) return -1;
            int len=lbase[sym]+bits(s,lext[sym]);
            int ds=decode(s,dc); if(ds<0||ds>=30) return -1;
            u32 dist=dbase[ds]+bits(s,dext[ds]);
            if(dist>s->outpos || s->outpos+len>s->outlen) return -1;
            for(int i=0;i<len;i++){ s->out[s->outpos]=s->out[s->outpos-dist]; s->outpos++; }
        }
        if(s->err) return -1;
    }
}
static int inflate(u8 *out,u32 outlen,const u8 *in,u32 inlen){
    static inf_t s; static huff_t lc, dc; short lens[320];
    memset(&s,0,sizeof s); s.in=in; s.inlen=inlen; s.out=out; s.outlen=outlen;
    int last;
    do {
        last=bits(&s,1); int type=bits(&s,2); if(s.err) return -1;
        if(type==0){
            s.bitbuf=0; s.bitcnt=0;
            if(s.pos+4>inlen) return -1;
            u32 len=in[s.pos]|(in[s.pos+1]<<8); s.pos+=4;
            if(s.pos+len>inlen || s.outpos+len>outlen) return -1;
            memcpy(out+s.outpos,in+s.pos,len); s.pos+=len; s.outpos+=len;
        } else if(type==1){
            int i=0; for(;i<144;i++) lens[i]=8; for(;i<256;i++) lens[i]=9; for(;i<280;i++) lens[i]=7; for(;i<288;i++) lens[i]=8;
            construct(&lc,lens,288);
            for(i=0;i<30;i++) lens[i]=5; construct(&dc,lens,30);
            if(codes(&s,&lc,&dc)) return -1;
        } else if(type==2){
            int nlen=bits(&s,5)+257, ndist=bits(&s,5)+1, ncode=bits(&s,4)+4;
            static const short order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            for(int i=0;i<19;i++) lens[order[i]]= i<ncode ? bits(&s,3) : 0;
            construct(&lc,lens,19);
            int idx=0;
            while(idx<nlen+ndist){
                int sym=decode(&s,&lc); if(sym<0) return -1;
                if(sym<16) lens[idx++]=sym;
                else { int len=0, rep;
                    if(sym==16){ if(!idx) return -1; len=lens[idx-1]; rep=3+bits(&s,2); }
                    else if(sym==17) rep=3+bits(&s,3); else rep=11+bits(&s,7);
                    if(idx+rep>nlen+ndist) return -1;
                    while(rep--) lens[idx++]=len; }
            }
            construct(&lc,lens,nlen); construct(&dc,lens+nlen,ndist);
            if(codes(&s,&lc,&dc)) return -1;
        } else return -1;
    } while(!last);
    return (int)s.outpos;
}

/* ======================= PNG ======================= */
static u32 be32(const u8 *p){ return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }
static int paeth(int a,int b,int c){ int p=a+b-c, pa=iabs(p-a), pb=iabs(p-b), pc=iabs(p-c); return (pa<=pb&&pa<=pc)?a:(pb<=pc)?b:c; }
static u32 *png_decode(const u8 *d,u32 n,int *ow,int *oh){
    if(n<33) return 0;
    u32 p=8; int W=0,H=0,depth=0,ctype=0,inter=0;
    u32 pal[256]; u8 palpha[256]; for(int i=0;i<256;i++){ pal[i]=0; palpha[i]=255; }
    int trns_gray=-1;
    u8 *idat=0; u32 idlen=0, idcap=0;
    while(p+8<=n){
        u32 len=be32(d+p); const u8 *t=d+p+4, *c=d+p+8;
        if(p+12+len>n) break;
        if(!memcmp(t,"IHDR",4)){ W=be32(c); H=be32(c+4); depth=c[8]; ctype=c[9]; inter=c[12]; }
        else if(!memcmp(t,"PLTE",4)){ for(u32 i=0;i<len/3 && i<256;i++) pal[i]=RGB(c[i*3],c[i*3+1],c[i*3+2]); }
        else if(!memcmp(t,"tRNS",4)){ if(ctype==3) for(u32 i=0;i<len && i<256;i++) palpha[i]=c[i]; else if(ctype==0 && len>=2) trns_gray=(c[0]<<8)|c[1]; }
        else if(!memcmp(t,"IDAT",4)){
            if(idlen+len>idcap){ u32 nc=(idlen+len)*2+65536; u8 *nb=kmalloc(nc); if(!nb){ kfree(idat); return 0; } if(idat){ memcpy(nb,idat,idlen); kfree(idat); } idat=nb; idcap=nc; }
            memcpy(idat+idlen,c,len); idlen+=len;
        }
        else if(!memcmp(t,"IEND",4)) break;
        p+=12+len;
    }
    if(!idat || inter || !W || !H){ kfree(idat); return 0; }
    int ch = ctype==0?1: ctype==2?3: ctype==3?1: ctype==4?2: ctype==6?4:0;
    if(!ch || (depth!=8 && depth!=16 && !((ctype==0||ctype==3) && depth<8))){ kfree(idat); return 0; }
    int bpp_bits=ch*depth, bpp=(bpp_bits+7)/8; u32 stride=((u32)W*bpp_bits+7)/8;
    u32 rawlen=(stride+1)*H;
    if((u32)W*(u32)H>MAXPIX){ kfree(idat); return 0; }
    u8 *raw=kmalloc(rawlen); if(!raw){ kfree(idat); return 0; }
    int got=inflate(raw,rawlen,idat+2,idlen>6?idlen-2:0);
    kfree(idat);
    if(got<(int)rawlen){ kfree(raw); return 0; }
    u32 *px=alloc_px(W,H); if(!px){ kfree(raw); return 0; }
    u8 *prev=0;
    for(int y=0;y<H;y++){
        u8 *row=raw+y*(stride+1); int f=row[0]; u8 *cur=row+1;
        for(u32 i=0;i<stride;i++){
            int a=i>=(u32)bpp?cur[i-bpp]:0, b=prev?prev[i]:0, c=(prev&&i>=(u32)bpp)?prev[i-bpp]:0;
            switch(f){ case 1: cur[i]+=a; break; case 2: cur[i]+=b; break; case 3: cur[i]+=(a+b)/2; break; case 4: cur[i]+=paeth(a,b,c); break; }
        }
        for(int x=0;x<W;x++){
            u32 col; int alpha=255;
            if(depth<8){
                int v=(cur[(x*depth)>>3]>>(8-depth-((x*depth)&7)))&((1<<depth)-1);
                if(ctype==3){ col=pal[v]; alpha=palpha[v]; }
                else { int g=v*255/((1<<depth)-1); col=RGB(g,g,g); if(v==trns_gray) alpha=0; }
            } else {
                const u8 *s=cur+x*bpp; int st=depth==16?2:1;
                switch(ctype){
                case 0: col=RGB(s[0],s[0],s[0]); if(trns_gray>=0 && s[0]==(trns_gray&255)) alpha=0; break;
                case 2: col=RGB(s[0],s[st],s[2*st]); break;
                case 3: col=pal[s[0]]; alpha=palpha[s[0]]; break;
                case 4: col=RGB(s[0],s[0],s[0]); alpha=s[st]; break;
                default: col=RGB(s[0],s[st],s[2*st]); alpha=s[3*st]; break;
                }
            }
            px[y*W+x] = alpha<128 ? IMG_CLEAR : col;
        }
        prev=cur;
    }
    kfree(raw);
    *ow=W; *oh=H; return px;
}

/* ======================= JPEG (baseline) ======================= */
static const short idct_t[64]={
#include "idct_table.h"
};
static const u8 zz[64]={0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
typedef struct { u8 len[17]; u8 val[256]; u16 code[256]; int n; int maxcode[18], valptr[17], mincode[17]; } jhuff_t;
typedef struct { const u8 *d; u32 n, p; u32 buf; int cnt; int marker; } jbits_t;
static void jh_build(jhuff_t *h){
    int k=0, code=0;
    for(int l=1;l<=16;l++){
        h->valptr[l]=k; h->mincode[l]=code;
        for(int i=0;i<h->len[l];i++){ h->code[k++]=code++; }
        h->maxcode[l]= h->len[l] ? code-1 : -1;
        code<<=1;
    }
    h->maxcode[17]=0x7FFFFFFF;
}
static int jbit(jbits_t *b){
    if(!b->cnt){
        if(b->p>=b->n){ b->buf=0; b->cnt=8; }
        else {
            u8 c=b->d[b->p++];
            if(c==0xFF){ u8 c2=b->p<b->n?b->d[b->p]:0; if(c2==0) b->p++; else { b->marker=c2; c=0; b->p--; } }
            b->buf=c; b->cnt=8;
        }
    }
    b->cnt--; return (b->buf>>b->cnt)&1;
}
static int jbits_n(jbits_t *b,int n){ int v=0; while(n--) v=(v<<1)|jbit(b); return v; }
static int jdecode(jbits_t *b,jhuff_t *h){
    int code=jbit(b);
    for(int l=1;l<=16;l++){
        if(h->len[l] && code<=h->maxcode[l] && code>=h->mincode[l]) return h->val[h->valptr[l]+code-h->mincode[l]];
        code=(code<<1)|jbit(b);
    }
    return 0;
}
static int extend(int v,int t){ return v < (1<<(t-1)) ? v - (1<<t) + 1 : v; }
static void idct8(const int *in,u8 *out,int stride){
    int tmp[64];
    for(int y=0;y<8;y++) for(int x=0;x<8;x++){
        s64 s=0; for(int u=0;u<8;u++) s+=(s64)in[y*8+u]*idct_t[u*8+x];
        tmp[y*8+x]=(int)(s>>12);
    }
    for(int x=0;x<8;x++) for(int y=0;y<8;y++){
        s64 s=0; for(int v=0;v<8;v++) s+=(s64)tmp[v*8+x]*idct_t[v*8+y];
        int val=(int)(s>>14)+128;
        out[y*stride+x]=(u8)clampi(val,0,255);
    }
}
static u32 *jpeg_decode(const u8 *d,u32 n,int *ow,int *oh){
    static u16 qt[4][64]; static jhuff_t hdc[4], hac[4];
    int W=0,H=0,nc=0, cid[3], hs[3], vs[3], tq[3], td[3], ta[3], restart=0;
    u32 p=2; u8 *plane[3]={0,0,0}; int pw[3]={0}, ph[3]={0};
    u32 *px=0;
    while(p+4<=n){
        if(d[p]!=0xFF){ p++; continue; }
        u8 m=d[p+1]; p+=2;
        if(m==0xD8||m==0x01||(m>=0xD0&&m<=0xD7)) continue;
        if(m==0xD9) break;
        u32 len=(d[p]<<8)|d[p+1]; const u8 *s=d+p+2; if(p+len>n) break;
        if(m==0xDB){
            u32 q=0; while(q+1<len-2){ int pq=s[q]>>4, id=s[q]&3; q++; for(int i=0;i<64;i++){ qt[id][zz[i]] = pq ? (s[q]<<8)|s[q+1] : s[q]; q+=pq?2:1; } }
        } else if(m==0xC4){
            u32 q=0; while(q<len-2){ int tc=s[q]>>4, id=s[q]&3; jhuff_t *h=tc?&hac[id]:&hdc[id]; q++; int tot=0; h->len[0]=0; for(int i=1;i<=16;i++){ h->len[i]=s[q+i-1]; tot+=h->len[i]; } q+=16; for(int i=0;i<tot && i<256;i++) h->val[i]=s[q+i]; q+=tot; jh_build(h); }
        } else if(m==0xC0||m==0xC1){
            H=(s[1]<<8)|s[2]; W=(s[3]<<8)|s[4]; nc=s[5]; if(nc!=1&&nc!=3) return 0;
            for(int i=0;i<nc;i++){ cid[i]=s[6+i*3]; hs[i]=s[7+i*3]>>4; vs[i]=s[7+i*3]&15; tq[i]=s[8+i*3]&3; if(hs[i]<1||hs[i]>2||vs[i]<1||vs[i]>2) return 0; }
        } else if(m==0xC2||m==0xC3||(m>=0xC5&&m<=0xCF&&m!=0xC8&&m!=0xCC)) return 0; /* progressive etc. */
        else if(m==0xDD) restart=(s[0]<<8)|s[1];
        else if(m==0xDA){
            if(!W||!H) return 0;
            int ns=s[0];
            for(int i=0;i<ns;i++){ int c=s[1+i*2]; for(int k=0;k<nc;k++) if(cid[k]==c){ td[k]=s[2+i*2]>>4; ta[k]=s[2+i*2]&15; } }
            int hmax=1,vmax=1; for(int i=0;i<nc;i++){ hmax=imax(hmax,hs[i]); vmax=imax(vmax,vs[i]); }
            int mcux=(W+8*hmax-1)/(8*hmax), mcuy=(H+8*vmax-1)/(8*vmax);
            if((u32)W*H>MAXPIX) return 0;
            for(int i=0;i<nc;i++){ pw[i]=mcux*hs[i]*8; ph[i]=mcuy*vs[i]*8; plane[i]=kmalloc(pw[i]*ph[i]); if(!plane[i]){ for(int k=0;k<nc;k++) kfree(plane[k]); return 0; } }
            jbits_t b; b.d=d; b.n=n; b.p=p+len; b.cnt=0; b.marker=0; b.buf=0;
            int dcpred[3]={0,0,0}; int mcus=0;
            int blk[64];
            for(int my=0;my<mcuy;my++) for(int mx=0;mx<mcux;mx++){
                if(restart && mcus && mcus%restart==0){
                    b.cnt=0; while(b.p+1<n && !(b.d[b.p]==0xFF && b.d[b.p+1]>=0xD0 && b.d[b.p+1]<=0xD7)) b.p++;
                    b.p+=2; b.marker=0; dcpred[0]=dcpred[1]=dcpred[2]=0;
                }
                mcus++;
                for(int c=0;c<nc;c++) for(int by=0;by<vs[c];by++) for(int bx=0;bx<hs[c];bx++){
                    memset(blk,0,sizeof blk);
                    int t=jdecode(&b,&hdc[td[c]]); int diff=t?extend(jbits_n(&b,t),t):0;
                    dcpred[c]+=diff; blk[0]=dcpred[c]*qt[tq[c]][0];
                    for(int k=1;k<64;){
                        int rs=jdecode(&b,&hac[ta[c]]); int r=rs>>4, sz=rs&15;
                        if(!sz){ if(r==15){ k+=16; continue; } break; }
                        k+=r; if(k>63) break;
                        blk[zz[k]]=extend(jbits_n(&b,sz),sz)*qt[tq[c]][zz[k]]; k++;
                    }
                    int ox=(mx*hs[c]+bx)*8, oy=(my*vs[c]+by)*8;
                    idct8(blk,plane[c]+oy*pw[c]+ox,pw[c]);
                }
            }
            px=alloc_px(W,H);
            if(px) for(int y=0;y<H;y++) for(int x=0;x<W;x++){
                int Y=plane[0][(y*vs[0]/vmax)*pw[0]+x*hs[0]/hmax];
                if(nc==1){ px[y*W+x]=RGB(Y,Y,Y); continue; }
                int Cb=plane[1][(y*vs[1]/vmax)*pw[1]+x*hs[1]/hmax]-128, Cr=plane[2][(y*vs[2]/vmax)*pw[2]+x*hs[2]/hmax]-128;
                int R=Y+((91881*Cr)>>16), G=Y-((22554*Cb+46802*Cr)>>16), B=Y+((116130*Cb)>>16);
                px[y*W+x]=RGB(clampi(R,0,255),clampi(G,0,255),clampi(B,0,255));
            }
            for(int i=0;i<nc;i++) kfree(plane[i]);
            break;
        }
        p+=len;
    }
    if(!px) return 0;
    *ow=W; *oh=H; return px;
}

/* ======================= BMP ======================= */
static u32 *bmp_decode(const u8 *d,u32 n,int *ow,int *oh){
    if(n<54) return 0;
    u32 off=d[10]|(d[11]<<8)|(d[12]<<16)|(d[13]<<24);
    int W=(int)(d[18]|(d[19]<<8)|(d[20]<<16)|(d[21]<<24)), H=(int)(d[22]|(d[23]<<8)|(d[24]<<16)|((u32)d[25]<<24));
    int bpp=d[28]|(d[29]<<8), comp=d[30];
    int flip=1; if(H<0){ H=-H; flip=0; }
    if(comp!=0 && comp!=3) return 0;
    if(bpp!=24 && bpp!=32 && bpp!=8) return 0;
    u32 hsz=d[14]|(d[15]<<8);
    u32 pal[256]; if(bpp==8) for(int i=0;i<256;i++){ u32 q=14+hsz+i*4; pal[i]= q+3<n ? RGB(d[q+2],d[q+1],d[q]) : 0; }
    u32 stride=((u32)W*bpp/8+3)&~3u;
    if(off+stride*H>n) return 0;
    u32 *px=alloc_px(W,H); if(!px) return 0;
    for(int y=0;y<H;y++){
        const u8 *row=d+off+(flip?(H-1-y):y)*stride;
        for(int x=0;x<W;x++){
            if(bpp==8) px[y*W+x]=pal[row[x]];
            else { const u8 *s=row+x*(bpp/8); px[y*W+x]=RGB(s[2],s[1],s[0]); }
        }
    }
    *ow=W; *oh=H; return px;
}

/* ======================= public ======================= */
u32 *image_decode(const u8 *d,u32 n,int *w,int *h){
    if(n>6 && !memcmp(d,"GIF8",4)) return gif_decode(d,n,w,h);
    if(n>8 && d[0]==0x89 && d[1]=='P' && d[2]=='N' && d[3]=='G') return png_decode(d,n,w,h);
    if(n>4 && d[0]==0xFF && d[1]==0xD8) return jpeg_decode(d,n,w,h);
    if(n>54 && d[0]=='B' && d[1]=='M') return bmp_decode(d,n,w,h);
    return 0;
}
u32 *image_scale(u32 *px,int w,int h,int nw,int nh){
    if(nw==w && nh==h) return px;
    u32 *o=kmalloc((u32)nw*nh*4); if(!o) return px;
    for(int y=0;y<nh;y++){ const u32 *src=px+(y*h/nh)*w; for(int x=0;x<nw;x++) o[y*nw+x]=src[x*w/nw]; }
    kfree(px); return o;
}
