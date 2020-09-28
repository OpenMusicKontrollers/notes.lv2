/*
 * Copyright (c) 2019-2020 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#ifndef _NOTES_LV2_H
#define _NOTES_LV2_H

#include <stdint.h>
#include <limits.h>
#if !defined(_WIN32)
#	include <sys/mman.h>
#else
#	define mlock(...)
#	define munlock(...)
#endif

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/time/time.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/log/logger.h>
#include <lv2/lv2plug.in/ns/ext/patch/patch.h>
#include <lv2/lv2plug.in/ns/ext/options/options.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>

#define NOTES_URI    "http://open-music-kontrollers.ch/lv2/notes"
#define NOTES_PREFIX NOTES_URI "#"

// plugin uris
#define NOTES__notes          NOTES_PREFIX "notes"

// plugin UI uris
#define NOTES__ui             NOTES_PREFIX "ui"

// param uris
#define NOTES__text           NOTES_PREFIX "text"
#define NOTES__image          NOTES_PREFIX "image"
#define NOTES__fontHeight     NOTES_PREFIX "fontHeight"
#define NOTES__imageMaximized NOTES_PREFIX "imageMaximized"
#define NOTES__textMaximized  NOTES_PREFIX "textMaximized"
#define NOTES__imageMinimized NOTES_PREFIX "imageMinimized"
#define NOTES__textMinimized  NOTES_PREFIX "textMinimized"

#define MAX_NPROPS 7
#define CODE_SIZE 0x10000 // 64 K

typedef struct _plugstate_t plugstate_t;

struct _plugstate_t {
	int32_t font_height;
	int32_t image_maximized;
	int32_t text_maximized;
	int32_t image_minimized;
	int32_t text_minimized;
	char image [PATH_MAX];
	char text [CODE_SIZE];
};

#endif // _NOTES_LV2_H
