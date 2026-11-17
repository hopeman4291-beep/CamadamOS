/* CamadamOS - shared declarations */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32;
typedef uint64_t u64; typedef int64_t s64;

#define OS_NAME "CamadamOS"
#define OS_VERSION "0.5"

/* ---------- port I/O ---------- */
static inline void outb(u16 p, u8 v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline u8 inb(u16 p){ u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outw(u16 p, u16 v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl(u16 p, u32 v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline u32 inl(u16 p){ u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline u16 inw(u16 p){ u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void io_wait(void){ outb(0x80,0); }
static inline void cli(void){ __asm__ volatile("cli"); }
static inline void sti(void){ __asm__ volatile("sti"); }
static inline void hlt(void){ __asm__ volatile("hlt"); }

/* ---------- libc-ish ---------- */
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void memset32(u32 *d, u32 v, u32 n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int atoi(const char *s);
void strncpy0(char *d, const char *s, size_t n); /* always terminates */
void strcat0(char *d, const char *s, size_t n);
const char *strrchr0(const char *s, char c);
void itoa10(int v, char *out);
int ends_with(const char *s, const char *suffix);
u32 rand32(void);
void srand32(u32 s);
int rnd(int n); /* 0..n-1 */
static inline int imin(int a,int b){return a<b?a:b;}
static inline int imax(int a,int b){return a>b?a:b;}
static inline int iabs(int a){return a<0?-a:a;}
static inline int clampi(int v,int lo,int hi){return v<lo?lo:(v>hi?hi:v);}

/* ---------- heap ---------- */
void heap_init(void);
void *kmalloc(u32 size);
void *kzalloc(u32 size);
void kfree(void *p);
u32 heap_free_bytes(void);

/* ---------- time / hardware ---------- */
extern volatile u32 ticks; /* milliseconds since boot */
void sleep_ms(u32 ms);
void rtc_time(int *h, int *m, int *s);
void rtc_date(int *d, int *mo, int *y);
void speaker_tone(u32 freq);
void speaker_off(void);
void reboot(void);
void shutdown(void);
extern u32 mem_total_kb;

/* ---------- input events ---------- */
enum { KEY_UP=0x100, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ESC, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F12, KEY_PRTSC, KEY_DEL, KEY_HOME, KEY_END };
extern volatile int mouse_x, mouse_y, mouse_btn;
#define KEY_CTRL(c) (0x200 + (((c)|32) - 'a'))   /* Ctrl + letter */
void clip_set(const char *text, int len);
const char *clip_get(void);                    /* never NULL */
void screenshot(void);
extern int mouse_speed;
typedef struct {
    u8 raw[64], rawsrc[64]; u16 rawdt[64]; int rawn;   /* every byte from port 0x60 */
    s16 ddx[8], ddy[8]; u8 dbtn[8]; int dn;            /* last decoded packets */
    u32 ok, badfirst, ovf, jump;
    int id, cfg, reset_ok;
    int pkt_size, resync, filter, swap_xy, invert_y;    /* adjustable in Mouse Test */
    u8 kraw[16]; int kn;
} ps2diag_t;
extern ps2diag_t ps2d;
void mouse_reinit(void);
void mouse_inject(int btn);
void app_mousetest(void);
void wm_toast(const char *msg);
int  kbd_get(void);           /* returns -1 if none */
int  mouse_get(int *dx, int *dy, int *btn, int *wheel); /* returns 0 if none */

/* ---------- graphics ---------- */
extern int scr_w, scr_h;
extern int gfx_ox, gfx_oy;    /* drawing origin */
#define RGB(r,g,b) ((u32)(((r)<<16)|((g)<<8)|(b)))
void gfx_init(u32 fb, u32 pitch, u32 w, u32 h, u32 bpp, u8 rpos, u8 gpos, u8 bpos);
void gfx_present(void);
void gfx_set_clip(int x, int y, int w, int h); /* absolute coords */
void gfx_reset_clip(void);
void gfx_get_clip(int *x0,int *y0,int *x1,int *y1);
void gfx_set_clip_raw(int x0,int y0,int x1,int y1);
void pset(int x, int y, u32 c);
u32  pget(int x, int y);
void fill_rect(int x, int y, int w, int h, u32 c);
void blend_rect(int x, int y, int w, int h, u32 c, int alpha);
void draw_rect(int x, int y, int w, int h, u32 c);
void hline(int x, int y, int w, u32 c);
void vline(int x, int y, int h, u32 c);
void draw_line(int x0,int y0,int x1,int y1,u32 c);
void fill_circle(int cx,int cy,int r,u32 c);
void draw_circle(int cx,int cy,int r,u32 c);
void fill_round_rect(int x,int y,int w,int h,int r,u32 c);
void gradient_v(int x,int y,int w,int h,u32 top,u32 bottom);
void draw_char(int x, int y, char ch, u32 c);
void draw_text(int x, int y, const char *s, u32 c);
void draw_text_bold(int x, int y, const char *s, u32 c);
void draw_text_big(int x, int y, const char *s, u32 c, int scale);
int  text_width(const char *s);
void draw_text_center(int x, int y, int w, const char *s, u32 c);
void blit(int x,int y,int w,int h,const u32 *src,int stride);
void draw_pixmap(int x,int y,const char *const *rows,int scale);
void draw_bevel(int x,int y,int w,int h,int pressed);
u32  *gfx_backbuffer(void);
u32  mix(u32 a, u32 b, int t); /* t 0..255 */
#define FONT_W 8
#define FONT_H 16

/* ---------- window manager ---------- */
enum { EV_DOWN=1, EV_UP, EV_MOVE, EV_DBL, EV_WHEEL };
typedef struct window {
    int used, minimized, resizable, maximized, min_w, min_h;
    int x, y, w, h;          /* outer frame */
    int rx, ry, rw, rh;      /* size before maximizing */
    char title[48];
    int icon;
    void *st;
    void (*draw)(struct window*);
    void (*mouse)(struct window*, int ev, int x, int y, int btn);
    void (*key)(struct window*, int key);
    void (*tick)(struct window*);
    void (*close)(struct window*);
} window_t;
#define TITLE_H 28
#define BORDER 3
static inline int win_cw(window_t *w){ return w->w - 2*BORDER; }
static inline int win_ch(window_t *w){ return w->h - TITLE_H - BORDER; }
window_t *wm_open(const char *title, int cw, int ch, int icon);
void wm_close(window_t *w);
void wm_invalidate(void);
void wm_set_title(window_t *w, const char *t);
void wm_toggle_max(window_t *w);
void wm_focus(window_t *w);
void basic_install_samples(void);
void wm_run(void);
void wm_message(const char *title, const char *msg);
int  ui_button(int x,int y,int w,int h,const char *label,int pressed); /* draw */
static inline int hit(int px,int py,int x,int y,int w,int h){ return px>=x && py>=y && px<x+w && py<y+h; }
extern u32 ui_accent;

/* icons */
enum { IC_MINES, IC_SOLITAIRE, IC_PAINT, IC_FILES, IC_BROWSER, IC_MUSIC, IC_PIANO, IC_ABOUT, IC_TEXT, IC_FOLDER, IC_IMAGE, IC_SONG, IC_PAGE, IC_NOTEPAD, IC_CALC, IC_TERM, IC_SNAKE, IC_SETTINGS, IC_IRC, IC_CLOCK, IC_WEATHER, IC_TETRIS, IC_PONG, IC_BREAKOUT, IC_2048, IC_INSTALL, IC_CHESS, IC_BASIC, IC_USB, IC_COUNT };
void draw_icon(int id, int x, int y, int scale, int tile);

/* ---------- file system (RAM) ---------- */
#define FS_NAME 32
enum { FT_DIR, FT_TEXT, FT_PAGE, FT_SONG, FT_IMAGE, FT_BIN };
typedef struct fsnode {
    int used;
    char name[FS_NAME];
    int type;
    int parent;
    char *data; u32 size;   /* text/page contents, or image pixels */
    int img_w, img_h;
    int song;               /* built-in song index */
    int readonly;
} fsnode_t;
#define FS_MAX 512
extern u32 fs_rev;                 /* bumped whenever file contents change (drives autosave) */
static inline void fs_touch(void){ fs_rev++; }
u8 *bmp_encode(const u32 *px,int w,int h,u32 *len);
extern u32 heap_extra_mb;
extern fsnode_t fs_nodes[FS_MAX];
void fs_init(void);
int  fs_find(int dir, const char *name);
int  fs_lookup(const char *path);
int  fs_create(int dir, const char *name, int type);
void fs_delete(int node);
int  fs_children(int dir, int *out, int max);
void fs_path(int node, char *out, int n);
void fs_unique_name(int dir, const char *base, const char *ext, char *out);

/* ---------- apps ---------- */
void app_mines(void);
void app_solitaire(void);
void app_paint(int file);
void app_files(int dir);
void app_music(int file);
void app_piano(void);
void app_about(void);
void open_file(int node);

/* songs */
typedef struct { const char *title; const char *composer; int ms_per_unit; const char *notes; } song_t;
extern const song_t songs[];
extern const int song_count;
u32 note_freq(int semitone_from_a4_x);

/* music engine (runs in timer interrupt) */
void music_play(int song);
void music_stop(void);
void music_play_notes(const char *notes, int ms_per_unit);
void clock_service(void);
void app_games(void);
int  music_playing(void);  /* song index or -1 */
int  music_progress(void); /* 0..1000 */
u32  music_current_freq(void);
void music_tick_ms(void);
void piano_note(u32 freq, u32 ms);

/* ---------- PCI ---------- */
u32 pci_read(int bus,int dev,int fn,int off);
void pci_write(int bus,int dev,int fn,int off,u32 v);
int pci_find(int cls,int sub,int progif,int n,int *b,int *d,int *f);
u32 pci_bar_mem(int b,int d,int f,int idx);
void pci_enable(int b,int d,int f);
static inline u32 mmio_r32(u32 a){ return *(volatile u32*)a; }
static inline void mmio_w32(u32 a,u32 v){ *(volatile u32*)a=v; }
static inline u16 mmio_r16(u32 a){ return *(volatile u16*)a; }
static inline void mmio_w16(u32 a,u16 v){ *(volatile u16*)a=v; }
static inline u8 mmio_r8(u32 a){ return *(volatile u8*)a; }
static inline void mmio_w8(u32 a,u8 v){ *(volatile u8*)a=v; }
void udelay(u32 us);

/* ---------- disks, saving, installer ---------- */
#define MAXDISKS 4
typedef struct { int port; u32 sectors; char model[41]; } disk_t;
extern disk_t disks[MAXDISKS]; extern int ndisks;
void disk_init(void);
int  disk_read(int d, u32 lba, u32 count, void *buf);
int  disk_write(int d, u32 lba, u32 count, const void *buf);
int  disk_flush(int d);
void disk_size_str(int d, char *out);
typedef struct { int disk; u32 lba, sectors; int loaded, saves; char status[64]; } persist_t;
extern persist_t store;
void store_init(void);
void store_tick(void);
int  store_save(void);
extern const u8 *install_img; extern u32 install_len;
void app_install(void);

/* ---------- USB ---------- */
typedef struct { u16 vid, pid; u8 cls, speed, nif, kbd, mouse, gone, msc; u8 ifc[4], ifs[4], ifp[4]; } usbdev_info_t;
typedef struct { int present, controllers, keyboards, mice, other; u32 reports; char status[48]; usbdev_info_t dev[8]; int ndev; } usbinfo_t;
extern usbinfo_t usb;
void usb_init(int enabled);
void usb_poll(void);
void mouse_input(int dx, int dy, int btn, int wheel);   /* dy > 0 means up */
void kbd_inject(int key);
/* USB sticks */
typedef struct { int present, port, changed; u32 sectors, bsize; char vendor[12], product[20], status[40]; } usbstick_t;
extern usbstick_t usbstick;
int usb_msc_read(u32 lba, u32 count, void *buf);
int usb_msc_write(u32 lba, u32 count, const void *buf);
/* FAT16/FAT32 on the USB stick */
typedef struct { char name[64]; u32 size; int dir; } fat_ent_t;
typedef struct { int mounted, type; char label[12]; char status[64]; u32 total_mb; } fatinfo_t;
extern fatinfo_t fat;
void fat_mount(void);
void fat_unmount(void);
int  fat_list(const char *path, fat_ent_t *out, int max);      /* -1 = not found */
int  fat_read(const char *path, u8 **data, u32 *len);            /* 1 ok; *data kmalloc'd */
int  fat_write(const char *path, const u8 *data, u32 len);       /* 1 ok */
int  fat_delete(const char *path);
int  fat_mkdir(const char *path);

/* ---------- sound ---------- */
enum { INST_PIANO, INST_ORGAN, INST_SAX, INST_CHIP, INST_BELL, INST_COUNT };
extern const char *const instrument_names[INST_COUNT];
typedef struct { int hda; char codec[48]; int volume; int instrument; u16 codec_vendor, codec_device; int pins; } soundinfo_t;
extern soundinfo_t snd;
void sound_init(void);
void sound_tick(void);                       /* called from the 1 kHz timer */
void snd_note(u32 freq, int inst, u32 ms, int vel);
void snd_tone_on(u32 freq);                  /* monophonic melody voice */
void snd_tone_off(void);
void snd_stop_all(void);
int  snd_play_wav(const u8 *data, u32 len);  /* 1 ok, 0 unsupported */
void snd_stop_wav(void);
int  snd_wav_playing(void);
int  snd_level(void);                        /* recent output level 0..100 for meters */

/* ---------- images ---------- */
#define IMG_CLEAR 0xFF000000u                /* transparent pixel marker */
u32 *image_decode(const u8 *d, u32 n, int *w, int *h);
u32 *image_scale(u32 *px, int w, int h, int nw, int nh);
void blit_key(int x,int y,int w,int h,const u32 *src,int stride);

/* ---------- network ---------- */
typedef struct {
    int present; char chip[48]; u16 vendor, device;
    u8 mac[6]; int link;
    u32 ip, mask, gw, dns;     /* host byte order: a.b.c.d = a<<24|... */
    int dhcp;                  /* 0 idle, 1 discovering, 2 requesting, 3 bound */
    u32 rx_pkts, tx_pkts;
} netinfo_t;
extern netinfo_t net;
void net_init(void);
void net_poll(void);
int  net_wait_ready(u32 ms);                 /* waits for DHCP; 1 if online */
int  dns_resolve(const char *host, u32 *ip);
int  net_ping(u32 ip, u32 timeout);          /* round trip in ms, or -1 */
void ip_to_str(u32 ip, char *o);
int  parse_ip(const char *s, u32 *ip);
typedef void (*status_fn)(const char *msg);
/* returns HTTP status (200, 404...) or negative error; *body is kmalloc'd */
enum { HTTP_ERR_URL=-1, HTTP_ERR_HTTPS=-2, HTTP_ERR_NONET=-3, HTTP_ERR_DNS=-4, HTTP_ERR_CONNECT=-5, HTTP_ERR_TIMEOUT=-6, HTTP_ERR_MEM=-7 };
int  http_get(const char *url, char **body, u32 *len, char *location, int locn, char *ctype, int ctn, status_fn st);
int  tcp_open(u32 ip, u16 port, u32 bufcap);    /* blocking connect, returns socket id or -1 */
int  tcp_write(int id, const void *d, int n);   /* blocking until acknowledged */
int  tcp_read(int id, void *out, int max);      /* non-blocking */
int  tcp_avail(int id);
int  tcp_is_open(int id);
void tcp_close(int id);
const char *http_err_str(int e);

/* ---------- more UI ---------- */
void wm_pump(void);                          /* redraw now (used during long operations) */
extern int clock_offset, clock_12h;
extern int theme_wallpaper, theme_accent_idx;
void theme_apply(void);
extern const char *const wallpaper_names[];
extern const u32 accent_colors[];
#define N_WALLPAPERS 6
#define N_ACCENTS 6
u32 accent_dark(void);
u32 accent_light(void);
void app_web(const char *url);
const char *translit(u32 codepoint);
u32 utf8(const char *s, u32 n, u32 *i);
void app_irc(void);
void app_clock(void);
void app_weather(void);
void app_tetris(void);
void app_chess(void);
u32 chess_perft(int d);
void app_basic(const char *file);

void app_pong(void);
void app_breakout(void);
void app_2048(void);
void lock_screen(void);
extern char user_name[32];
void app_notepad(int file);
void app_calc(void);
void app_terminal(void);
void app_snake(void);
void app_settings(void);
