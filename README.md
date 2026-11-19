# CamadamOS

**A hobby operating system written from scratch in C and x86 assembly.**

CamadamOS boots on a real PC from a USB stick. It has its own graphical desktop, drivers, network stack, web browser, sound system, file system and apps. No Linux and no Windows are underneath it. The outside parts are the GRUB bootloader that starts it, MicroPython (the optional Python app), BearSSL (the encryption for HTTPS), TinyCC (the built-in C compiler), the minimp3 and stb_vorbis sound decoders, the Inter typeface and Mozilla's list of trusted certificate authorities.

![CamadamOS desktop](docs/screenshots/desktop.png)

**Version 2.1.1** · MIT license · by Camadam

---

## What's inside

**Apps**
- **Files:** folders, pictures, music, web pages and USB sticks.
- **Web:** a browser for today's web: secure **HTTPS**, style sheets (CSS), pictures, forms and international text. **Tabs**, **bookmarks**, **history**, a **download manager** and an **App Store** page. Search with DuckDuckGo.
- **Mail:** read, write and reply to e-mail (IMAP and SMTP over TLS), with Turkish and other accented letters, HTML messages and new-mail notifications.
- **Chat:** an IRC client for irc.libera.chat.
- **Paint**, **Notepad** (with copy and paste) and **Calculator**.
- **Music:** plays **MP3**, **OGG Vorbis**, **MIDI** and WAV files from /Music, /Downloads and /Documents, plus the built-in songs, with a seek bar and pause.
- **Video:** plays `.cvd` videos (pictures + MP3 sound in sync). Make them from any video with `tools/mkvideo.py` on another computer.
- **Recorder:** a **4-track recorder**: record from the microphone onto one track while the others play, mute tracks, and save the mix as a WAV file.
- **Piano:** real sound with piano, organ, saxophone, chiptune and bell instruments.
- **Code:** a code editor with tabs, line numbers, syntax colours (C, Python, BASIC, HTML, CSS, JavaScript), find and replace, and **F5 to run**.
- **C compiler:** write C in Code and press F5: CamadamOS compiles it **inside the OS** (TinyCC) and runs it, or installs it as an app. Examples in **/Examples/C**.
- **Clock** (alarm, timer, stopwatch) and **Weather**.
- **Terminal:** about 40 commands; type `help`.
- **BASIC:** a line-numbered BASIC like the 1980s home computers, with graphics and music.
- **Settings:** wallpapers, accent colours, mouse speed, sound, **user accounts** (each person gets their own files, wallpaper and password) and **laptop** settings (screen brightness on Intel graphics).
- **Notifications:** pop-up cards and a bell in the taskbar that keeps the last 30 (alarms, downloads, mail, screenshots, apps); a quiet mode.
- **Hardware:** lists every PCI device and names the Wi-Fi card (About → Hardware, or `hardware` in Terminal).
- **My Apps:** install extra apps from `.cxe` files and uninstall them again.
- **Python:** a Python 3 IDE (MicroPython) with an editor, a console, turtle graphics, **pygame** and **tkinter**, installed as an app.

**Games:** Chess with a computer opponent **or another CamadamOS computer on your network**, **Crystal Caves** (a 3D maze), Minesweeper, Solitaire, Tetris, Snake, Pong, Breakout and 2048, plus six more you can install as apps: Space Defenders, Rock Storm, Sudoku, Four in a Row, Checkers and Sky Hopper.

**Desktop:** smooth anti-aliased text everywhere, windows with soft shadows and rounded corners, a see-through frosted taskbar and start menu, animations when windows open, close and minimize, your screen's own sharp resolution, a lock screen, a boot jingle, screenshots (Print Screen), copy and paste, and themes.

| Chess | BASIC |
|---|---|
| ![Chess](docs/screenshots/chess.png) | ![BASIC](docs/screenshots/basic.png) |

![Files showing a USB stick](docs/screenshots/usb-stick.png)

---

## Apps you can install (.cxe)

CamadamOS 0.6 can install extra programs. An app is a single `.cxe` file. Double-click it (in Files, on a USB stick or after downloading it in Web) and CamadamOS shows its icon, name, author and description, then asks whether to install it. Installed apps appear in **My Apps** on the desktop, where you can open or uninstall them.

