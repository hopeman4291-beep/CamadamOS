/* CamadamOS Files (file browser) */
#include "kernel.h"

#define SIDE_W 130
#define BAR_H 42
#define ITEM_W 100
#define ITEM_H 88
#define MAXU 256
typedef struct { int dir, sel, scroll; int usb; char upath[200]; fat_ent_t *ents; int nents, usel, gen; } files_t;

static int type_icon(int t){
    switch(t){ case FT_DIR: return IC_FOLDER; case FT_TEXT: return IC_TEXT; case FT_PAGE: return IC_PAGE; case FT_SONG: return IC_SONG; case FT_IMAGE: return IC_IMAGE; }
    return IC_TEXT;
}
void open_file(int n){
    if(n<0 || !fs_nodes[n].used) return;
    switch(fs_nodes[n].type){
    case FT_DIR: app_files(n); break;
    case FT_TEXT: if(ends_with(fs_nodes[n].name,".bas")||ends_with(fs_nodes[n].name,".BAS")){ char pth[80]; fs_path(n,pth,sizeof pth); app_basic(pth); break; } app_notepad(n); break;
    case FT_PAGE: { char u[96], p[80]; fs_path(n,p,sizeof p); strncpy0(u,"file://",sizeof u); strcat0(u,p,sizeof u); app_web(u); break; }
    case FT_SONG: app_music(n); break;
    case FT_IMAGE: app_paint(n); break;
    case FT_BIN: if(ends_with(fs_nodes[n].name,".wav")||ends_with(fs_nodes[n].name,".WAV")){ app_music(n); break; } wm_message("Files","CamadamOS doesn't have a program\nthat can open this kind of file."); break;
    }
}
static const char *places[]={"Home","Documents","Programs","Music","Pages","Pictures","Downloads"};
static int place_node(int i){ if(i==0) return 0; return fs_find(0,places[i]); }

