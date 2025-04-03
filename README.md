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

# Building

```
git submodule update --init --recursive
cmake .
make
```
