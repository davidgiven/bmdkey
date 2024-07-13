bmdkey
======

What?
-----

This is a simple and fairly crude tool for turning a Blackmagic Design Speed
Editor into a macro pad on X. Each key of the macro pad is mapped to a function
key with Alt and Meta and sometimes Shift pressed. The jog wheel is mapped to
the mouse scroll wheel buttons.

This only works if DaVinci Resolve isn't open.

It uses [smunaut's algorithm for unlocking the
device](https://github.com/smunaut/blackmagic-misc). Without this, the device is
a paperweight unless you have Resolve running.

<iframe width="560" height="315" src="https://www.youtube.com/embed/UoIlwze5xp4?si=8X7iZ6ykQxiWAlHv" title="YouTube video player" frameborder="0" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" referrerpolicy="strict-origin-when-cross-origin" allowfullscreen></iframe>

Why?
----

I found having a nice piece of hardware which was locked down to only work with
one particular application offensive.

Where?
------

It's [open source on GitHub!](https://github.com/davidgiven/bmdkey)

How?
----

Build it with `make`. You'll need `libfakekey`, `hidapi-libusb` and `fmt`. Then
just run the `bmdkey` application with the keyboard connected up.

There are no configuration options. Also, if you unplug the Speed Editor, it'll
crash.

You _should_ be able to use this on other BMD hardware, but I haven't tried. If
anyone wants to send me some incredibly expensive keyboards for free, I'll quite
happily do a port!

Who?
----

You may contact me at dg@cowlark.com, or visit my website at
http://www.cowlark.com.  There may or may not be anything interesting there.


License
-------

Everything here is © 2024 David Given, and is licensed under the two-clause BSD open source
license. Please see [LICENSE](LICENSE) for the full text. The tl;dr is: you can
do what you like with it provided you don't claim you wrote it.

