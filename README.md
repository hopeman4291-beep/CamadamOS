# CamadamOS 0.4

A hobby operating system written from scratch in C and x86 assembly. It has a graphical desktop with themes and 14 programs: Minesweeper, Solitaire, Snake, Paint, Files, Web, Music, Piano, Notepad, Calculator, Terminal, Settings, Mouse Test and About.

## New in 0.4

- **Real sound:** an Intel High Definition Audio driver with a 12-voice software synthesizer. Instruments are piano, organ, saxophone, chiptune and bell, chosen in Music or in Settings -> Sound. The piano is polyphonic, and WAV files play from Files. Without an HD Audio chip, sound falls back to the PC speaker.
- **USB:** an xHCI (USB 3) driver for USB mice and keyboards (HID boot protocol), with key repeat. USB hubs aren't supported yet. The boot menu entry "CamadamOS (without USB driver)" is a fallback.
- **Installer:** the Install icon erases a chosen SATA disk (AHCI mode) after you type ERASE, then installs CamadamOS on it with a data partition.
- **Saving:** an installed CamadamOS saves files and settings to the disk automatically, a couple of seconds after each change. Files then shows "Saved on disk". Use the Terminal command `save` to save at once, and `disks` to list disks.
- New Terminal commands: `usb`, `sound`, `disks`, `save`.

Not supported yet: NVMe drives, disks in RAID / Intel RST mode, USB hubs and USB storage.

## New in 0.3

- **Lock screen** with a big clock and your name (set it in Settings), plus a **boot jingle**. Press F12 or choose Lock in the menu to lock.
- **Native resolution:** the default boot entry uses the screen's own resolution. There's also a 1366 x 768 entry and a safe 1024 x 768 entry.
- **Web browser images:** GIF, PNG, baseline JPEG and BMP, decoded from scratch. Progressive JPEGs show their text label instead.
- **Downloads:** files that aren't web pages are saved to /Downloads. The **Save** button saves the current page.
- **Chat:** an IRC client. It connects to irc.libera.chat on port 6667 and supports /join, /msg, /me, /nick and more.
- **Clock** (analog clock, stopwatch, timer, alarm) and **Weather** (wttr.in, with Open-Meteo as a backup).
- **Games folder:** Minesweeper, Solitaire, Snake, plus new **Tetris, Pong, Breakout and 2048**.
- The TCP stack now handles several connections at once.

## New in 0.2

- **Networking from scratch:** drivers for Intel e1000 network cards (QEMU, VirtualBox) and Realtek RTL810x/8168 chips (many laptops, including the Lenovo G50-80), plus a TCP/IP stack with ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP and HTTP/1.0.
- **Web:** a 1990s-style HTML browser with links, lists, tables, forms, a back button and an address bar. HTTPS sites are opened through [FrogFind](http://frogfind.com), which simplifies modern pages for vintage browsers. Typing words in the address bar searches FrogFind.
- **Notepad, Calculator, Terminal** (`help` lists the commands, including `ping`, `get` and `net`) and **Snake**.
- **Settings:** six wallpapers, six accent colours, mouse speed, clock offset and 12/24-hour time, and network status.
- **Keyboard shortcuts:** F2/F3 mouse speed, F4 keyboard mouse, F5 Mouse Test. On Lenovo laptops, hold Fn.

## Try it first in a virtual machine (recommended)

**QEMU** (free, best sound support). On Windows:

```
qemu-system-i386 -m 256 -cdrom camadamos.iso -nic user,model=e1000 -audiodev dsound,id=snd0 -machine pcspk-audiodev=snd0
```

On Linux, replace `dsound` with `pa`.

**VirtualBox:** create a new VM with Type "Other" and Version "Other/Unknown", give it 256 MB of RAM, leave the network adapter as "Intel PRO/1000 MT Desktop", attach `camadamos.iso` as the optical drive and start it. Sound from the PC speaker is usually silent in VirtualBox.

## Put it on a USB stick

1. Download **balenaEtcher** or **Rufus**.
2. Select `camadamos.iso` and your USB stick. In Rufus, choose **DD Image mode** if it asks.
3. Flash it. This erases everything on the stick.

## Boot a PC from the stick

1. Plug in the stick and restart the PC.
2. Press the boot menu key while it starts. It is usually F12, F11, F8 or Esc, depending on the brand.
3. Pick the USB stick. CamadamOS boots on both older (BIOS) and newer (UEFI) PCs.
4. If the PC refuses to boot it, turn off **Secure Boot** in the BIOS/UEFI settings.

CamadamOS runs directly from the stick and **does not touch your hard drive**. Remove the stick and restart to go back to your normal system.

## Known limits of version 0.1

- **Mouse and keyboard:** these use the old PS/2 interface. USB devices work if the PC's firmware emulates PS/2. Look for "USB Legacy Support" in the BIOS. Some newer UEFI-only laptops don't offer this.
- **Sound:** sound comes from the PC speaker, so it plays one note at a time. Many modern PCs have no PC speaker and will stay silent.
- **Files:** the file system lives in RAM, so files you save disappear when you restart.
- **Network:** wired Ethernet only, no Wi-Fi. Plug in a cable before or after booting. HTTPS only works through FrogFind.
- **Settings** aren't saved and reset when you restart.
- **Clock:** the clock shows the PC's hardware clock time, which may be UTC.

## Building from source (Linux or WSL)

```
sudo apt install build-essential nasm grub-pc-bin grub-efi-amd64-bin grub-common xorriso mtools qemu-system-x86
make          # builds camadamos.iso (the USB image, with the installer inside)
make run      # builds and starts it in QEMU
```

## Map of the source code

| File | What it does |
|---|---|
| `src/boot.asm` | Multiboot header, stack, interrupt entry stubs |
| `src/kernel.c` | CPU tables, interrupts, timer, keyboard, mouse, clock, speaker, memory, boot sequence |
| `src/gfx.c` | Drawing: pixels, shapes, text, double buffering |
| `src/wm.c` | Desktop, windows, taskbar, start menu, icons |
| `src/fs.c` | RAM file system and the built-in files and pages |
| `src/net.c` | PCI scan, network card drivers, TCP/IP stack, HTTP |
| `src/web.c` | HTML browser: parser, layout, forms, history |
| `src/notepad.c`, `calc.c`, `term.c`, `snake.c`, `settings.c`, `diag.c` | Programs added in 0.2 |
| `src/img.c` | GIF, PNG (with inflate), JPEG and BMP decoders |
| `src/irc.c`, `clock.c`, `games.c` | Chat, Clock and Weather, Tetris/Pong/Breakout/2048 |
| `src/sound.c` | HD Audio driver, synthesizer, WAV playback |
| `src/usb.c` | xHCI driver, USB mouse and keyboard |
| `src/disk.c`, `persist.c`, `install.c` | SATA (AHCI) driver, saving to disk, installer |
| `src/pci.c` | PCI access shared by all drivers |
| `src/songs.c` | The song library and the music engine |
| `src/mines.c`, `solitaire.c`, `paint.c`, `files.c`, `music.c` | The original programs |
| `src/font.c` | Font bitmap generated from DejaVu Sans Mono by `tools/mkfont.py` |

To add a song, append a line to `songs[]` in `src/songs.c`. The note format is `NOTE[#|b]OCTAVE:LENGTH`, for example `C5:2 Eb5:1 R:2`. `R` is a rest, and `T150` changes the tempo.
