# Changelog

## 0.1.0-beta.2

**Themes.** Both apps now come in four colour themes, chosen in Settings › Theme:

- **Graphite**, the new default: neutral near-black greys, a white focus ring and the logo's
  amber as the one accent. Warnings move to orange so they never read as the accent.
- **Slate**: cool slate greys with a steel-blue accent.
- **Teal**: the slate ground with the logo's own teal as the accent; information moves to
  steel blue.
- **Classic**: the indigo and violet of 0.1.0-beta.1, unchanged.

On the PSP, press ◀ or ▶ on the Theme row; on Windows, click a theme. The change applies at
once and is kept: `theme = graphite` in the PSP app's `xmbapp.ini`, `"palette"` in the Windows
app's `prefs.json`. Dark and light in the Windows app work with every theme.

- **Title cards follow the theme.** A Game Boy Advance game with no art gets a title card in
  the theme's colour family. Entries already written keep their icon; only entries made or
  updated after a change use the new theme.
- **The app's own icon** in the XMB is drawn in Graphite.
- **PSP Settings scroll**, so the last rows are no longer cut off by the button bar.
- **Contrast.** Graphite, Slate and Teal meet WCAG AA for every piece of text on the PSP
  (`psp/tools/check_shots.py`). Classic keeps beta 1's colours exactly, including its dimmer
  small text.
- Windows: the confidence chips on art keep readable colours in light mode; the selected card
  has a visible focus ring in light mode.
- **Fixed:** the PSP app no longer says "The network dialog could not open" when the dialog
  opened fine (a missing block in `psp/main.c`).

## 0.1.0-beta.1

The first test build: the PSP app and the Windows app, making XMB entries that open straight
into GBAdhoc 4.6.0-beta.4 or later.
