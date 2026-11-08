# fx580vnxfromasmtoc
Compile .asm source code of Casio FX580 VN X (Vietnam Market) to .c source code

## Generate C ROM source

The address-annotated listing contains the complete ROM image. Generate a C
source file that embeds those bytes and exposes `fx580vnx_load_rom()` for the
nX-U8/100 emulator:

```sh
python3 tools/asm2c.py
gcc -std=c11 -Wall -Wextra -Iemu -c build/rom.c -o build/rom.o
```

The generator verifies that the listing has no conflicting or missing words.
This preserves the original machine code in a buildable C representation; it
does not translate the firmware into high-level C logic.
