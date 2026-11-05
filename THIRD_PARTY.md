# Third-party software and data

GBAdhoc XMB is licensed GPL-2.0-or-later (see `LICENSE`). It includes or links the following
work by other people, each under its own licence.

## Included in this repository

| Component | Where | Licence | Used by |
|---|---|---|---|
| **stb_image** 2.30 and **stb_image_write** 1.16, Sean Barrett | `shared/third_party/` | Public domain or MIT, your choice (the licence is at the end of each header) | both apps: decoding and encoding art |
| **Inter** (variable), The Inter Project Authors | `psp/fonts/InterVariable.ttf`, `desktop/ui/fonts/Inter-latin.woff2` | SIL Open Font License 1.1 (`LICENSES/OFL-1.1.txt`) | PSP app (baked into `psp/gen/xmb_fonts.h`), desktop UI |
| **JetBrains Mono** 500, The JetBrains Mono Project Authors | `desktop/ui/fonts/JetBrainsMono-500-latin.woff2` | SIL Open Font License 1.1 (`LICENSES/OFL-1.1.txt`) | desktop UI |
| **WebView2.h**, Microsoft.Web.WebView2 1.0.2903.40, Microsoft Corporation | `desktop/third_party/webview2/` | BSD-style (`desktop/third_party/webview2/LICENSE.txt`, also `LICENSES/WebView2-BSD.txt`) | desktop app; header only, the WebView2 runtime is part of Windows |
| **Root CA certificates**: ISRG Root X1 and X2, USERTrust ECC and RSA, DigiCert Global Root G2 | `psp/net_ca.h` | Public certificates, redistributed as in every CA bundle | PSP app: checking the TLS servers it fetches from |

## Linked from the toolchain

The PSP builds link these from the `pspdev/pspdev` docker image; they are not in this
repository, but they are in the PSP EBOOTs.

| Component | Licence | Used by |
|---|---|---|
| **Mbed TLS** 2.28 | Apache-2.0 OR GPL-2.0-or-later (`LICENSES/mbedTLS.txt`) | PSP app: https:// |
| **zlib** 1.3.1, Jean-loup Gailly and Mark Adler | zlib licence (below) | PSP app: PNG and the baked name list |
| **PSPSDK** (pspdev) | BSD-style | PSP app and launcher |
| **newlib** | BSD-style and similar, per file | PSP app and launcher |
| **psp-cfw-sdk** (`libpspsystemctrl_user`, the custom firmware's import stubs) | GPL-3.0 | launcher: starting GBAdhoc through the firmware |

The Windows build links only the mingw-w64 runtime and Windows system libraries.

### zlib licence

```
(C) 1995-2024 Jean-loup Gailly and Mark Adler

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## Data the apps use

- **Game names.** `psp/gen/xmb_names.h` is a list of titles only, the file names of
  libretro-thumbnails' Named_Boxarts folders (No-Intro naming) and of GBAdhoc-heroart's hero
  art. It holds no ROM data and no images. The desktop app downloads the No-Intro DATs from
  [libretro-database](https://github.com/libretro/libretro-database) at run time.
- **Art.** Box art comes from [libretro-thumbnails](https://github.com/libretro-thumbnails) and
  hero art from [GBAdhoc-heroart](https://github.com/ShoshinFauteux/GBAdhoc-heroart), both
  fetched at run time onto the user's own PSP. None of it is in this repository or in a release.

## Our own art

All other art is ours: the GBAdhoc logo (`desktop/ui/assets/`), the app's ICON0 and PIC1
(`psp/assets/`, drawn by `psp/tools/make_app_art.py`), the XMB category glyphs
(`psp/tools/glyphs.js`), the title cards and every UI element, which are drawn in code. The
screenshots in `docs/img/` are captures of the two apps on test folders with placeholder
ROMs and drawn placeholder art.

## Images

| Component | Licence | Used for |
|---|---|---|
| **"Sony PSP" 3D model** by [Ilya Ostrovsky](https://sketchfab.com/strov) ([source](https://sketchfab.com/3d-models/sony-psp-dca89d10ec304d0cab76837750df7761)) | [CC BY 4.0](http://creativecommons.org/licenses/by/4.0/) | the README hero image (`docs/img/hero.png`), rendered with the app's screen on it. The model itself is not in this repository. |