static int ext_is(const char *n,const char *e){ int ln=strlen(n), le=strlen(e); if(ln<le) return 0; for(int i=0;i<le;i++){ int a=n[ln-le+i]; if(a>='A'&&a<='Z') a+=32; if(a!=e[i]) return 0; } return 1; }
static int ent_icon(fat_ent_t *e){
    if(e->dir) return IC_FOLDER;
    if(ext_is(e->name,".bas")) return IC_BASIC;
    if(ext_is(e->name,".htm")||ext_is(e->name,".html")) return IC_PAGE;
    if(ext_is(e->name,".bmp")||ext_is(e->name,".png")||ext_is(e->name,".jpg")||ext_is(e->name,".jpeg")||ext_is(e->name,".gif")) return IC_IMAGE;
    if(ext_is(e->name,".wav")||ext_is(e->name,".mp3")) return IC_SONG;
    return IC_TEXT;
}
static void usb_reload(files_t *f){
    if(!f->ents) f->ents=kmalloc(MAXU*sizeof(fat_ent_t));
    f->nents=fat_list(f->upath,f->ents,MAXU);
    if(f->nents<0){ strncpy0(f->upath,"/",sizeof f->upath); f->nents=fat_list("/",f->ents,MAXU); if(f->nents<0) f->nents=0; }
    f->usel=-1; f->gen=usbstick.changed;
}
static void usb_child(files_t *f,const char *name,char *out,int n){
    strncpy0(out,f->upath,n); if(out[strlen(out)-1]!='/') strcat0(out,"/",n); strcat0(out,name,n);
}
/* write a FT_IMAGE as a 24-bit BMP */
static u8 *make_bmp(fsnode_t *nd,u32 *len){ return bmp_encode((u32*)nd->data,nd->img_w,nd->img_h,len); }
u8 *bmp_encode(const u32 *px,int w,int h,u32 *len){
    int row=(w*3+3)&~3; u32 sz=54+row*h;
    u8 *b=kzalloc(sz); if(!b) return 0;
    b[0]='B'; b[1]='M'; memcpy(b+2,&sz,4); u32 v=54; memcpy(b+10,&v,4); v=40; memcpy(b+14,&v,4);
    memcpy(b+18,&w,4); memcpy(b+22,&h,4); b[26]=1; b[28]=24; v=row*h; memcpy(b+34,&v,4);
    for(int y=0;y<h;y++){ u8 *r=b+54+(h-1-y)*row; for(int x=0;x<w;x++){ u32 c=px[y*w+x]; if((c&0xFF000000)==0xFF000000) c=0xFFFFFF; r[x*3]=c; r[x*3+1]=c>>8; r[x*3+2]=c>>16; } }
    *len=sz; return b;
}
static void copy_to_usb(files_t *f,int n){
    if(!fat.mounted){ wm_message("Copy to USB","No USB stick is connected."); return; }
    if(n<=0 || !fs_nodes[n].used){ wm_message("Copy to USB","Select a file first."); return; }
    fsnode_t *nd=&fs_nodes[n]; char name[80]; strncpy0(name,nd->name,sizeof name);
    u8 *data=0; u32 len=0; int own=0;
    if(nd->type==FT_DIR||nd->type==FT_SONG){ wm_message("Copy to USB","Folders and built-in songs can't be copied yet.\nCopy the files inside the folder one by one."); return; }
    if(nd->type==FT_IMAGE){
        data=make_bmp(nd,&len); own=1;
        if(ext_is(name,".img")) name[strlen(name)-4]=0;
        if(!ext_is(name,".bmp")) strcat0(name,".bmp",sizeof name);
    } else { data=(u8*)nd->data; len=nd->size; if(!data) len=0; }
    char dst[300]; strncpy0(dst,f->usb?f->upath:"/",sizeof dst); if(dst[strlen(dst)-1]!='/') strcat0(dst,"/",sizeof dst); strcat0(dst,name,sizeof dst);
    wm_toast("Copying to USB stick...");
    int ok=fat_write(dst,data?data:(u8*)"",len);
    if(own) kfree(data);
    char t[96]; strncpy0(t,ok?"Copied to USB: ":"Copy failed: ",sizeof t); strcat0(t,name,sizeof t); wm_toast(t);
    if(f->usb) usb_reload(f);
}
/* copy a file from the stick into /Downloads; returns the new node or -1 */
static int copy_from_usb(files_t *f,fat_ent_t *e){
    char src[300]; usb_child(f,e->name,src,sizeof src);
    if(e->size>24*1024*1024){ wm_message("Copy to computer","That file is too big (max 24 MB)."); return -1; }
    u8 *data; u32 len;
    if(!fat_read(src,&data,&len)){ wm_message("Copy to computer","Couldn't read that file from the stick."); return -1; }
    int dl=fs_lookup("/Downloads"); if(dl<0) dl=fs_create(0,"Downloads",FT_DIR);
    /* fit the name into FS_NAME keeping the extension */
    char name[FS_NAME], ext[12]=""; const char *dot=0; for(const char *q=e->name;*q;q++) if(*q=='.') dot=q;
    if(dot && strlen(dot)<10) strncpy0(ext,dot,sizeof ext);
    int isimg = ext_is(e->name,".bmp")||ext_is(e->name,".png")||ext_is(e->name,".jpg")||ext_is(e->name,".jpeg")||ext_is(e->name,".gif");
    if(isimg) strncpy0(ext,".img",sizeof ext);
    char base[FS_NAME]; int bl=dot ? (int)(dot-e->name) : (int)strlen(e->name); if(bl>FS_NAME-1-(int)strlen(ext)-4) bl=FS_NAME-1-strlen(ext)-4; if(bl<1) bl=1;
    memcpy(base,e->name,bl); base[bl]=0;
    strncpy0(name,base,sizeof name); strcat0(name,ext,sizeof name);
    if(fs_find(dl,name)>=0) fs_unique_name(dl,base,ext,name);
    int n=-1;
    if(isimg){
        int iw,ih; u32 *px=image_decode(data,len,&iw,&ih); kfree(data);
        if(!px){ wm_message("Copy to computer","CamadamOS couldn't decode that picture."); return -1; }
        n=fs_create(dl,name,FT_IMAGE); if(n<0){ kfree(px); return -1; }
        fs_nodes[n].data=(char*)px; fs_nodes[n].img_w=iw; fs_nodes[n].img_h=ih; fs_nodes[n].size=iw*ih*4; fs_touch();
    } else {
        int type = (ext_is(e->name,".htm")||ext_is(e->name,".html")) ? FT_PAGE :
                   (ext_is(e->name,".txt")||ext_is(e->name,".bas")||ext_is(e->name,".md")||ext_is(e->name,".csv")||ext_is(e->name,".ini")||ext_is(e->name,".log")||ext_is(e->name,".c")||ext_is(e->name,".h")||ext_is(e->name,".py")) ? FT_TEXT : FT_BIN;
        n=fs_create(dl,name,type); if(n<0){ kfree(data); wm_message("Copy to computer","No room for more files."); return -1; }
        fs_nodes[n].data=(char*)data; fs_nodes[n].size=len; fs_touch();
    }
    char t[80]="Copied to Downloads: "; strcat0(t,name,sizeof t); wm_toast(t);
    return n;
}
static int cols(window_t *w){ return imax(1,(win_cw(w)-SIDE_W-12)/ITEM_W); }
static void f_title(window_t *w, files_t *f){
    char p[80], t[48]; if(f->usb){ strncpy0(p,"USB stick ",sizeof p); strcat0(p,f->upath,sizeof p); } else fs_path(f->dir,p,sizeof p);
    strncpy0(t,"Files - ",sizeof t); strcat0(t,p,sizeof t); wm_set_title(w,t);
}
static void f_draw(window_t *w){
    files_t *f=w->st; int cw=win_cw(w), ch=win_ch(w);
    if(!fs_nodes[f->dir].used) f->dir=0;
    if(f->usb && !fat.mounted){ f->usb=0; f->dir=0; f->sel=-1; f_title(w,f); }
    if(f->usb && f->gen!=usbstick.changed) usb_reload(f);
    int extra = (f->usb||fat.mounted) ? 130 : 0;
    fill_rect(0,0,cw,ch,RGB(255,255,255));
    /* toolbar */
    fill_rect(0,0,cw,BAR_H,RGB(238,236,246)); hline(0,BAR_H-1,cw,RGB(210,206,224));
    ui_button(8,8,44,26,"Up",0);
    fill_round_rect(60,8,cw-60-244-extra,26,6,RGB(255,255,255)); draw_rect(60,8,cw-60-244-extra,26,RGB(200,196,214));
    char path[220]; if(f->usb){ strncpy0(path,"USB:",sizeof path); strcat0(path,f->upath,sizeof path); } else fs_path(f->dir,path,sizeof path);
    gfx_set_clip(gfx_ox+64,gfx_oy+8,imax(0,cw-60-252-extra),26); draw_text(68,13,path,RGB(40,40,60)); gfx_set_clip(gfx_ox,gfx_oy,cw,ch);
    if(extra) ui_button(cw-176-130,8,124,26,f->usb?"Copy to PC":"Copy to USB",0);
    ui_button(cw-176,8,96,26,"New folder",0);
    ui_button(cw-74,8,66,26,"Delete",0);
    /* sidebar */
    fill_rect(0,BAR_H,SIDE_W,ch-BAR_H,RGB(246,245,251)); vline(SIDE_W-1,BAR_H,ch-BAR_H,RGB(220,216,232));
    draw_text(12,BAR_H+10,"Places",RGB(130,120,160));
    if(usbstick.present){
        int y=BAR_H+32+7*30;
        if(f->usb) fill_round_rect(6,y-4,SIDE_W-12,26,6,RGB(225,215,250));
        draw_icon(IC_USB,12,y-1,1,0); draw_text(34,y,"USB stick",fat.mounted?RGB(40,40,60):RGB(160,150,160));
    }
    for(int i=0;i<7;i++){
        int n=place_node(i), y=BAR_H+32+i*30;
        if(n==f->dir && !f->usb) fill_round_rect(6,y-4,SIDE_W-12,26,6,RGB(225,215,250));
        { static const int pi[7]={IC_FILES,IC_FOLDER,IC_BASIC,IC_SONG,IC_PAGE,IC_IMAGE,IC_BROWSER}; draw_icon(pi[i],12,y-1,1,0); }
        draw_text(34,y,places[i],RGB(40,40,60));
    }
    /* items */
    int kids[FS_MAX]; int n=f->usb ? 0 : fs_children(f->dir,kids,FS_MAX);
    int c=cols(w), x0=SIDE_W+8, y0=BAR_H+8;
    gfx_set_clip(gfx_ox+SIDE_W,gfx_oy+BAR_H,cw-SIDE_W,ch-BAR_H-24);
    if(f->usb){
        for(int i=0;i<f->nents;i++){
            int x=x0+(i%c)*ITEM_W, y=y0+(i/c)*ITEM_H-f->scroll;
            fat_ent_t *e=&f->ents[i];
            if(i==f->usel) fill_round_rect(x+2,y,ITEM_W-4,ITEM_H-4,8,RGB(225,215,250));
            draw_icon(ent_icon(e),x+ITEM_W/2-16,y+8,2,0);
            int L=strlen(e->name); char nm[24]; strncpy0(nm,e->name,13); if(L>12){ nm[10]='.'; nm[11]='.'; nm[12]=0; }
            draw_text_center(x,y+48,ITEM_W,nm,RGB(30,30,45));
            if(L>12){ char n2[16]; strncpy0(n2,e->name+10,13); if(L>22){ n2[10]='.';n2[11]='.';n2[12]=0; } draw_text_center(x,y+64,ITEM_W,n2,RGB(30,30,45)); }
        }
        if(!f->nents) draw_text_center(SIDE_W,BAR_H+60,cw-SIDE_W,"This folder is empty",RGB(150,150,165));
        gfx_set_clip(gfx_ox,gfx_oy,cw,ch);
        fill_rect(SIDE_W,ch-24,cw-SIDE_W,24,RGB(246,245,251)); hline(SIDE_W,ch-24,cw-SIDE_W,RGB(220,216,232));
        char st[100]; itoa10(f->nents,st); strcat0(st,f->nents==1?" item":" items",sizeof st);
        if(f->usel>=0){ fat_ent_t *e=&f->ents[f->usel]; strcat0(st,"  |  ",sizeof st); strcat0(st,e->name,sizeof st);
            if(!e->dir){ char k[16]; itoa10((int)((e->size+1023)/1024),k); strcat0(st,"  ",sizeof st); strcat0(st,k,sizeof st); strcat0(st," KB",sizeof st); } }
        draw_text(SIDE_W+10,ch-20,st,RGB(100,100,120));
        char sv[48]; strncpy0(sv,fat.label,sizeof sv); strcat0(sv," - ",sizeof sv); strcat0(sv,fat.type==32?"FAT32 ":"FAT16 ",sizeof sv);
        char k[16]; if(fat.total_mb>=1024){ itoa10(fat.total_mb/1024,k); strcat0(k," GB",sizeof k); } else { itoa10(fat.total_mb,k); strcat0(k," MB",sizeof k); } strcat0(sv,k,sizeof sv);
        fill_circle(cw-text_width(sv)-22,ch-12,4,RGB(60,140,230));
        draw_text(cw-text_width(sv)-10,ch-20,sv,RGB(100,100,120));
        return;
    }
    for(int i=0;i<n;i++){
        int x=x0+(i%c)*ITEM_W, y=y0+(i/c)*ITEM_H-f->scroll;
        fsnode_t *nd=&fs_nodes[kids[i]];
        if(kids[i]==f->sel) fill_round_rect(x+2,y,ITEM_W-4,ITEM_H-4,8,RGB(225,215,250));
        draw_icon(type_icon(nd->type),x+ITEM_W/2-16,y+8,2,0);
        char nm[24]; strncpy0(nm,nd->name,13); if(strlen(nd->name)>12){ nm[10]='.'; nm[11]='.'; nm[12]=0; }
        draw_text_center(x,y+48,ITEM_W,nm,RGB(30,30,45));
        if(strlen(nd->name)>12){ char n2[16]; strncpy0(n2,nd->name+10,13); if(strlen(nd->name)>22){ n2[10]='.';n2[11]='.';n2[12]=0; } draw_text_center(x,y+64,ITEM_W,n2,RGB(30,30,45)); }
    }
    if(!n) draw_text_center(SIDE_W,BAR_H+60,cw-SIDE_W,"This folder is empty",RGB(150,150,165));
    gfx_set_clip(gfx_ox,gfx_oy,cw,ch);
    /* status bar */
    fill_rect(SIDE_W,ch-24,cw-SIDE_W,24,RGB(246,245,251)); hline(SIDE_W,ch-24,cw-SIDE_W,RGB(220,216,232));
    char st[64]; itoa10(n,st); strcat0(st,n==1?" item":" items",sizeof st);
    if(f->sel>0 && fs_nodes[f->sel].used){
        strcat0(st,"  |  ",sizeof st); strcat0(st,fs_nodes[f->sel].name,sizeof st);
        if(fs_nodes[f->sel].type!=FT_DIR){ char k[16]; u32 sz=fs_nodes[f->sel].size; if(fs_nodes[f->sel].type==FT_SONG) sz=strlen(songs[fs_nodes[f->sel].song].notes);
            itoa10((int)((sz+1023)/1024),k); strcat0(st,"  ",sizeof st); strcat0(st,k,sizeof st); strcat0(st," KB",sizeof st); }
    }
    draw_text(SIDE_W+10,ch-20,st,RGB(100,100,120));
    const char *sv = store.disk>=0 ? "Saved on disk" : "Not saved (USB)";
    fill_circle(cw-text_width(sv)-22,ch-12,4,store.disk>=0?RGB(60,180,90):RGB(230,160,40));
    draw_text(cw-text_width(sv)-10,ch-20,sv,RGB(100,100,120));
}
static void usb_up(window_t *w,files_t *f){
    int l=strlen(f->upath); if(l<=1) return;
    while(l>1 && f->upath[l-1]!='/') l--; if(l>1) l--; f->upath[l]=0; if(!f->upath[0]) strncpy0(f->upath,"/",sizeof f->upath);
    f->scroll=0; usb_reload(f); f_title(w,f);
}
static void usb_delete(files_t *f){
    if(f->usel<0){ wm_message("Delete","Select something on the stick first."); return; }
    char p[300]; usb_child(f,f->ents[f->usel].name,p,sizeof p);
    int r=fat_delete(p);
    wm_toast(r==1?"Deleted from USB stick": r==-2?"That folder isn't empty":"Couldn't delete it");
    usb_reload(f);
}
static void go(window_t *w, files_t *f, int dir){ f->dir=dir; f->sel=-1; f->scroll=0; f_title(w,f); }
static void f_mouse(window_t *w,int ev,int x,int y,int b){
    files_t *f=w->st; int cw=win_cw(w), ch=win_ch(w); (void)b;
    if(ev==EV_WHEEL){ f->scroll=imax(0,f->scroll+b*30); return; }
    if(ev!=EV_DOWN && ev!=EV_DBL) return;
    if(ev==EV_DOWN){
        int extra = (f->usb||fat.mounted) ? 130 : 0;
        if(f->usb){
            if(hit(x,y,8,8,44,26)){ usb_up(w,f); return; }
            if(hit(x,y,cw-176-130,8,124,26)){ if(f->usel>=0 && !f->ents[f->usel].dir) copy_from_usb(f,&f->ents[f->usel]); else wm_message("Copy to computer","Select a file on the stick first."); return; }
            if(hit(x,y,cw-176,8,96,26)){ char p[300]; char nm[40]; for(int k=1;k<100;k++){ strncpy0(nm,"New folder",40); if(k>1){ char q[8]; itoa10(k,q); strcat0(nm," ",40); strcat0(nm,q,40); }
                    int clash=0; for(int i=0;i<f->nents;i++) if(!strcmp(f->ents[i].name,nm)) clash=1; if(!clash) break; }
                usb_child(f,nm,p,sizeof p); if(!fat_mkdir(p)) wm_toast("Couldn't create the folder"); usb_reload(f); return; }
            if(hit(x,y,cw-74,8,66,26)){ usb_delete(f); return; }
        } else {
            if(hit(x,y,8,8,44,26)){ if(f->dir) go(w,f,fs_nodes[f->dir].parent); return; }
            if(extra && hit(x,y,cw-176-130,8,124,26)){ copy_to_usb(f,f->sel); return; }
            if(hit(x,y,cw-176,8,96,26)){ char nm[FS_NAME]; fs_unique_name(f->dir,"New folder","",nm); f->sel=fs_create(f->dir,nm,FT_DIR); return; }
            if(hit(x,y,cw-74,8,66,26)){ if(f->sel>0 && fs_nodes[f->sel].used){ fs_delete(f->sel); f->sel=-1; } return; }
        }
        for(int i=0;i<7;i++) if(hit(x,y,6,BAR_H+28+i*30,SIDE_W-12,26)){ int n=place_node(i); if(n>=0){ f->usb=0; f->sel=-1; go(w,f,n); } return; }
        if(usbstick.present && hit(x,y,6,BAR_H+28+7*30,SIDE_W-12,26)){
            if(!fat.mounted){ wm_message("USB stick",fat.status[0]?fat.status:usbstick.status); return; }
            f->usb=1; strncpy0(f->upath,"/",sizeof f->upath); f->scroll=0; usb_reload(f); f_title(w,f); return; }
    }
    if(f->usb){
        if(x<SIDE_W || y<BAR_H || y>ch-24) return;
        int c=cols(w), cx=(x-SIDE_W-8)/ITEM_W, cy=(y-BAR_H-8+f->scroll)/ITEM_H;
        int idx = (x-SIDE_W-8)>=0 && cx<c ? cy*c+cx : -1;
        if(idx<0||idx>=f->nents){ f->usel=-1; return; }
        f->usel=idx;
        if(ev==EV_DBL){
            fat_ent_t *e=&f->ents[idx];
            if(e->dir){ char p[300]; usb_child(f,e->name,p,sizeof p); strncpy0(f->upath,p,sizeof f->upath); f->scroll=0; usb_reload(f); f_title(w,f); }
            else { int n=copy_from_usb(f,e); if(n>=0) open_file(n); }
        }
        return;
    }
    if(x<SIDE_W || y<BAR_H || y>ch-24) return;
    int kids[FS_MAX]; int n=fs_children(f->dir,kids,FS_MAX);
    int c=cols(w), cx=(x-SIDE_W-8)/ITEM_W, cy=(y-BAR_H-8+f->scroll)/ITEM_H;
    int idx = (x-SIDE_W-8)>=0 && cx<c ? cy*c+cx : -1;
    if(idx<0||idx>=n){ f->sel=-1; return; }
    f->sel=kids[idx];
    if(ev==EV_DBL){
        if(fs_nodes[f->sel].type==FT_DIR) go(w,f,f->sel);
        else open_file(f->sel);
    }
}
static void f_key(window_t *w,int k){
    files_t *f=w->st;
    if(f->usb){
        if(k=='\b') usb_up(w,f);
        else if(k==KEY_DEL) usb_delete(f);
        else if(k==KEY_DOWN) f->scroll+=30; else if(k==KEY_UP) f->scroll=imax(0,f->scroll-30);
        return;
    }
    if(k==KEY_DOWN) f->scroll+=30;
    else if(k==KEY_UP) f->scroll=imax(0,f->scroll-30);
    else if(k=='\b'){ if(f->dir) go(w,f,fs_nodes[f->dir].parent); }
    else if(k==KEY_DEL){ if(f->sel>0){ fs_delete(f->sel); f->sel=-1; } }
    else if(k=='\n' && f->sel>0){ if(fs_nodes[f->sel].type==FT_DIR) go(w,f,f->sel); else open_file(f->sel); }
}
static void f_close(window_t *w){ files_t *f=w->st; if(f->ents) kfree(f->ents); }
void app_files(int dir){
    if(dir<0) dir=0;
    window_t *w=wm_open("Files",SIDE_W+5*ITEM_W+16,440,IC_FILES); if(!w) return;
    files_t *f=kzalloc(sizeof *f); f->sel=-1;
    w->st=f; w->draw=f_draw; w->mouse=f_mouse; w->key=f_key; w->close=f_close;
    w->resizable=1; w->min_w=560; w->min_h=260;
    go(w,f,dir);
}
