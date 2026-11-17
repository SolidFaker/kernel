# Graphics and terminal

## Framebuffer

The 16-bit loader sets VBE mode `1280x1024x32` with a linear framebuffer and writes a
`struct bootinfo` at physical `0x7600`:

- physical framebuffer address, width, height, pitch and bpp
- kernel maps it at virtual `0xD0000000`
- `drivers/fbcon.c` owns rendering

## Graphical terminal

- rounded window, title bar with traffic lights, tty name and grid size
- status bar with tty index, uptime and shortcuts
- 76x56 character cells, 8x8 font scaled 2x, Catppuccin-style 16-color palette
- ANSI SGR parsing for colors in shell output
- software blinking cursor
- each tty has its own cell buffer; `Alt+1..4` switches ttys
- text output still goes through `NR_write`; user programs do not need to know about
  the framebuffer

`drivers/display.c` keeps the tty cell grid in RAM and calls `fbcon` for rendering.
`drivers/tty.c` handles tty selection and cursor restoration.

## `gfx` demo

`drivers/graphic.c` provides a logical 320x200x256 canvas:

- pixels, lines, rectangles, circles, 8x8 text
- 3-3-2 color packing via `GFX_RGB()`
- rendered at 4x inside the VBE framebuffer
- leaving the demo redraws the terminal

User space uses the `NR_gfx` syscall (`include/gfx.h`):

```sh
gfx          # or: run gfx.elf
```
