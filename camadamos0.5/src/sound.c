/* CamadamOS sound: Intel High Definition Audio driver + software synthesizer.
 * Falls back to the PC speaker when no HD Audio controller works. */
#include "kernel.h"

soundinfo_t snd = { 0, "PC speaker", 80, INST_PIANO, 0, 0, 0 };
const char *const instrument_names[INST_COUNT]={"Piano","Organ","Saxophone","Chiptune","Bell"};
void udelay(u32 us){ while(us--) io_wait(); }

/* ======================= HD Audio controller ======================= */
static u32 hb;                       /* MMIO base */
static u32 corb[256] __attribute__((aligned(128)));
static u64 rirb[256] __attribute__((aligned(128)));
static u16 rirb_rp;
static int codec_addr;
#define RATE 48000
#define RING 16384                   /* frames in the DMA ring */
static s16 ring[RING*2] __attribute__((aligned(128)));
typedef struct { u32 lo, hi, len, ioc; } bdl_t;
static bdl_t bdl[4] __attribute__((aligned(128)));
static u32 sd;                       /* output stream descriptor base */
static u32 wpos;                     /* next frame to write */

static u32 verb(int nid,u32 v){
    u32 cmd=((u32)codec_addr<<28)|((u32)nid<<20)|v;
    u16 wp=(mmio_r16(hb+0x48)+1)&255;
    corb[wp]=cmd; mmio_w16(hb+0x48,wp);
    for(int t=0;t<20000;t++){
        u16 rp=mmio_r16(hb+0x58)&255;
        if(rp!=rirb_rp){ rirb_rp=(rirb_rp+1)&255; mmio_w8(hb+0x5D,0x05); return (u32)rirb[rirb_rp]; }
        udelay(1);
    }
    return 0xFFFFFFFF;
}
static u32 param(int nid,int p);
static u32 param(int nid,int p){ return verb(nid,0xF0000|p); }
static int conn_list(int nid,int *out,int max){
    u32 len=param(nid,0x0E)&0x7F; int n=0;
    if(len&0x80) return 0; /* long form not handled */
    for(u32 i=0;i<len && n<max;i+=4){ u32 r=verb(nid,0xF0200|i); for(int k=0;k<4 && i+k<len && n<max;k++) out[n++]=(r>>(8*k))&0xFF; }
    return n;
}
static int wtype(int nid){ return (param(nid,0x09)>>20)&15; }
static void unmute_out(int nid){
    u32 caps=param(nid,0x12); int off=caps&0x7F; if(!caps) off=0x7F;
    verb(nid,0x3B000|off);                       /* output amp, both channels, unmuted, 0 dB */
}
static void unmute_in(int nid,int idx){
    u32 caps=param(nid,0x0D); int off=caps&0x7F;
    verb(nid,0x37000|(idx<<8)|off);
}
static int dacs_used[8], ndacs;
/* depth-first search from a widget towards a DAC, configuring the path */
static int find_dac(int nid,int depth){
    int t=wtype(nid);
    if(t==0){ /* audio output converter */
        verb(nid,0x70500); unmute_out(nid);
        int seen=0; for(int i=0;i<ndacs;i++) if(dacs_used[i]==nid) seen=1;
        if(!seen && ndacs<8) dacs_used[ndacs++]=nid;
        return 1;
    }
    if(depth>5) return 0;
    int c[32]; int n=conn_list(nid,c,32);
    for(int i=0;i<n;i++){
        if(find_dac(c[i],depth+1)){
            verb(nid,0x70500);
            if(t==3||t==4) verb(nid,0x70100|i);       /* selector / pin: choose this input */
            if(t==2) unmute_in(nid,i);                 /* mixer: unmute this input */
            unmute_out(nid);
            return 1;
        }
    }
    return 0;
}
static char dbg[40];
static void hx(char *o,u32 v){ const char *h="0123456789ABCDEF"; for(int i=0;i<8;i++) o[i]=h[(v>>(28-4*i))&15]; o[8]=0; }
static int allow_fallback;
static int hda_codec_setup(void){
    u32 vid=param(0,0x00);
    hx(dbg,vid);
    if(vid==0xFFFFFFFF||!vid) return 0;
    snd.codec_vendor=vid>>16; snd.codec_device=vid&0xFFFF;
    u32 sub=param(0,0x04); int start=(sub>>16)&0xFF, cnt=sub&0xFF;
    int afg=-1;
    for(int i=0;i<cnt;i++) if((param(start+i,0x05)&0xFF)==1){ afg=start+i; break; }
    if(afg<0){ strncpy0(dbg,"no AFG",40); char t[10]; hx(t,sub); strcat0(dbg,t,40); return 0; }
    verb(afg,0x70500);                                  /* AFG power D0 */
    udelay(10000);
    for(int g=0;g<4;g++) verb(afg,0x71600|0); /* GPIO off by default (some laptops use them for amps) */
    u32 ws=param(afg,0x04); int wstart=(ws>>16)&0xFF, wcnt=ws&0xFF;
    ndacs=0; snd.pins=0;
    /* pass 1: speakers and headphones; pass 2: line outs if nothing found */
    for(int pass=0;pass<2;pass++){
        for(int i=0;i<wcnt;i++){
            int nid=wstart+i;
            if(wtype(nid)!=4) continue;
            u32 pc=param(nid,0x0C); if(!(pc&(1<<4))) continue;          /* output capable */
            if(pc&((1<<7)|(1<<24))) continue;                              /* HDMI / DisplayPort pin */
            u32 cfg=verb(nid,0xF1C00);
            int conn=cfg>>30, dev=(cfg>>20)&15;
            if(conn==1) continue;                                          /* nothing connected */
            int want = pass==0 ? (dev==1||dev==2) : (dev==0);
            if(!want) continue;
            if(find_dac(nid,0)){
                verb(nid,0x70700|0x40|(dev==2?0x80:0));                     /* pin out enable (+HP amp) */
                if(pc&(1<<16)) verb(nid,0x70C02);                            /* EAPD on: laptop speaker amp */
                unmute_out(nid);
                snd.pins++;
            }
        }
        if(snd.pins) break;
    }
    if(!ndacs && allow_fallback){ /* QEMU-style simple codecs: just use the first DAC */
        for(int i=0;i<wcnt;i++) if(wtype(wstart+i)==0){ dacs_used[ndacs++]=wstart+i; verb(wstart+i,0x70500); unmute_out(wstart+i); break; }
        for(int i=0;i<wcnt;i++) if(wtype(wstart+i)==4){ int nid=wstart+i; verb(nid,0x70740); unmute_out(nid); snd.pins++; }
    }
    if(!ndacs){ strncpy0(dbg,"no DAC ",40); char t[10]; hx(t,ws); strcat0(dbg,t,40); return 0; }
    for(int i=0;i<ndacs;i++){
        verb(dacs_used[i],0x20011);                     /* format: 48 kHz, 16-bit, stereo */
        verb(dacs_used[i],0x70610);                     /* stream 1, channel 0 */
    }
    return 1;
}
static const char *fail="";
static int hda_try(int n,int pass){
    int b,d,f;
    if(!pci_find(4,3,-1,n,&b,&d,&f)){ fail="no controller"; return 0; }
    pci_enable(b,d,f);
    u32 id=pci_read(b,d,f,0);
    if((id&0xFFFF)==0x8086){
        u32 v=pci_read(b,d,f,0x44); pci_write(b,d,f,0x44,v&~7u);          /* TCSEL = TC0 */
        v=pci_read(b,d,f,0x78); pci_write(b,d,f,0x78,v&~(1u<<11));         /* make sure DMA is snooped */
    }
    hb=pci_bar_mem(b,d,f,0); if(!hb){ fail="no BAR"; return 0; }
    /* controller reset */
    mmio_w32(hb+0x08,mmio_r32(hb+0x08)&~1u);
    for(int t=0;t<1000 && (mmio_r32(hb+0x08)&1);t++) udelay(100);
    udelay(1000);
    mmio_w32(hb+0x08,mmio_r32(hb+0x08)|1u);
    for(int t=0;t<1000 && !(mmio_r32(hb+0x08)&1);t++) udelay(100);
    if(!(mmio_r32(hb+0x08)&1)){ fail="reset"; return 0; }
    udelay(5000);
    u16 states=mmio_r16(hb+0x0E);
    if(!states){ fail="no codec"; return 0; }
    mmio_w32(hb+0x20,0);                                /* no interrupts */
    /* CORB */
    mmio_w8(hb+0x4C,0); mmio_w8(hb+0x5C,0); udelay(1000);
    mmio_w32(hb+0x40,(u32)corb); mmio_w32(hb+0x44,0);
    u8 cs=mmio_r8(hb+0x4E); mmio_w8(hb+0x4E,(cs&0x40)?2:(cs&0x20)?1:0);
    mmio_w16(hb+0x4A,0x8000); for(int t=0;t<1000 && !(mmio_r16(hb+0x4A)&0x8000);t++) udelay(1);
    mmio_w16(hb+0x4A,0); for(int t=0;t<1000 && (mmio_r16(hb+0x4A)&0x8000);t++) udelay(1);
    mmio_w16(hb+0x48,0);
    /* RIRB */
    mmio_w32(hb+0x50,(u32)rirb); mmio_w32(hb+0x54,0);
    u8 rs=mmio_r8(hb+0x5E); mmio_w8(hb+0x5E,(rs&0x40)?2:(rs&0x20)?1:0);
    mmio_w16(hb+0x58,0x8000); udelay(100);
    mmio_w16(hb+0x5A,0xFF);
    rirb_rp=0;
    mmio_w8(hb+0x4C,2); mmio_w8(hb+0x5C,2);             /* run CORB and RIRB DMA */
    udelay(1000);
    int found=0;
    allow_fallback = pass==1;
    for(int i=0;i<15 && !found;i++){
        if(!(states&(1<<i))) continue;
        codec_addr=i;
        u32 vid=param(0,0x00);
        if(pass==0 && (vid>>16)==0x8086) continue;          /* Intel codecs here are HDMI-only */
        if(hda_codec_setup()) found=1;
    }
    if(!found){ fail=dbg[0]?dbg:"no speaker output"; return 0; }
    /* output stream */
    u16 gcap=mmio_r16(hb+0x00); int iss=(gcap>>8)&15;
    sd=hb+0x80+iss*0x20;
    mmio_w8(sd,mmio_r8(sd)|1); for(int t=0;t<1000 && !(mmio_r8(sd)&1);t++) udelay(10);
    udelay(100);
    mmio_w8(sd,mmio_r8(sd)&~1); for(int t=0;t<1000 && (mmio_r8(sd)&1);t++) udelay(10);
    memset(ring,0,sizeof ring);
    for(int i=0;i<4;i++){ bdl[i].lo=(u32)ring+i*(sizeof ring/4); bdl[i].hi=0; bdl[i].len=sizeof ring/4; bdl[i].ioc=0; }
    mmio_w32(sd+0x18,(u32)bdl); mmio_w32(sd+0x1C,0);
    mmio_w32(sd+0x08,sizeof ring);
    mmio_w16(sd+0x0C,3);
    mmio_w16(sd+0x12,0x0011);
    mmio_w8(sd+0x02,(mmio_r8(sd+0x02)&0x0F)|(1<<4));    /* stream tag 1 */
    mmio_w8(sd+0x03,0x1C);                              /* clear status bits */
    mmio_w8(sd,mmio_r8(sd)|2);                          /* RUN */
    wpos=RING/8;
    return 1;
}

