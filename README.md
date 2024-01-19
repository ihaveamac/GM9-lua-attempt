# GodMode9 but with lua

Fork of [d0k3/GodMode9](https://github.com/d0k3/GodMode9) with super experimental Lua support.

A simple test... Includes Lua 5.4.6 with a few changes for compile-time warnings

* Makefile.build edited to support a LIBS variable on build

* Math lib (-lm) now needed for lua

* link.ld on arm9 edited to include .ARM.exidx into .rodata, caused by lua's code. Limit of AHBWRAM max size was increased until address 0x80C0000, 744KiB since 0x8006000

Copy init.lua to the SD card and select it. There is now an "Execute Lua script" option.

Or put your script at `0:/gm9/luascripts/*.lua`. Then press HOME or POWER and choose "Lua scripts...".

The main lua stuff is at `arm9/source/lua`. Custom files are prefixed with `gm9`.

The API here is not at all stable. But there are currently two libraries to play with. This is not set in stone!

# NOTES FOR BUILDING lua-attempt-linker-crimes

This branch alters the arm9/link.ld file to split the binary into two memory regions.

The Makefile is not updated yet. After you run `make`, you must manually extract the sections and build the firm yourself.

* `arm-none-eabi-objcopy -O binary arm9/arm9.elf --only-section=AHBWRAM AHBWRAM.bin`
* `arm-none-eabi-objcopy -O binary arm9/arm9.elf --only-section=AHBWRAM2 AHBWRAM2.bin`
* `firmtool build out.firm -D AHBWRAM.bin AHBWRAM2.bin arm11/arm11.elf -A 0x08000000 0x080a0000 -C NDMA NDMA XDMA -n 0x08000040`

Alternatively, run `build_firm.sh` which does all of the above.

## Global

* print(...)
  * Calling this will replace the alt screen with an output buffer. It doesn't support newlines or word wrapping properly yet

## UI

* void UI.ShowPrompt(string text)
* bool UI.AskPrompt(string text)
* void UI.ShowString(string text)
* string UI.WordWrapString(string text[, int llen])
  * llen == -1 means alt screen
* void UI.ClearScreen(number screen, u32 color)
* number UI.ShowSelectPrompt(table optionstable, string text)
* bool UI.ShowProgress(u32 current, u32 total, text text)
  * returns true if B is pressed
* void UI.DrawString(int which\_screen, string text, int x, int y, int color, int bgcolor)
  * which\_screen:
    * 0 = main screen
    * 1 = alt screen
    * 2 = top screen
    * 3 = bottom screen

## FS

* bool FS.InitImgFS(string path)
* string FS.FileGetData(string path, int size, int offset)
