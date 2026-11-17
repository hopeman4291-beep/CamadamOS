/* CamadamOS installer: erases a chosen hard disk and installs CamadamOS on it */
#include "kernel.h"

const u8 *install_img; u32 install_len;
typedef struct { int state, sel; char confirm[16]; int focus; int progress; char err[96]; } inst_t;
enum { S_PICK, S_CONFIRM, S_WORK, S_DONE, S_FAIL };

static void put32le(u8 *p,u32 v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static int do_install(window_t *w,inst_t *s){
    int d=s->sel; u32 total=(install_len+511)/512;
    u32 chunk=256; /* 128 KB at a time */
    u8 *buf=kmalloc(chunk*512); if(!buf){ strncpy0(s->err,"Not enough memory.",sizeof s->err); return 0; }
    for(u32 done=0;done<total;done+=chunk){
        u32 n=total-done<chunk?total-done:chunk;
        memset(buf,0,chunk*512);
        u32 bytes=imin((int)(n*512),(int)(install_len-done*512));
        memcpy(buf,install_img+done*512,bytes);
        if(!disk_write(d,done,n,buf)){ kfree(buf); strncpy0(s->err,"Writing to the disk failed.",sizeof s->err); return 0; }
        s->progress=(int)((u64)(done+n)*90/total); wm_invalidate(); wm_pump();
    }
    /* data partition for saved files: starts on a 1 MB boundary after the system */
    u32 start=((total+2048)+2047)&~2047u;
    u32 size=disks[d].sectors>start+64 ? disks[d].sectors-start-64 : 0;
    if(size>1048576) size=1048576;                         /* up to 512 MB */
    if(size<8192){ kfree(buf); strncpy0(s->err,"This disk is too small (needs at least 32 MB).",sizeof s->err); return 0; }
    if(!disk_read(d,0,1,buf)){ kfree(buf); strncpy0(s->err,"Reading the disk failed.",sizeof s->err); return 0; }
    int slot=-1; for(int i=0;i<4;i++) if(buf[446+i*16+4]==0){ slot=i; break; }
    if(slot<0) slot=3;
    u8 *e=buf+446+slot*16; memset(e,0,16);
    e[1]=0xFE; e[2]=0xFF; e[3]=0xFF; e[4]=0xDA; e[5]=0xFE; e[6]=0xFF; e[7]=0xFF;
    put32le(e+8,start); put32le(e+12,size);
    if(!disk_write(d,0,1,buf)){ kfree(buf); strncpy0(s->err,"Writing the partition table failed.",sizeof s->err); return 0; }
    memset(buf,0,chunk*512);
    memcpy(buf,"CAMADATA",8); put32le(buf+8,1);              /* empty store */
    disk_write(d,start,1,buf);
    memset(buf,0,512);
    if(disks[d].sectors>40) disk_write(d,disks[d].sectors-34,34,buf); /* remove an old GPT backup */
    disk_flush(d);
    s->progress=95; wm_invalidate(); wm_pump();
    /* verify the first megabyte */
    for(u32 v=0;v<2048 && v<total;v+=chunk){
        u32 n=imin((int)chunk,(int)(total-v));
        if(!disk_read(d,v,n,buf)){ kfree(buf); strncpy0(s->err,"Checking the disk failed.",sizeof s->err); return 0; }
        u32 bytes=imin((int)(n*512),(int)(install_len-v*512));
        if(memcmp(buf+(v==0?512:0),install_img+v*512+(v==0?512:0),bytes-(v==0?512:0))){ kfree(buf); strncpy0(s->err,"The data on the disk doesn't match. The disk may be faulty.",sizeof s->err); return 0; }
    }
    kfree(buf);
    s->progress=100;
    store.disk=d; store.lba=start; store.sectors=size; store_save();
    return 1;
}
static void in_draw(window_t *w){
    inst_t *s=w->st; int cw=win_cw(w), ch=win_ch(w);
    fill_rect(0,0,cw,ch,RGB(250,249,253));
    gradient_v(0,0,cw,64,accent_light(),accent_dark());
    draw_text_big(20,16,"Install CamadamOS",RGB(255,255,255),2);
    int y=84; char b[128], sz[16];
    if(!install_img){
        draw_text(20,y,"This copy of CamadamOS has no installer image.",RGB(40,40,60)); y+=20;
        draw_text(20,y,"It's probably already installed on this computer.",RGB(40,40,60));
        return;
    }
    if(s->state==S_PICK){
        draw_text(20,y,"CamadamOS will be copied onto a hard disk, so the computer",RGB(40,40,60)); y+=18;
        draw_text(20,y,"starts it without the USB stick. Your files will be saved too.",RGB(40,40,60)); y+=30;
        if(!ndisks){
            draw_text_bold(20,y,"No supported hard disk was found.",RGB(200,40,40)); y+=22;
            draw_text(20,y,"CamadamOS supports SATA disks in AHCI mode. NVMe drives",RGB(90,90,110)); y+=18;
            draw_text(20,y,"and disks in RAID / Intel RST mode aren't supported yet.",RGB(90,90,110));
            return;
        }
        draw_text_bold(20,y,"Choose a disk:",RGB(40,40,60)); y+=24;
        for(int i=0;i<ndisks;i++){
            disk_size_str(i,sz);
            if(i==s->sel) fill_round_rect(16,y-4,cw-32,30,6,mix(accent_light(),0xFFFFFF,170));
            fill_circle(32,y+10,6,RGB(120,120,140)); if(i==s->sel) fill_circle(32,y+10,3,accent_dark());
            strncpy0(b,disks[i].model[0]?disks[i].model:"Disk",sizeof b); strcat0(b,"  -  ",sizeof b); strcat0(b,sz,sizeof b);
            draw_text(48,y+2,b,RGB(30,30,45)); y+=36;
        }
        ui_button(cw-140,ch-48,124,32,"Next",0);
    } else if(s->state==S_CONFIRM){
        disk_size_str(s->sel,sz);
        fill_round_rect(16,y,cw-32,96,8,RGB(255,230,230)); draw_rect(16,y,cw-32,96,RGB(220,60,60));
        draw_text_bold(30,y+12,"WARNING: this erases the whole disk",RGB(190,20,20));
        strncpy0(b,disks[s->sel].model,sizeof b); strcat0(b," (",sizeof b); strcat0(b,sz,sizeof b); strcat0(b,")",sizeof b);
        draw_text(30,y+34,b,RGB(60,20,20));
        draw_text(30,y+52,"Windows, documents, photos: everything on it will be gone.",RGB(60,20,20));
        draw_text(30,y+70,"This cannot be undone. Back up anything important first.",RGB(60,20,20));
        y+=118;
        draw_text(20,y,"To continue, type ERASE in capital letters:",RGB(40,40,60)); y+=24;
        fill_round_rect(20,y,200,30,6,RGB(255,255,255)); draw_rect(20,y,200,30,RGB(200,60,60));
        draw_text(30,y+7,s->confirm,RGB(30,30,45)); if((ticks/500)%2==0) vline(30+(int)strlen(s->confirm)*8,y+6,18,RGB(30,30,45));
        ui_button(16,ch-48,110,32,"Back",0);
        int ok=!strcmp(s->confirm,"ERASE");
        fill_round_rect(cw-196,ch-48,180,32,8,ok?RGB(200,40,40):RGB(220,200,200));
        draw_text_center(cw-196,ch-40,180,"Erase and install",RGB(255,255,255));
    } else if(s->state==S_WORK){
        draw_text(20,y,"Installing... don't turn off the computer.",RGB(40,40,60)); y+=30;
        fill_round_rect(20,y,cw-40,20,10,RGB(225,222,235));
        fill_round_rect(20,y,imax(20,(cw-40)*s->progress/100),20,10,accent_colors[theme_accent_idx]);
        itoa10(s->progress,b); strcat0(b," %",sizeof b); draw_text_center(0,y+28,cw,b,RGB(60,60,80));
    } else if(s->state==S_DONE){
        draw_text_bold(20,y,"CamadamOS is installed!",RGB(30,140,60)); y+=26;
        draw_text(20,y,"1. Remove the USB stick.",RGB(40,40,60)); y+=20;
        draw_text(20,y,"2. Restart the computer.",RGB(40,40,60)); y+=20;
        draw_text(20,y,"If the computer doesn't start CamadamOS by itself, choose the",RGB(90,90,110)); y+=18;
        draw_text(20,y,"hard disk in its boot menu (F12 on Lenovo laptops).",RGB(90,90,110));
        ui_button(cw-140,ch-48,124,32,"Restart now",0);
    } else {
        draw_text_bold(20,y,"The installation didn't finish.",RGB(200,40,40)); y+=24;
        draw_text(20,y,s->err,RGB(40,40,60)); y+=24;
        draw_text(20,y,"The disk may not start any more. You can try again,",RGB(90,90,110)); y+=18;
        draw_text(20,y,"or reinstall Windows from its recovery media.",RGB(90,90,110));
        ui_button(cw-140,ch-48,124,32,"Try again",0);
    }
}
static void start_install(window_t *w,inst_t *s){
    s->state=S_WORK; s->progress=0; wm_invalidate(); wm_pump();
    music_stop();
    s->state = do_install(w,s) ? S_DONE : S_FAIL;
}
static void in_mouse(window_t *w,int ev,int x,int y,int b){
    inst_t *s=w->st; int cw=win_cw(w), ch=win_ch(w); (void)b;
    if(ev!=EV_DOWN || !install_img) return;
    if(s->state==S_PICK){
        for(int i=0;i<ndisks;i++) if(hit(x,y,16,84+48+24-4+i*36,cw-32,30)) s->sel=i;
        if(ndisks && hit(x,y,cw-140,ch-48,124,32)){ s->state=S_CONFIRM; s->confirm[0]=0; }
    } else if(s->state==S_CONFIRM){
        if(hit(x,y,16,ch-48,110,32)) s->state=S_PICK;
        if(hit(x,y,cw-196,ch-48,180,32) && !strcmp(s->confirm,"ERASE")) start_install(w,s);
    } else if(s->state==S_DONE){
        if(hit(x,y,cw-140,ch-48,124,32)) reboot();
    } else if(s->state==S_FAIL){
        if(hit(x,y,cw-140,ch-48,124,32)) s->state=S_PICK;
    }
}
static void in_key(window_t *w,int k){
    inst_t *s=w->st; (void)w;
    if(s->state==S_PICK){ if(k==KEY_DOWN && s->sel<ndisks-1) s->sel++; if(k==KEY_UP && s->sel>0) s->sel--; if(k=='\n' && ndisks){ s->state=S_CONFIRM; s->confirm[0]=0; } return; }
    if(s->state!=S_CONFIRM) return;
    int L=(int)strlen(s->confirm);
    if(k=='\b'){ if(L) s->confirm[L-1]=0; }
    else if(k==KEY_ESC) s->state=S_PICK;
    else if(k=='\n'){ if(!strcmp(s->confirm,"ERASE")) start_install(w,s); }
    else if(k>=32 && k<127 && L<10){ s->confirm[L]=(char)k; s->confirm[L+1]=0; }
}
static void in_tick(window_t *w){ static u32 last; inst_t *s=w->st; if(s->state==S_CONFIRM && ticks-last>500){ last=ticks; wm_invalidate(); } }
void app_install(void){
    window_t *w=wm_open("Install CamadamOS",560,400,IC_INSTALL); if(!w) return;
    inst_t *s=kzalloc(sizeof *s);
    w->st=s; w->draw=in_draw; w->mouse=in_mouse; w->key=in_key; w->tick=in_tick;
}