/* ======================= synthesizer ======================= */
static const s16 sine[1024]={
#include "sine_table.h"
};
#define NV 12
typedef struct { int on, inst, rel; u32 phase, inc; s32 env, peak; u32 hold; s32 decay, release; int vel; u32 age; } voice_t;
static voice_t vs[NV];
static volatile int mel_voice=-1;
static u32 note_age;
/* WAV playback state */
static const u8 *wav_data; static u32 wav_len, wav_pos16, wav_step; static int wav_ch, wav_bits; static volatile int wav_on;
static int level_peak;

static s32 osc(int inst,u32 ph){
    #define S(m) sine[((ph*(m))>>22)&1023]
    switch(inst){
    case INST_PIANO: return S(1u) + (S(2u)>>1) + (S(3u)>>3) + (S(4u)>>4);
    case INST_ORGAN: return (S(1u)>>1) + (S(2u)>>1) + (S(3u)>>2) + (S(4u)>>3) + (S(8u)>>4);
    case INST_SAX:   return S(1u) + (S(2u)*3>>2) + (S(3u)>>1) + (S(4u)>>2) + (S(5u)>>3) + (S(6u)>>4);
    case INST_CHIP:  return (ph<0x80000000u)?14000:-14000;
    case INST_BELL:  return S(1u) + (S(3u)>>2) + (S(5u)>>3) + (S(7u)>>4);
    }
    return S(1u);
    #undef S
}
static void voice_params(voice_t *v){
    switch(v->inst){
    case INST_PIANO: v->decay=60000; v->release=200; break;   /* larger = slower decay (samples per halving-ish) */
    case INST_ORGAN: v->decay=0;     v->release=600; break;
    case INST_SAX:   v->decay=0;     v->release=1500; break;
    case INST_CHIP:  v->decay=0;     v->release=100; break;
    case INST_BELL:  v->decay=120000;v->release=4000; break;
    }
}
static u32 freq_inc(u32 f){ return (u32)(((u64)f<<32)/RATE); }
static int alloc_voice(void){
    int best=0; u32 oldest=0xFFFFFFFF;
    for(int i=0;i<NV;i++){ if(!vs[i].on) return i; if(vs[i].age<oldest && i!=mel_voice){ oldest=vs[i].age; best=i; } }
    return best;
}
void snd_note(u32 f,int inst,u32 ms,int vel){
    if(!snd.hda){ if(f) speaker_tone(f); return; }
    cli();
    int i=alloc_voice(); voice_t *v=&vs[i];
    memset(v,0,sizeof *v); v->on=1; v->inst=inst; v->inc=freq_inc(f); v->vel=vel; v->age=++note_age;
    v->env=0; v->peak=vel<<8; v->hold=ms*RATE/1000; voice_params(v);
    sti();
}
void snd_tone_on(u32 f){
    if(!snd.hda){ speaker_tone(f); return; }
    cli();
    int i=alloc_voice(); voice_t *v=&vs[i];
    if(mel_voice>=0 && vs[mel_voice].on) vs[mel_voice].rel=1;
    memset(v,0,sizeof *v); v->on=1; v->inst=snd.instrument; v->inc=freq_inc(f); v->vel=110; v->age=++note_age;
    v->peak=110<<8; v->hold=0xFFFFFFFF; voice_params(v); mel_voice=i;
    sti();
}
void snd_tone_off(void){
    if(!snd.hda){ speaker_off(); return; }
    cli(); if(mel_voice>=0) vs[mel_voice].rel=1; mel_voice=-1; sti();
}
void snd_stop_all(void){ cli(); for(int i=0;i<NV;i++) vs[i].on=0; mel_voice=-1; wav_on=0; sti(); speaker_off(); }
int snd_level(void){ return level_peak; }

