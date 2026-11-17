/* CamadamOS graphics: double-buffered framebuffer drawing */
#include "kernel.h"

#define MAXW 2560
#define MAXH 1600
static u32 back[MAXW*MAXH];
static u32 shadow[MAXW*MAXH];
static u8 *fb; static u32 fb_pitch, fb_bpp; static u8 rp, gp, bp; static int std_layout;
int scr_w, scr_h, gfx_ox, gfx_oy;
static int cx0, cy0, cx1, cy1;
extern const u8 font_alpha[95][16][8];

void gfx_init(u32 addr, u32 pitch, u32 w, u32 h, u32 bpp, u8 r, u8 g, u8 b){
    fb=(u8*)addr; fb_pitch=pitch; fb_bpp=bpp; rp=r; gp=g; bp=b;
    std_layout = (bpp==32 && r==16 && g==8 && b==0);
    scr_w = w>MAXW?MAXW:(int)w; scr_h = h>MAXH?MAXH:(int)h;
    memset32(shadow, 0xFFFFFFFF, MAXW*MAXH); /* force first full write */
    gfx_reset_clip();
}
u32 *gfx_backbuffer(void){ return back; }

void gfx_present(void){
    for(int y=0;y<scr_h;y++){
        u32 *src=back+y*scr_w, *sh=shadow+y*scr_w;
        u8 *row=fb+y*fb_pitch;
        if(std_layout){
            u32 *dst=(u32*)row;
            for(int x=0;x<scr_w;x++) if(src[x]!=sh[x]){ sh[x]=src[x]; dst[x]=src[x]; }
        } else {
            for(int x=0;x<scr_w;x++) if(src[x]!=sh[x]){
                u32 c=src[x]; sh[x]=c;
                u32 v=(((c>>16)&255)<<rp)|(((c>>8)&255)<<gp)|((c&255)<<bp);
                if(fb_bpp==32) ((u32*)row)[x]=v;
                else { u8 *p=row+x*3; p[0]=v&255; p[1]=(v>>8)&255; p[2]=(v>>16)&255; }
            }
        }
    }
}

void gfx_reset_clip(void){ cx0=0; cy0=0; cx1=scr_w; cy1=scr_h; }
void gfx_set_clip(int x,int y,int w,int h){
    cx0=imax(0,x); cy0=imax(0,y); cx1=imin(scr_w,x+w); cy1=imin(scr_h,y+h);
    if(cx1<cx0) cx1=cx0;
    if(cy1<cy0) cy1=cy0;
}
void gfx_get_clip(int *a,int *b,int *c,int *d){ *a=cx0;*b=cy0;*c=cx1;*d=cy1; }
void gfx_set_clip_raw(int a,int b,int c,int d){ cx0=a;cy0=b;cx1=c;cy1=d; }

u32 mix(u32 a, u32 b, int t){
    int ra=(a>>16)&255, ga=(a>>8)&255, ba=a&255;
    int rb=(b>>16)&255, gb=(b>>8)&255, bb=b&255;
    return RGB(ra+((rb-ra)*t>>8), ga+((gb-ga)*t>>8), ba+((bb-ba)*t>>8));
}

void pset(int x,int y,u32 c){ x+=gfx_ox; y+=gfx_oy; if(x>=cx0&&y>=cy0&&x<cx1&&y<cy1) back[y*scr_w+x]=c; }
u32 pget(int x,int y){ x+=gfx_ox; y+=gfx_oy; if(x>=0&&y>=0&&x<scr_w&&y<scr_h) return back[y*scr_w+x]; return 0; }

void fill_rect(int x,int y,int w,int h,u32 c){
    x+=gfx_ox; y+=gfx_oy;
    int x0=imax(x,cx0), y0=imax(y,cy0), x1=imin(x+w,cx1), y1=imin(y+h,cy1);
    if(x1<=x0||y1<=y0) return;
    for(int j=y0;j<y1;j++) memset32(back+j*scr_w+x0, c, x1-x0);
}
void blend_rect(int x,int y,int w,int h,u32 c,int a){
    x+=gfx_ox; y+=gfx_oy;
    int x0=imax(x,cx0), y0=imax(y,cy0), x1=imin(x+w,cx1), y1=imin(y+h,cy1);
    for(int j=y0;j<y1;j++){ u32 *p=back+j*scr_w; for(int i=x0;i<x1;i++) p[i]=mix(p[i],c,a); }
}
void hline(int x,int y,int w,u32 c){ fill_rect(x,y,w,1,c); }
void vline(int x,int y,int h,u32 c){ fill_rect(x,y,1,h,c); }
void draw_rect(int x,int y,int w,int h,u32 c){ hline(x,y,w,c); hline(x,y+h-1,w,c); vline(x,y,h,c); vline(x+w-1,y,h,c); }

