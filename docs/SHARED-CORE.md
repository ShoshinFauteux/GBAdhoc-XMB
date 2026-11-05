# The shared core: xw_core and xw_match

`shared/` holds the C both apps build: platform-free, compiled by mingw-w64 for Windows and by
psp-gcc for the PSP (`psp/xw_core_psp.c` includes `shared/xw_core.c` unchanged).

## xw_core.h: writing and reading entries

- `xw_make_entry(e, ...)`: one entry, `EBOOT.PBP` and `launch.ini`, in `PSP/GAME/<folder>/`.
- `xw_build_pbp(e, icon_png, pic_png, &pbp, err, cap)`: the entry's EBOOT.PBP in memory.
  `icon_png` / `pic_png` are finished PNGs (must be 144x80 / 480x272) or NULL to make them from
  `e->icon_image` / `e->bg_image`. `xw_make_entry` calls it.
- `xw_launch_ini(e, out, cap)`: launch.ini's text, exactly what `xw_make_entry` writes, and
  `xw_parse_launch_ini(text, rom, cap, gbadhoc, cap)`, the reverse.
- `xw_read_entry(dir, template, len, &info)`: an entry read back (TITLE, rom, gbadhoc, whether
  DATA.PSP is this launcher's, has ICON0/PIC1).
- `xw_pbp_section_file(path, XW_ICON0, &buf)`: one section of a PBP on disk.
- `xw_png_size(png, len, &w, &h)` and `xw_crc32(crc, data, len)` (zlib's CRC-32, as No-Intro
  DATs give it).

The desktop app writes an entry to a staging folder and moves it into PSP/GAME in one rename
(an unplug never leaves half an entry in the XMB), composes ICON0/PIC1 itself, and lists the
entries already on the stick, so it uses the in-memory and read-back calls.

### Art

Any image stb_image reads (PNG, JPEG, BMP, GIF, TGA) is made into a PNG of the right size. If
the file is already a PNG of exactly `w` x `h`, 8-bit, not interlaced, and whole (IHDR first,
every chunk inside the file with a correct CRC, IEND last and at the very end), its own bytes
are kept: re-encoding took about 15 s per entry on the PSP. Anything else, including a
truncated or damaged PNG, goes through the decoder, which refuses what it cannot read.

## xw_match.h: recognising a ROM (desktop)

Matching a ROM against a No-Intro DAT (clrmamepro form, as libretro-database ships them):
`xw_dat_parse`, `xw_rom_header_parse` (GBA game code, GB/GBC CGB flag), `xw_dat_match`
(checksum, exact name, game code, close name, and "hack": the game code of a No-Intro game but
not its dump), and the name helpers the art lookups need: `xw_nointro_base`,
`xw_nointro_region`, `xw_nointro_display` ("Pokemon - Emerald Version (USA)" -> "Pokemon
Emerald"), `xw_thumb_name` (libretro-thumbnails' file name), `xw_norm_key`, `xw_name_score`.

The PSP app recognises by file name instead (`psp/names.c` against the baked list in
`psp/gen/xmb_names.h`: libretro-thumbnails' Named_Boxarts names plus the names hero art exists
for), so a match is also the URL of the art. A title counts as recognised at 80 % or above.

## text_fold.h

GBAdhoc's own helper for file names that are not ASCII: a display fold (é -> e) for the PSP's
baked fonts, and the FAT 8.3 fallback for opening a long name the firmware could not convert.
It is a copy of GBAdhoc's `psp/text_fold.h`.