static void render(s16 *out,int frames){
    int peak=0;
    for(int n=0;n<frames;n++){
        s32 mix=0;
        for(int i=0;i<NV;i++){
            voice_t *v=&vs[i]; if(!v->on) continue;
            /* envelope: fast attack, optional decay, release when held time ends */
            if(v->rel){ v->env-= (v->env/ v->release)+1; if(v->env<=0){ v->on=0; continue; } }
            else {
                if(v->env<v->peak){ v->env+= v->inst==INST_SAX ? 24 : 160; if(v->env>v->peak) v->env=v->peak; }
                else if(v->decay){ v->env-= v->env/v->decay+0; }
                if(v->hold!=0xFFFFFFFF){ if(v->hold) v->hold--; else v->rel=1; }
            }
            s32 s=osc(v->inst,v->phase); v->phase+=v->inc;
            mix += (s*(v->env>>8))>>8;
        }
        if(wav_on){
            u32 idx=wav_pos16>>16; u32 bpf=wav_ch*(wav_bits/8);
            if(idx*bpf+bpf>wav_len) wav_on=0;
            else { const u8 *p=wav_data+idx*bpf; s32 sm = wav_bits==16 ? (s16)(p[0]|(p[1]<<8)) : ((s32)p[0]-128)<<8;
                if(wav_ch==2){ const u8 *q=p+wav_bits/8; sm=(sm+(wav_bits==16?(s16)(q[0]|(q[1]<<8)):((s32)q[0]-128)<<8))/2; }
                mix+=sm*2; wav_pos16+=wav_step; }
        }
        mix = mix*snd.volume/150;
        if(mix>32767) mix=32767; if(mix<-32768) mix=-32768;
        out[n*2]=(s16)mix; out[n*2+1]=(s16)mix;
        int a=mix<0?-mix:mix; if(a>peak) peak=a;
    }
    level_peak = (level_peak*7 + peak*100/32768)/8;
}
void sound_tick(void){
    if(!snd.hda) return;
    static u32 last; if(ticks-last<4) return; last=ticks;
    u32 play=(mmio_r32(sd+0x04)/4)%RING;
    u32 ahead=(wpos-play+RING)%RING;
    if(ahead>RING/2){ wpos=(play+RING/16)%RING; ahead=RING/16; }  /* we fell behind: jump ahead */
    int want=(int)(RING/8)-(int)ahead;                               /* keep ~43 ms queued */
    while(want>0){
        int chunk=imin(want,(int)(RING-wpos));
        render(ring+wpos*2,chunk);
        wpos=(wpos+chunk)%RING; want-=chunk;
    }
}

