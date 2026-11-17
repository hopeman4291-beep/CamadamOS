/* CamadamOS Paint */
#include "kernel.h"

enum { T_BRUSH, T_ERASE, T_FILL, T_LINE, T_RECT, T_OVAL, T_COUNT };
static const char *tool_names[T_COUNT]={"Brush","Erase","Fill","Line","Rect","Oval"};
static const u32 palette[16]={RGB(0,0,0),RGB(255,255,255),RGB(128,128,128),RGB(200,200,200),RGB(220,40,40),RGB(255,140,30),
    RGB(255,220,40),RGB(60,180,70),RGB(20,110,50),RGB(60,200,220),RGB(40,90,220),RGB(20,30,120),RGB(150,70,210),RGB(240,110,190),
    RGB(140,90,50),RGB(255,210,170)};
static const int sizes[4]={1,3,6,12};
#define TB_W 64
#define TOP_H 38
#define PAL_H 40
typedef struct {
    int cw, ch; u32 *px;
    int tool, size, color;
    int drawing, sx, sy, lx, ly, cx, cy;
    int file; char fname[FS_NAME];
} paint_t;

static void stamp(paint_t *p,int x,int y,int r,u32 c){
    if(r<=1){ if(x>=0&&y>=0&&x<p->cw&&y<p->ch) p->px[y*p->cw+x]=c; return; }
    int rr=r/2;
    for(int dy=-rr;dy<=rr;dy++) for(int dx=-rr;dx<=rr;dx++){
        if(dx*dx+dy*dy>rr*rr+rr) continue;
        int nx=x+dx, ny=y+dy; if(nx>=0&&ny>=0&&nx<p->cw&&ny<p->ch) p->px[ny*p->cw+nx]=c;
    }
}
static void cline(paint_t *p,int x0,int y0,int x1,int y1,int r,u32 c){
    int dx=iabs(x1-x0), sx=x0<x1?1:-1, dy=-iabs(y1-y0), sy=y0<y1?1:-1, e=dx+dy;
    for(;;){ stamp(p,x0,y0,r,c); if(x0==x1&&y0==y1) break; int e2=2*e; if(e2>=dy){e+=dy;x0+=sx;} if(e2<=dx){e+=dx;y0+=sy;} }
}
static void crect(paint_t *p,int x0,int y0,int x1,int y1,int r,u32 c){
    cline(p,x0,y0,x1,y0,r,c); cline(p,x1,y0,x1,y1,r,c); cline(p,x1,y1,x0,y1,r,c); cline(p,x0,y1,x0,y0,r,c);
}
static const s16 sin64[64]={0,98,195,290,383,471,556,634,707,773,831,882,924,957,981,995,1000,995,981,957,924,882,831,773,707,634,556,471,383,290,195,98,0,-98,-195,-290,-383,-471,-556,-634,-707,-773,-831,-882,-924,-957,-981,-995,-1000,-995,-981,-957,-924,-882,-831,-773,-707,-634,-556,-471,-383,-290,-195,-98};
static void coval(paint_t *p,int x0,int y0,int x1,int y1,int r,u32 c){
    int cx=(x0+x1)/2, cy=(y0+y1)/2, a=iabs(x1-x0)/2, b=iabs(y1-y0)/2;
    int px=cx+a, py=cy;
    for(int i=1;i<=64;i++){
        int x=cx+a*sin64[(i+16)%64]/1000, y=cy+b*sin64[i%64]/1000;
        cline(p,px,py,x,y,r,c); px=x; py=y;
    }
}
static void cfill(paint_t *p,int x,int y,u32 c){
    if(x<0||y<0||x>=p->cw||y>=p->ch) return;
    u32 target=p->px[y*p->cw+x]; if(target==c) return;
    u32 cap=p->cw*p->ch; u32 *st=kmalloc(cap*4); if(!st) return;
    u32 sp=0; st[sp++]=y*p->cw+x;
    while(sp){
        u32 v=st[--sp]; int sx=v%p->cw, sy=v/p->cw;
        if(p->px[v]!=target) continue;
        int l=sx, r=sx;
        while(l>0 && p->px[sy*p->cw+l-1]==target) l--;
        while(r<p->cw-1 && p->px[sy*p->cw+r+1]==target) r++;
        for(int i=l;i<=r;i++) p->px[sy*p->cw+i]=c;
        for(int d=-1;d<=1;d+=2){
            int ny=sy+d; if(ny<0||ny>=p->ch) continue;
            int in=0;
            for(int i=l;i<=r;i++){
                int m=p->px[ny*p->cw+i]==target;
                if(m && !in && sp<cap) st[sp++]=ny*p->cw+i;
                in=m;
            }
        }
    }
    kfree(st);
}

