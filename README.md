Raspberry Pi Neopixel Signboard
==========

The bulk of this code was shamelessly stolen from the https://github.com/rpi-ws281x/
project from which the majority of the code and the following sections of the README
were taken.

![photo](images/photo.jpg)

My changes were added to allow a scrolling text signboard (typically 8 pixels by something)
which you can make by adding a cheap NeoPixel array like the one at https://a.co/d/f1bIZp8
to something like a Raspberry Pi Zero W2. You can even chain multiple display together to
make the display even longer (set the "y") parameter on the test.

Run like:

`./neosign -x 8 -y 32 -m 'Hello, World! This is a test!'`

Pipe option allows you to specify a named pipe (`-p filename`). Subsequent writes to the named
pipe with update the display with the given text.

It will normally scroll text (scroll mode) on pixels, but can alternateley present text *line by line*
with the `-f` (flash mode) option.

# Important flags
For a complete list of flags, use `--help`. But important ones are:

`-x` *height* of panel - defaults to 8 pixels high 
`-y` *width* of panel - defaults to 32 pixels wide. (When daisy-chaining additional panel, set to 64, etc)
`-m` Display message on pannel
`-p` Create and listen on named pipe for new text. (echo or write to pipe to update text)
`-S` Debug out to screen - lets you run on any system without hardware
`-C 0xrrggbb` Set color rgb hex value. (Helpful with -S above which sometimes doesn't display dim colors)
`-f` "Flash" text rather than scroll
`-c` Clear display on exit

# Inline color commands
The sequence `[ESC]cRRGGBB` where RR GG and BB are each exactly 2 hex digits allows you to specify explicit color (changes) in inline text. (See examples below for doing this with shell commands)


# Colors
`./neosign  -y 64 -m $'\ecff00ffThis is a test of \ecffff00the emergency broadcasting \ec00ffffsystem' -S -f`
`./neosign  -y 128 -m $'\ec0000ffMake\ecff8000!\ec0000fft \ecff8000Labs' -S -f`

# Building

```
git submodule update --init --recursive
cmake .
make
```

# Test and Debug

The `-S` flag will write output to (ANSI) screen, and requires no pixels, as follows.

```
0         1         2         3         4         5         6
-123456789-123456789-123456789-123456789-123456789-123456789-123


 ████  ██  ██   ████  ██ ███   ███ ██  ████  █████   ████  ██  █
██  ██ ███████ ██  ██  ███ ██ ██  ██  ██  ██ ██  ██ ██  ██ ██  █
██████ ███████ ██████  ██  ██ ██  ██  ██████ ██  ██ ██     ██  █
██     ██ █ ██ ██      ██      █████  ██     ██  ██ ██  ██  ████
 ████  ██   ██  ████  ████        ██   ████  ██  ██  ████      █
                              █████                        █████

```

Note that sometimes very dim colors will show up as all black on some terminals.
To work around this, you may want to specify an explicit color with the `-C` flag like:

`./neosign  -y 64 -m "This is a test of the emergency broadcasting" -f -S -C 0xff00ff`