/* ======================= WAV files ======================= */
int snd_play_wav(const u8 *d,u32 n){
    if(n<44 || memcmp(d,"RIFF",4) || memcmp(d+8,"WAVE",4)) return 0;
    u32 p=12; int fmt=0, ch=0, bits=0; u32 rate=0; const u8 *data=0; u32 dlen=0;
    while(p+8<=n){
        u32 len=d[p+4]|(d[p+5]<<8)|(d[p+6]<<16)|((u32)d[p+7]<<24);
        if(!memcmp(d+p,"fmt ",4) && len>=16){ fmt=d[p+8]|(d[p+9]<<8); ch=d[p+10]; rate=d[p+12]|(d[p+13]<<8)|(d[p+14]<<16); bits=d[p+22]; }
        else if(!memcmp(d+p,"data",4)){ data=d+p+8; dlen=imin((int)len,(int)(n-p-8)); break; }
        p+=8+len+(len&1);
    }
    if(fmt!=1 || !data || (ch!=1&&ch!=2) || (bits!=8&&bits!=16) || !rate) return 0;
    if(!snd.hda) return 0;
    cli(); wav_data=data; wav_len=dlen; wav_ch=ch; wav_bits=bits; wav_pos16=0; wav_step=(u32)(((u64)rate<<16)/RATE); wav_on=1; sti();
    return 1;
}
void snd_stop_wav(void){ wav_on=0; }
int snd_wav_playing(void){ return wav_on; }