static void p_draw(window_t *w){
    paint_t *p=w->st; int cw=win_cw(w), ch=win_ch(w);
    fill_rect(0,0,cw,ch,RGB(215,218,228));
    ui_button(8,6,60,26,"New",0); ui_button(74,6,60,26,"Save",0);
    int tx=146; if(fat.mounted){ ui_button(140,6,72,26,"To USB",0); tx=222; }
    draw_text(tx,11,p->fname[0]?p->fname:"Untitled",RGB(60,60,80));
    for(int i=0;i<T_COUNT;i++) ui_button(6,TOP_H+i*32,TB_W-12,28,tool_names[i],p->tool==i);
    int sy=TOP_H+T_COUNT*32+8;
    draw_text(8,sy,"Size",RGB(70,70,90));
    for(int i=0;i<4;i++){
        int bx=6+(i%2)*26, by=sy+20+(i/2)*26;
        fill_round_rect(bx,by,24,24,4,p->size==i?RGB(200,190,235):RGB(240,240,246));
        draw_rect(bx,by,24,24,p->size==i?ui_accent:RGB(170,170,190));
        int r=imax(1,sizes[i]/2+ (i==0?0:0)); fill_circle(bx+12,by+12,i==0?1:imin(r,10),RGB(20,20,30));
    }
    /* canvas */
    int ox=TB_W, oy=TOP_H;
    blend_rect(ox+3,oy+3,p->cw,p->ch,0,80);
    blit(ox,oy,p->cw,p->ch,p->px,p->cw);
    if(p->drawing && (p->tool==T_LINE||p->tool==T_RECT||p->tool==T_OVAL)){
        int x0,y0,x1,y1; gfx_get_clip(&x0,&y0,&x1,&y1);
        gfx_set_clip(gfx_ox+ox,gfx_oy+oy,p->cw,p->ch);
        int a=ox+p->sx, b=oy+p->sy, c=ox+p->cx, d=oy+p->cy; u32 col=palette[p->color];
        if(p->tool==T_LINE) draw_line(a,b,c,d,col);
        else if(p->tool==T_RECT) draw_rect(imin(a,c),imin(b,d),iabs(c-a)+1,iabs(d-b)+1,col);
        else { int cx=(a+c)/2, cy=(b+d)/2, rx=iabs(c-a)/2, ry=iabs(d-b)/2, qx=cx+rx, qy=cy;
               for(int i=1;i<=64;i++){ int nx=cx+rx*sin64[(i+16)%64]/1000, ny=cy+ry*sin64[i%64]/1000; draw_line(qx,qy,nx,ny,col); qx=nx; qy=ny; } }
        gfx_set_clip_raw(x0,y0,x1,y1);
    }
    /* palette */
    int py=ch-PAL_H+4;
    fill_rect(8,py,30,30,palette[p->color]); draw_rect(8,py,30,30,RGB(60,60,70));
    for(int i=0;i<16;i++){ int x=50+i*28; fill_rect(x,py+3,24,24,palette[i]); draw_rect(x,py+3,24,24,i==p->color?RGB(110,60,200):RGB(120,120,130)); if(i==p->color) draw_rect(x-2,py+1,28,28,RGB(110,60,200)); }
}
static void save(window_t *w, paint_t *p){
    int pics=fs_lookup("/Pictures"); if(pics<0) pics=fs_create(0,"Pictures",FT_DIR);
    int n=p->file;
    if(n<0 || !fs_nodes[n].used || fs_nodes[n].type!=FT_IMAGE){
        char name[FS_NAME]; fs_unique_name(pics,"Drawing",".img",name);
        n=fs_create(pics,name,FT_IMAGE); if(n<0){ wm_message("Paint","Could not save: the disk is full."); return; }
        p->file=n; strncpy0(p->fname,name,FS_NAME);
    }
    u32 bytes=p->cw*p->ch*4; char *d=kmalloc(bytes); if(!d){ wm_message("Paint","Not enough memory to save."); return; }
    memcpy(d,p->px,bytes);
    if(!fs_nodes[n].readonly) kfree(fs_nodes[n].data); fs_nodes[n].readonly=0;
    fs_nodes[n].data=d; fs_nodes[n].size=bytes; fs_nodes[n].img_w=p->cw; fs_nodes[n].img_h=p->ch;
    fs_touch(); int on_disk = store.disk>=0 && store_save();
    char path[96], msg[160]; fs_path(n,path,sizeof path);
    strncpy0(msg,"Your picture was saved as\n",sizeof msg); strcat0(msg,path,sizeof msg);
    strcat0(msg, on_disk ? "\n\nIt is stored on the hard disk." : store.disk>=0 ? "\n\nWARNING: writing it to the hard disk failed." : "\n\nYou are running from USB, so it stays in memory\nuntil you restart. Use \"To USB\" to keep a copy.",sizeof msg);
    char t[64]; strncpy0(t,"Paint - ",sizeof t); strcat0(t,p->fname,sizeof t); wm_set_title(w,t);
    wm_message("Saved",msg);
}
static void to_usb(paint_t *p){
    u32 len; u8 *b=bmp_encode(p->px,p->cw,p->ch,&len); if(!b){ wm_message("Paint","Not enough memory."); return; }
    char name[64];
    if(p->fname[0]){ strncpy0(name,p->fname,sizeof name); int l=strlen(name); if(l>4 && !strcmp(name+l-4,".img")) name[l-4]=0; }
    else strncpy0(name,"Drawing",sizeof name);
    strcat0(name,".bmp",sizeof name);
    char path[80]="/"; strcat0(path,name,sizeof path);
    wm_toast("Saving to USB stick...");
    int ok=fat_write(path,b,len); kfree(b);
    char t[96]; strncpy0(t,ok?"Saved on the USB stick: ":"Saving to the USB stick failed: ",sizeof t); strcat0(t,name,sizeof t); wm_toast(t);
}
static void p_mouse(window_t *w,int ev,int x,int y,int b){
    paint_t *p=w->st; int ch=win_ch(w);
    int cx=x-TB_W, cy=y-TOP_H;
    u32 col = palette[p->color];
    if(ev==EV_DOWN){
        if(hit(x,y,8,6,60,26)){ for(u32 i=0;i<(u32)(p->cw*p->ch);i++) p->px[i]=0xFFFFFF; p->file=-1; p->fname[0]=0; wm_set_title(w,"Paint"); return; }
        if(hit(x,y,74,6,60,26)){ save(w,p); return; }
        if(fat.mounted && hit(x,y,140,6,72,26)){ to_usb(p); return; }
        for(int i=0;i<T_COUNT;i++) if(hit(x,y,6,TOP_H+i*32,TB_W-12,28)){ p->tool=i; return; }
        int sy=TOP_H+T_COUNT*32+8;
        for(int i=0;i<4;i++) if(hit(x,y,6+(i%2)*26,sy+20+(i/2)*26,24,24)){ p->size=i; return; }
        int py=ch-PAL_H+4;
        for(int i=0;i<16;i++) if(hit(x,y,50+i*28,py+3,24,24)){ p->color=i; return; }
        if(cx<0||cy<0||cx>=p->cw||cy>=p->ch) return;
        if(b==2){ p->color = 0; for(int i=0;i<16;i++) if(palette[i]==p->px[cy*p->cw+cx]) p->color=i; return; } /* right click picks a colour */
        if(p->tool==T_FILL){ cfill(p,cx,cy,col); return; }
        p->drawing=1; p->sx=p->lx=p->cx=cx; p->sy=p->ly=p->cy=cy;
        if(p->tool==T_BRUSH) stamp(p,cx,cy,sizes[p->size],col);
        if(p->tool==T_ERASE) stamp(p,cx,cy,sizes[p->size]*2+4,0xFFFFFF);
    } else if(ev==EV_MOVE && p->drawing){
        cx=clampi(cx,-20,p->cw+20); cy=clampi(cy,-20,p->ch+20);
        if(p->tool==T_BRUSH) cline(p,p->lx,p->ly,cx,cy,sizes[p->size],col);
        if(p->tool==T_ERASE) cline(p,p->lx,p->ly,cx,cy,sizes[p->size]*2+4,0xFFFFFF);
        p->lx=cx; p->ly=cy; p->cx=cx; p->cy=cy;
    } else if(ev==EV_UP && p->drawing){
        p->drawing=0;
        int r=sizes[p->size];
        if(p->tool==T_LINE) cline(p,p->sx,p->sy,p->cx,p->cy,r,col);
        if(p->tool==T_RECT) crect(p,p->sx,p->sy,p->cx,p->cy,r,col);
        if(p->tool==T_OVAL) coval(p,p->sx,p->sy,p->cx,p->cy,r,col);
    }
}
static void p_key(window_t *w,int k){ paint_t *p=w->st; if(k>='1'&&k<='6') p->tool=k-'1'; if(k=='s'||k=='S') save(w,p); }
static void p_close(window_t *w){ paint_t *p=w->st; kfree(p->px); }
void app_paint(int file){
    int iw=560, ih=360;
    if(file>=0 && fs_nodes[file].type==FT_IMAGE){ iw=imin(fs_nodes[file].img_w, scr_w-TB_W-40); ih=imin(fs_nodes[file].img_h, scr_h-200); }
    iw=imax(iw,480); ih=imax(ih,360);
    window_t *w=wm_open("Paint",TB_W+iw+8,TOP_H+ih+PAL_H+6,IC_PAINT); if(!w) return;
    paint_t *p=kzalloc(sizeof *p);
    p->cw=iw; p->ch=ih; p->px=kmalloc(iw*ih*4); p->file=-1; p->tool=T_BRUSH; p->size=1;
    if(!p->px){ kfree(p); wm_close(w); wm_message("Paint","Not enough memory."); return; }
    for(int i=0;i<iw*ih;i++) p->px[i]=0xFFFFFF;
    if(file>=0 && fs_nodes[file].type==FT_IMAGE && fs_nodes[file].data){
        fsnode_t *n=&fs_nodes[file]; u32 *src=(u32*)n->data;
        for(int y=0;y<imin(ih,n->img_h);y++) for(int x=0;x<imin(iw,n->img_w);x++){ u32 c=src[y*n->img_w+x]; p->px[y*iw+x]= c==IMG_CLEAR?0xFFFFFF:c; }
        p->file=file; strncpy0(p->fname,n->name,FS_NAME);
        char t[64]; strncpy0(t,"Paint - ",sizeof t); strcat0(t,n->name,sizeof t); wm_set_title(w,t);
    }
    w->st=p; w->draw=p_draw; w->mouse=p_mouse; w->key=p_key; w->close=p_close;
}