void draw_line(int x0,int y0,int x1,int y1,u32 c){
    int dx=iabs(x1-x0), sx=x0<x1?1:-1, dy=-iabs(y1-y0), sy=y0<y1?1:-1, e=dx+dy;
    for(;;){ pset(x0,y0,c); if(x0==x1&&y0==y1) break; int e2=2*e; if(e2>=dy){e+=dy;x0+=sx;} if(e2<=dx){e+=dx;y0+=sy;} }
}
static int isqrt(int v){ if(v<=0) return 0; int r=0; while((r+1)*(r+1)<=v) r++; return r; }
void fill_circle(int cx,int cy,int r,u32 c){ for(int dy=-r;dy<=r;dy++){ int dx=isqrt(r*r-dy*dy); hline(cx-dx,cy+dy,2*dx+1,c); } }
void draw_circle(int cx,int cy,int r,u32 c){
    int x=r,y=0,e=1-r;
    while(x>=y){
        pset(cx+x,cy+y,c);pset(cx+y,cy+x,c);pset(cx-y,cy+x,c);pset(cx-x,cy+y,c);
        pset(cx-x,cy-y,c);pset(cx-y,cy-x,c);pset(cx+y,cy-x,c);pset(cx+x,cy-y,c);
        y++; if(e<0) e+=2*y+1; else { x--; e+=2*(y-x)+1; }
    }
}
void fill_round_rect(int x,int y,int w,int h,int r,u32 c){
    if(r*2>h) r=h/2;
    if(r*2>w) r=w/2;
    for(int i=0;i<h;i++){
        int in=0;
        if(i<r) in=r-isqrt(r*r-(r-i)*(r-i));
        else if(i>=h-r) in=r-isqrt(r*r-(i-(h-r-1))*(i-(h-r-1)));
        hline(x+in,y+i,w-2*in,c);
    }
}
void gradient_v(int x,int y,int w,int h,u32 t,u32 b){
    for(int i=0;i<h;i++) hline(x,y+i,w,mix(t,b,h>1?i*255/(h-1):0));
}

void draw_char(int x,int y,char ch,u32 c){
    if(ch<32||ch>126) ch='?';
    const u8 (*g)[8]=font_alpha[ch-32];
    x+=gfx_ox; y+=gfx_oy;
    if(x>=cx1||y>=cy1||x+8<=cx0||y+16<=cy0) return;
    for(int j=0;j<16;j++){
        int py=y+j; if(py<cy0||py>=cy1) continue;
        u32 *row=back+py*scr_w;
        for(int i=0;i<8;i++){
            int a=g[j][i]; if(!a) continue;
            int px=x+i; if(px<cx0||px>=cx1) continue;
            row[px]= a>=250 ? c : mix(row[px],c,a);
        }
    }
}
void draw_text(int x,int y,const char *s,u32 c){ while(*s){ if(*s=='\n'){ y+=16; x-=0; } else draw_char(x,y,*s,c); x+=8; s++; } }
void draw_text_bold(int x,int y,const char *s,u32 c){ draw_text(x,y,s,c); draw_text(x+1,y,s,c); }
int text_width(const char *s){ return (int)strlen(s)*8; }
void draw_text_center(int x,int y,int w,const char *s,u32 c){ draw_text(x+(w-text_width(s))/2,y,s,c); }
void draw_text_big(int x,int y,const char *s,u32 c,int sc){
    for(;*s;s++,x+=8*sc){
        char ch=*s; if(ch<32||ch>126) continue;
        const u8 (*g)[8]=font_alpha[ch-32];
        for(int j=0;j<16;j++) for(int i=0;i<8;i++){ int a=g[j][i]; if(a>60) blend_rect(x+i*sc,y+j*sc,sc,sc,c,a>=250?256:a); }
    }
}
void blit(int x,int y,int w,int h,const u32 *src,int stride){
    x+=gfx_ox; y+=gfx_oy;
    int x0=imax(x,cx0), y0=imax(y,cy0), x1=imin(x+w,cx1), y1=imin(y+h,cy1);
    if(x1<=x0||y1<=y0) return;
    for(int j=y0;j<y1;j++) memcpy(back+j*scr_w+x0, src+(j-y)*stride+(x0-x), (x1-x0)*4);
}

static u32 pal(char c){
    switch(c){
    case 'k': return RGB(20,20,24);   case 'w': return RGB(255,255,255); case 'r': return RGB(220,40,50);
    case 'g': return RGB(60,190,80);  case 'G': return RGB(30,110,50);   case 'b': return RGB(50,100,220);
    case 'B': return RGB(20,50,140);  case 'l': return RGB(120,190,255); case 'y': return RGB(255,210,60);
    case 'o': return RGB(220,140,30); case 'n': return RGB(150,95,50);   case 's': return RGB(170,170,180);
    case 'd': return RGB(90,90,100);  case 'c': return RGB(40,200,210);  case 'p': return RGB(150,70,200);
    case 'm': return RGB(240,120,200);
    }
    return 0xFFFFFFFF;
}
void draw_pixmap(int x,int y,const char *const *rows,int sc){
    for(int j=0;rows[j];j++) for(int i=0;rows[j][i];i++){
        u32 c=pal(rows[j][i]); if(c!=0xFFFFFFFF) fill_rect(x+i*sc,y+j*sc,sc,sc,c);
    }
}
void draw_bevel(int x,int y,int w,int h,int pressed){
    fill_rect(x,y,w,h,pressed?RGB(205,208,218):RGB(238,240,245));
    draw_rect(x,y,w,h,RGB(160,165,180));
}

void blit_key(int x,int y,int w,int h,const u32 *src,int stride){
    x+=gfx_ox; y+=gfx_oy;
    int x0=imax(x,cx0), y0=imax(y,cy0), x1=imin(x+w,cx1), y1=imin(y+h,cy1);
    for(int j=y0;j<y1;j++){ const u32 *s=src+(j-y)*stride; u32 *d=back+j*scr_w; for(int i=x0;i<x1;i++){ u32 c=s[i-x]; if(c!=IMG_CLEAR) d[i]=c; } }
}