static int hda_init(void){
    for(int pass=0;pass<2;pass++)
        for(int n=0;n<4;n++){ int b,d,f; if(!pci_find(4,3,-1,n,&b,&d,&f)) break; if(hda_try(n,pass)) return 1; }
    return 0;
}
void sound_init(void){
    int ok=hda_init();
    if(!ok && fail[0]){ strncpy0(snd.codec,"PC speaker (HDA: ",sizeof snd.codec); strcat0(snd.codec,fail,sizeof snd.codec); strcat0(snd.codec,")",sizeof snd.codec); }
    if(ok){
        snd.hda=1;
        const char *vn = snd.codec_vendor==0x10EC?"Realtek": snd.codec_vendor==0x14F1?"Conexant": snd.codec_vendor==0x1106?"VIA":
                         snd.codec_vendor==0x111D||snd.codec_vendor==0x8384?"IDT": snd.codec_vendor==0x1AF4?"QEMU": snd.codec_vendor==0x8086?"Intel":"Unknown";
        strncpy0(snd.codec,"HD Audio (",sizeof snd.codec); strcat0(snd.codec,vn,sizeof snd.codec);
        const char *h="0123456789ABCDEF"; char hx[6]={' ',h[(snd.codec_device>>12)&15],h[(snd.codec_device>>8)&15],h[(snd.codec_device>>4)&15],h[snd.codec_device&15],0};
        strcat0(snd.codec,hx,sizeof snd.codec); strcat0(snd.codec,")",sizeof snd.codec);
    }
}
