VeltOS Installer Utility
===================

A graphical installer tool for VeltOS. It uses the custom Material
Design widget toolkit Cmk designed for the Graphene Desktop. It can
currently install to an existing partition, as well as install the
rEFInd boot manager.

The installation is powered by the command line utility
vos-install-cli, which, despite its name, is effectively an Arch
Linux installer. It is a single C file, with minimal dependencies
(and I hope to remove GLib as one of them soon) which can install
Arch Linux to an existing partition, create a user account, add
custom repositories install extra packages, set custom configurations,
install rEFInd, and more. Sadly it only works when running ON Arch
Linux already, as it requires Arch's pacman.

Anyone is welcome to use the CLI utility to create an their own
Arch Linux installer GUI, or for use on its own.

Building
--------

Building the CLI utility requires glib2, libudev, and pacman (and
CMake for building). The GUI requires those plus
[libcmk](https://github.com/VeltOS/cmk).

```bash

    cmake .
    make
    sudo make install
```

The compiled package is also available for Arch Linux from the
VeltOS repository, [repo.velt.io](http://repo.velt.io).

License
--------

Both the GUI and CLI programs are under the Apache License v2.0.

Authors
--------

Aidan Shafran <zelbrium@gmail.com>
