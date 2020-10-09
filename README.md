## Notes

### an LV2 plugin to store arbitrary notes

With this plugin you can store arbitrary notes/metadata/memos about your project.

#### Build status

[![build status](https://gitlab.com/OpenMusicKontrollers/notes.lv2/badges/master/build.svg)](https://gitlab.com/OpenMusicKontrollers/notes.lv2/commits/master)

### Binaries

For GNU/Linux (64-bit, 32-bit).

To install the plugin bundle on your system, simply copy the __notes.lv2__
folder out of the platform folder of the downloaded package into your
[LV2 path](http://lv2plug.in/pages/filesystem-hierarchy-standard.html).

<!--
#### Stable release

* [notes.lv2-0.12.0.zip](https://dl.open-music-kontrollers.ch/notes.lv2/stable/notes.lv2-0.12.0.zip) ([sig](https://dl.open-music-kontrollers.ch/notes.lv2/stable/notes.lv2-0.12.0.zip.sig))
-->

#### Unstable (nightly) release

* [notes.lv2-latest-unstable.zip](https://dl.open-music-kontrollers.ch/notes.lv2/unstable/notes.lv2-latest-unstable.zip) ([sig](https://dl.open-music-kontrollers.ch/notes.lv2/unstable/notes.lv2-latest-unstable.zip.sig))

### Sources

<!--
#### Stable release

* [notes.lv2-0.12.0.tar.xz](https://git.open-music-kontrollers.ch/lv2/notes.lv2/snapshot/notes.lv2-0.12.0.tar.xz)([sig](https://git.open-music-kontrollers.ch/lv2/notes.lv2/snapshot/notes.lv2-0.12.0.tar.xz.asc))
-->

#### Git repository

* <https://git.open-music-kontrollers.ch/lv2/notes.lv2>

### Bugs and feature requests

* [Gitlab](https://gitlab.com/OpenMusicKontrollers/notes.lv2)
* [Github](https://github.com/OpenMusicKontrollers/notes.lv2)

### Plugins

![Screenshot](/screenshots/screenshot_1.png)

#### Dependencies

##### Mandatory

* [LV2](http://lv2plug.in) (LV2 Plugin Standard)
* [OpenGl](https://www.opengl.org) (OpenGl)
* [GLEW](http://glew.sourceforge.net) (GLEW)
* [VTERM](http://www.leonerd.org.uk/code/libvterm) (Virtual terminal emulator)
* [XDG-UTILS](https://www.freedesktop.org/wiki/Software/xdg-utils/) (Freedesktop tools, namely xdg-open)

##### Optional

* [FONTCONFIG](https://www.fontconfig.org/) (Font configuration/access library)
* [FIRA](https://en.wikipedia.org/wiki/Fira_(typeface) (Fira typeface)

#### Build / install

	git clone https://git.open-music-kontrollers.ch/lv2/notes.lv2
	cd notes.lv2
	meson build
	cd build
	ninja -j4
	ninja test
	sudo ninja install

If you want to build with embedded Fira font, just disable fontconfig support
(-Duse-fontconfig=diabled). If fontconfig support is enabled, Fira font MUST
be present on your system.

#### UI

This plugin features a native LV2 plugin UI which embeds a terminal emulator
which can run your favorite terminal editor to edit the plugin's notes.

Currently, the editor has to be defined via the environment variable *EDITOR*:

    export EDITOR='vim'
    export EDITOR='emacs'

If no environment variable is defined, the default fallback editor is 'vi', as
it must be part of every POSIX system.

Whenever you save the notes, the plugin will try to just-in-time compile and
inject it. Potential warnings and errors are reported in the plugin host's log
and the UI itself.

On hi-DPI displays, the UI scales automatically if you have set the correct DPI
in your ~/.Xresources.

    Xft.dpi: 200

If not, you can manually set your DPI via environmental variable *D2TK_SCALE*:

    export D2TK_SCALE=200

#### License

Copyright (c) 2019-2020 Hanspeter Portner (dev@open-music-kontrollers.ch)

This is free software: you can redistribute it and/or modify
it under the terms of the Artistic License 2.0 as published by
The Perl Foundation.

This source is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
Artistic License 2.0 for more details.

You should have received a copy of the Artistic License 2.0
along the source as a COPYING file. If not, obtain it from
<http://www.perlfoundation.org/artistic_license_2_0>.