- **Compiled apps** are C programs built with the CamadamOS SDK and run as real machine code.
- **BASIC apps** are made inside CamadamOS: write a program in BASIC and type `MAKEAPP "My Game", 1`.
- Eleven apps come with CamadamOS in the **/Examples** folder, ready to install:
  - **Python:** the Python IDE (see below).
  - **Games:** Space Defenders, Rock Storm, Sudoku, Four in a Row, Checkers and Sky Hopper.
  - **Other apps:** Mandelbrot Explorer, Target Practice, Times Tables and Fireworks.
  Open **My Apps → Example apps** and double-click one to install it.

![An installed app running from My Apps](docs/screenshots/apps.png)

To make your own apps, see [sdk/README.md](sdk/README.md).

---

## Built from scratch

| Part | What CamadamOS does itself |
|---|---|
| Kernel | 32-bit protected mode, interrupts, timer, memory allocator that uses all of the PC's RAM (up to 1 GB) |
| Graphics | Double-buffered framebuffer drawing at the screen's native resolution, proportional anti-aliased text with UTF-8, anti-aliased shapes, soft shadows, blur and animations, window manager |
| Input | PS/2 keyboard, mouse and touchpad; USB keyboards and mice through its own xHCI (USB 3) driver |
| Storage | SATA (AHCI) driver; installer; automatic saving of files and settings to the installed disk |
| USB sticks | USB mass storage (Bulk-Only Transport + SCSI); FAT16/FAT32 with long file names, reading and writing |
| Network | Intel e1000 and Realtek drivers; ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, HTTP/1.1 and HTTPS (the TLS maths comes from BearSSL) |
| Web | HTML layout engine with a CSS subset: style sheets, selectors, colours, backgrounds, font sizes, bold and italic, alignment, hidden parts, `@media` |
| Images | GIF, PNG (with its own inflate), JPEG and BMP decoders |
| Sound | Intel HD Audio driver, 12-voice synthesizer, WAV playback, PC speaker fallback |
| Chess | Full rules and an alpha-beta search engine |
| BASIC | Interpreter with floating-point maths, graphics, `SOUND` and `PLAY` |
| Apps | `.cxe` app format, a loader that relocates compiled code, an app API, an installer and an SDK |

---

## Try it

### In a virtual machine (safest)

