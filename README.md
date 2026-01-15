## Name

Emu68Blanker

![Screenshot](bin/Emu68Blanker.png)

## Author

Philippe CARPENTIER (flype)


## Repository

https://github.com/flype44/Emu68Blanker


## Requirements

An Amiga equipped with a PiStorm and Emu68.

Requires Emu68 version 1.0.999 or higher.


## Description

Emu68Blanker is a very simple screensaver for PiStorm/Emu68.

The purpose of this program is to display the Emu68 builtin boot screen after a period of user inactivity.


## Installation

Copy the Emu68Blanker file and the .info in the `SYS:WBStartup` folder.

Then edit the icon tooltypes to customize the `HOTKEY` and the `DELAY` tooltypes.

The `DELAY` tooltype allows to define the period of user inactivity, in seconds.

Defaults tooltypes are:

```
HOTKEY=control alt b
DELAY=180
```

## Additional notes

The Emu68 builtin boot screen can be `gray`, `black` or `purple`.

This is configurable in the Emu68 SDCard `cmdline.txt` file.

Eg. `logo=black`, or `logo=purple`, or unset for gray.


## Programming notes

This program was written as an example on how to use the new `mailbox.resource` present in the Emu68 1.0.999+ builtin ROM.

The headers for this resource are embedded in the Emu68-Tools package.

See https://github.com/michalsc/Emu68-Tools/releases




Amiga Rulez!