With [QEMU](https://www.qemu.org/) on Windows:

```
qemu-system-i386 -m 512 -cdrom camadamos.iso -nic user,model=e1000 -device intel-hda -device hda-duplex
```

On VirtualBox, make a VM of type "Other / Unknown" with 512 MB of RAM and attach `camadamos.iso` as the CD.

### On a real PC

1. Download `camadamos.iso` from the **Releases** page.
2. Flash it to a USB stick with [Rufus](https://rufus.ie) (choose **DD Image mode**) or [balenaEtcher](https://etcher.balena.io). This erases the stick.
3. Restart the PC and open the boot menu, usually with F12, F11, F8 or Esc. Choose the USB stick.
4. If the PC won't boot it, turn off **Secure Boot** in the BIOS/UEFI settings.

CamadamOS runs from the stick and **doesn't touch your hard drive** unless you use the installer. It boots on both older BIOS PCs and newer UEFI PCs.

It uses your screen's native resolution automatically, and text gets bigger on large screens (very large 4K-class screens are drawn at half size and doubled, so everything stays readable). If the picture looks wrong, choose **CamadamOS - choose a screen size** in the boot menu.

### Installing to a hard drive

The **Install** icon copies CamadamOS to a SATA drive. Your files and settings are then saved automatically.

Your files, settings and installed apps are kept on that drive.

> ⚠️ The installer **erases the whole drive** you choose, and you have to type `ERASE` to confirm. Only use it on a spare PC or a spare drive.

---

## Python

Install **Python** from **My Apps → Example apps**. It opens an IDE with an editor at the top and a console at the bottom.

- Write a program and press **F5** (or **Run**). **Esc** or **Stop** stops it, even an endless loop. The OS stays responsive while Python runs.
- If the program crashes, the line with the error turns red.
- `input()` asks questions in the console.
- Type Python straight into the console to try things out, like `2 ** 100` or `[x * x for x in range(10)]`. After a program finishes, its variables are still there to inspect.
- **Turtle graphics:** `import turtle` works like the turtle module in normal Python (`forward`, `left`, `circle`, `color`, `begin_fill`...) and draws in a *Python Graphics* window.
- **The `cam` module** is for games and drawings: `cam.line`, `cam.rect`, `cam.circle`, `cam.text`, `cam.clear`, `cam.key("left")` for held keys, `cam.mouse()` and `cam.beep()`.
- Also included: `math`, `random`, `time`, `json`, `re`, `collections`, `struct`, f-strings and big integers. `open()` reads and writes files; plain names go in `/Documents`.
- **Save as** stores programs in `/Programs`, and `import myfile` loads `/Programs/myfile.py`. Double-clicking a `.py` file opens it in the IDE.
- **Examples** has thirteen programs to start from, including turtle drawings, a times-tables quiz, two pygame games and two tkinter apps.

### Making apps from Python programs

Press **Make app** in the Python window to turn the program into an app, like `MAKEAPP` in BASIC:

- Give it a name, a description (the first `#` comment is used) and one of nine icons.
- Modules it imports from `/Programs` and files it names in quotes (pictures, `.txt`, `.json`, `.csv` ...) are packed into the app automatically. The window lists them.
- The app is saved as `/Programs/<name>.cxe`, and the install window opens. Installed apps appear in **My Apps** marked "Python".
- A Python app runs without the editor. A console window appears only when the program prints, asks with `input()` or stops with an error, so pygame and tkinter apps look like normal apps.
- Inside the app, the packed files are in a read-only folder `/App`; `pygame.image.load("ship.png")`, `open("words.txt")` and `import helper` find them. Files the app writes go to `/Documents`.
- Python apps are small (a few KB plus their pictures), because they run with the Python app. To share one, copy the `.cxe` to a USB stick; the other computer needs CamadamOS 0.9 or newer, with Python installed.

### pygame

`import pygame` works like pygame 2, so most beginner games and tutorials run unchanged:

- `pygame.display.set_mode`, `flip`, `set_caption`; `Surface`, `Rect`, `Color`, `Vector2`
- `pygame.draw` (rect with rounded corners, circle, ellipse, line, lines, polygon, arc)
- `pygame.image.load` for **PNG, JPEG, GIF and BMP** pictures (with transparency), `image.save` (BMP)
- `pygame.font.Font` / `SysFont` (one smooth font at any size), `pygame.transform` (scale, rotate, flip, rotozoom)
- `pygame.event.get()` with keys, mouse buttons, mouse wheel and QUIT; `pygame.key.get_pressed()`, `pygame.mouse`
- `pygame.time.Clock().tick(60)`, `pygame.time.set_timer`; sprites and groups with collisions, `pygame.mask`

Pictures are found in `/Programs`, `/Documents`, `/Pictures` or `/Downloads`. A game bigger than the screen is shrunk to fit. Sound files are silent, because CamadamOS can only play tones so far; use `pygame.mixer.beep(440, 200)` for a tone.

### tkinter

`import tkinter` gives you windows with real widgets:

- `Tk`, `Toplevel`, `Frame`, `LabelFrame`, `Label`, `Button`, `Entry`, `Text`, `Checkbutton`, `Radiobutton`, `Scale`, `Listbox`, `Spinbox`, `Scrollbar`, `Canvas`, `Menu` (menu bars and pop-ups), `OptionMenu`, `PhotoImage`
- `pack`, `grid` and `place`; `bind` for keys and the mouse; `after` timers; `StringVar`, `IntVar`, `BooleanVar`, `DoubleVar`
- `tkinter.ttk`: the same widgets plus `Combobox`, `Progressbar`, `Notebook` (tabs), `Treeview` (tables) and `Separator`
- **Message boxes:** `messagebox.showinfo`, `showwarning`, `showerror`, `askyesno`, `askokcancel`, `askyesnocancel`, `askquestion`, `askretrycancel`. They also work in pygame programs and without a `Tk()` window.
- `simpledialog.askstring` / `askinteger` / `askfloat`, `filedialog.askopenfilename` / `asksaveasfilename` / `askdirectory`, `colorchooser.askcolor`

```python
import tkinter as tk
from tkinter import messagebox

root = tk.Tk()
root.title("Hello")
tk.Button(root, text="Click me", command=lambda: messagebox.showinfo("Hi", "Hello, world!")).pack(padx=40, pady=20)
root.mainloop()
```

Windows made in the console stay alive between commands, like in IDLE. Letters with accents, including Turkish (ç ğ ı İ ö ş ü), work in both pygame and tkinter.

```python
import turtle
turtle.bgcolor("navy")
turtle.speed(0)
for i in range(36):
    turtle.color(["red", "orange", "yellow", "lime", "cyan", "violet"][i % 6])
    turtle.circle(80)
    turtle.left(10)
```

This is MicroPython, a compact Python 3, so a few things from desktop Python, like `pip` and most of the standard library, aren't available. pygame and tkinter here are work-alikes written for CamadamOS: they follow the real APIs, but rarely used parts (pygame sound files, numpy arrays, OpenGL; Text-widget word wrapping and undo) are missing.

## Programming in C, inside CamadamOS

Open **Code**, write a C program, save it and press **F5**. CamadamOS compiles it with its built-in
C compiler (TinyCC, running inside CamadamOS itself) and starts it straight away. The compiler
window shows any errors with their line numbers, and **Install as app** turns the program into a
`.cxe` app with an icon in My Apps. In Terminal, `cc file.c` does the same.

- A program with `main()` is a **console program**: what it prints with `printf` appears in an
  output window when it finishes.
- A program with `cxe_main(const cx_api *api)` is a **window app** that can draw, play notes,
  read the mouse and keyboard, use files and the web: the same API as the SDK
  ([sdk/README.md](sdk/README.md)).
- Available headers: `cxe.h`, `stdio.h` (printf, sprintf), `string.h`, `stdlib.h` (malloc, rand,
  qsort...), `math.h` (sqrt, sin, cos, pow...), `ctype.h`, `stdint.h`, `stdbool.h`, `stdarg.h`.
- Examples: **/Examples/C** has `hello.c` (console), `bounce.c` (bouncing balls) and `spiral.c`.

![A C program compiled and run inside CamadamOS](docs/screenshots/c-compiler.png)

Programs run as real machine code with full access to the computer, like the apps you install,
so a bug can freeze CamadamOS: save your work first.

## Music, video and recording

Copy MP3, OGG, MIDI or WAV files into /Music (from a USB stick or with Web's download manager)
and open **Music**. To watch a video, turn it into a `.cvd` file on another computer with
`python3 tools/mkvideo.py input.mp4 output.cvd` (it needs ffmpeg; `--width 360 --fps 12` makes it smaller),
copy it over and double-click it. **Recorder** records from the built-in microphone, the
microphone jack or line in; saved mixes go to /Music as WAV files.

![Recorder](docs/screenshots/recorder.png)

## Internet through your phone (USB tethering)

No cable near the router? Connect your phone to the computer with its USB cable, then turn on
**USB tethering** on the phone (Android: Settings → Network & internet → Hotspot & tethering →
USB tethering). CamadamOS notices it within a few seconds, shows a notification and uses the
phone's Wi-Fi or mobile data. Unplug the phone and it goes back to the Ethernet port.

It works with Android phones (RNDIS and the newer NCM) and with USB Ethernet adapters that use
the standard CDC-ECM. iPhones use Apple's own method and don't work yet.

## Mail

Open **Mail** and fill in your address and password. Gmail, Yahoo, Yandex, iCloud and many
others work; Gmail needs an **app password** (Google Account → Security → App passwords),
because CamadamOS can't do Google's sign-in page. Outlook.com/Hotmail no longer allow
password sign-in for mail programs, so they don't work.

## Using BASIC

Open **BASIC** from the desktop. You can type numbered lines like the old home computers, or press **F2** for the full-screen editor and write modern, structured BASIC without line numbers:

```basic
' Count down, then celebrate
FOR I = 5 TO 1 STEP -1
  PRINT I
NEXT I
Celebrate "Utku"

SUB Celebrate(Name$)
  PRINT "Well done, "; Name$; "!"
  PLAY "T180 C E G > C"
END SUB
```

Press **F5** in the editor to run it. BASIC supports:
- `SUB` and `FUNCTION` with local variables (including recursion), and `SHARED`;
- `IF ... ELSEIF ... ELSE ... END IF`, `SELECT CASE`, and `DO ... LOOP` with `WHILE` or `UNTIL`;
- `EXIT DO`/`EXIT FOR`, labels with `GOTO`, and `CONST`;
- reading and writing files (`OPEN`, `PRINT #`, `INPUT #`, `LINE INPUT`, `EOF`), mouse input (`MOUSEX`, `MOUSEY`, `MOUSEB`) and `PAINT`.

The classic numbered style still works:

```basic
10 FOR I = 1 TO 5
20 PRINT "Hello number"; I
30 NEXT I
RUN
```

- `RUN` starts the program, `LIST` shows it, and **Esc** stops it.
- `SAVE "name"` and `LOAD "name"` store and open programs in /Programs.
- You can also write programs in Notepad and save them as `.bas`.
- There are six example programs; type `FILES` to list them and `HELP` for every command.
- `MAKEAPP "name", icon` turns your program into an installable app.

---

## Keyboard shortcuts

| Key | Action |
|---|---|
| F2 / F3 | Mouse slower / faster |
| F4 | Control the pointer with the keyboard |
| F5 | Mouse test |
| F12 | Lock the screen |
| Print Screen | Take a screenshot (saved in /Pictures) |
| Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+A | Copy / cut / paste / select all |

On many laptops you hold **Fn** for the F-keys.

---

## Not supported yet

- Wi-Fi cards. Use an Ethernet cable, or **your phone's internet over USB** (see below). About → Hardware names your Wi-Fi card.
- Battery level, sleeping when the laptop lid closes, and the Fn brightness keys (they need a full ACPI interpreter). **Shut down** does turn real PCs and laptops off, and brightness is in Settings → Laptop.
- Real YouTube (it needs a full JavaScript engine and video codecs); Video plays `.cvd` files instead
- NVMe SSDs, and disks in RAID / Intel RST mode (for installing)
- USB hubs
- exFAT and NTFS USB sticks (reformat them as FAT32)
- JavaScript, so very modern sites may look plain or incomplete
- Sites that only speak TLS 1.3 (they open through FrogFind instead), because BearSSL has no TLS 1.3 yet
- Passwords protect accounts from other people at the login screen, but files on the disk are not encrypted, and the mail password is only lightly scrambled

---

## Building from source

On Linux or WSL:

```
sudo apt install build-essential gcc-multilib nasm grub-pc-bin grub-efi-amd64-bin grub-common xorriso mtools qemu-system-x86 python3-pil python3-cryptography
make          # builds camadamos.iso
make run      # builds it and starts it in QEMU
```

### Map of the source code

| File | What it does |
|---|---|
| `src/boot.asm` | Multiboot header, stack, interrupt entry points |
| `src/kernel.c` | CPU setup, interrupts, timer, keyboard, mouse, memory, boot sequence |
| `src/gfx.c` | Drawing: shapes, anti-aliasing, shadows, blur |
| `src/uifont.c`, `tools/mkuifont.py` | Smooth proportional text; the Inter font is baked into tables at build time |
| `src/font.c` | The fixed-width 8 x 16 font (terminal, BASIC, Notepad) |
| `src/wm.c` | Desktop, windows, taskbar, start menu, screenshots |
| `src/fs.c`, `src/files.c` | File system and the Files app |
| `src/pci.c` | PCI bus access shared by the drivers |
| `src/net.c`, `src/web.c`, `src/irc.c` | Network drivers, TCP/IP and HTTP, browser (HTML + CSS), chat |
| `src/tls.c`, `tools/mkta.py` | HTTPS: connects BearSSL to the TCP stack; turns the certificate list into C |
| `third_party/bearssl/` | BearSSL TLS library (MIT license) |
| `third_party/cacert/`, `third_party/inter/` | Trusted root certificates (Mozilla), the Inter typeface |
| `src/img.c` | Image decoders |
| `src/sound.c`, `src/songs.c`, `src/music.c` | HD Audio, synthesizer, songs, Music, Piano |
| `src/usb.c`, `src/fat.c` | USB controller, keyboards, mice and sticks; FAT file system |
| `src/disk.c`, `src/persist.c`, `src/install.c` | SATA driver, saving to disk, installer |
| `src/chess.c` | Chess rules, engine and board |
| `src/basic.c` | BASIC interpreter |
| `src/cxe.c` | App format, loader, installer, My Apps |
| `src/cc.c`, `src/cclib.c`, `src/tcc_core.c` | The C compiler: window, its small C library, TinyCC built into the kernel |
| `third_party/tinycc/`, `sdk/cc/` | TinyCC (LGPL); the headers, runtime and examples for programs compiled inside CamadamOS |
| `src/code.c` | Code editor |
| `src/media.c`, `src/video.c`, `src/recorder.c` | MP3/OGG/MIDI playback, Video, Recorder |
| `third_party/media/` | minimp3 and stb_vorbis decoders |
| `src/mail.c` | Mail (IMAP + SMTP) |
| `src/users.c` | User accounts |
| `src/notify.c` | Notifications |
| `src/hwinfo.c`, `src/laptop.c` | Hardware list; ACPI power off and screen brightness |
| `src/maze3d.c` | Crystal Caves |
| `sdk/`, `tools/mkcxe.py` | App SDK: header, linker script, example apps, packer |
| `apps/python/` | The Python app: IDE, MicroPython port, `cam`, `turtle` and `_gfx` (drawing, pictures, windows, dialogs) |
| `apps/python/lib/` | pygame, tkinter and a small `os`, written in Python and built into the app |
| `apps/python/examples/` | The example programs of the IDE |
| `tools/pyhost/` | Runs the Python app on the build machine with fake windows, for testing |
| `third_party/micropython/` | The parts of MicroPython the Python app uses (MIT license) |
| `src/games.c`, `mines.c`, `solitaire.c`, `snake.c` | Games |
| `src/paint.c`, `notepad.c`, `calc.c`, `clock.c`, `term.c`, `settings.c`, `diag.c` | Other apps |

---

## Version history

- **2.1.1:** fixes phones (e.g. Samsung Galaxy) switching the USB tethering option back off: CamadamOS now notices when a phone reconnects in a new mode, and recovers when a device refuses a request
- **2.1:** internet through your phone with **USB tethering** (Android RNDIS and NCM, plus CDC-ECM USB Ethernet adapters)

- **2.0:** browser **tabs**, bookmarks, history, download manager and App Store page; **Mail**; **MP3, OGG and MIDI** music; **Video** (`.cvd`) with a converter; a **4-track Recorder**; **Code**, a code editor; a **C compiler inside the OS**; network **Chess** between two CamadamOS computers; **Crystal Caves** 3D game; more Python libraries (`requests`, `csv`, `shelve`, `cam.http`, `cam.notify`); **user accounts** with passwords; **notifications**; laptop basics (real **ACPI power off**, screen brightness on Intel graphics, a Hardware page that names the Wi-Fi card)
- **1.0:** real **HTTPS** (TLS 1.2 with certificate checking), style sheets in web pages, Turkish and other accented letters on web pages, DuckDuckGo search; smooth proportional text across the whole system, soft window shadows, rounded corners, frosted see-through taskbar and start menu, window animations, smoother icons and buttons; the screen's native resolution is picked automatically, with larger text on big screens and a screen-size boot menu
- **0.9:** Python apps: **Make app** in the Python window packs a program, its modules and pictures into a `.cxe` that installs into My Apps; apps can open the install window (`install` in the app API); `open()` in Python saves every write at once
- **Python app update (runs on 0.8):** pygame and tkinter (with ttk, message boxes and file dialogs), PNG/JPEG/GIF pictures in Python, Turkish letters in Python windows, four new examples
- **0.8:** the Python app (MicroPython IDE with console, turtle graphics, file access and imports), apps can open files and list folders, function keys reach apps
- **0.7:** six new games as installable apps, BASIC editor with syntax colours, structured BASIC (SUB/FUNCTION, block IF, SELECT CASE, DO/LOOP), file input/output, mouse and PAINT in BASIC, held-key input for app games
- **0.6:** install and uninstall apps (.cxe), My Apps, SDK for compiled C apps, `MAKEAPP` for BASIC apps, four example apps, safer saving when upgrading
- **0.5:** Chess with AI, BASIC, USB sticks, copy and paste, screenshots, resizable windows, more reliable saving
- **0.4:** real HD Audio sound, USB mouse and keyboard, hard-drive installer, saving to disk
- **0.3:** lock screen, boot jingle, native resolution, images and downloads in the browser, IRC chat, Clock, Weather, more games
- **0.2:** networking, web browser, Notepad, Calculator, Terminal, Settings
- **0.1:** first prototype with the desktop, Minesweeper, Solitaire, Paint, Files and Music

## License

MIT, see [LICENSE](LICENSE). The font and other third-party parts are listed in [THIRD_PARTY.md](THIRD_PARTY.md).
