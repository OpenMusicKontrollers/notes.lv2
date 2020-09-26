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

#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <utime.h>

#include <notes.h>
#include <props.h>

#define SER_ATOM_IMPLEMENTATION
#include <ser_atom.lv2/ser_atom.h>

#include <d2tk/hash.h>
#include <d2tk/frontend_pugl.h>

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

#ifdef _LV2_HAS_REQUEST_VALUE
	LV2UI_Request_Value *request;
#endif

	d2tk_pugl_config_t config;
	d2tk_frontend_t *dpugl;

	LV2UI_Controller *controller;
	LV2UI_Write_Function writer;

	PROPS_T(props, MAX_NPROPS);

	plugstate_t state;
	plugstate_t stash;

	uint64_t hash;

	LV2_URID atom_eventTransfer;
	LV2_URID atom_beatTime;
	LV2_URID urid_text;
	LV2_URID urid_fontHeight;
	LV2_URID urid_image;

	bool reinit;
	char template [24];
	int fd;
	time_t modtime;

	float scale;
	d2tk_coord_t header_height;
	d2tk_coord_t footer_height;
	d2tk_coord_t font_height;

	uint32_t max_red;

	int done;
};

static inline void
_update_font_height(plughandle_t *handle)
{
	handle->font_height = handle->state.font_height * handle->scale;
}

static void
_intercept_text(void *data, int64_t frames __attribute__((unused)),
	props_impl_t *impl)
{
	plughandle_t *handle = data;

	ssize_t txt_len = impl->value.size - 1;
	const char *txt = handle->state.text;

	const uint64_t hash = d2tk_hash(txt, txt_len);

	if(handle->hash == hash)
	{
		return;
	}

	handle->hash = hash;

	// save txt to file
	if(lseek(handle->fd, 0, SEEK_SET) == -1)
	{
		lv2_log_error(&handle->logger, "lseek: %s\n", strerror(errno));
	}
	if(ftruncate(handle->fd, 0) == -1)
	{
		lv2_log_error(&handle->logger, "ftruncate: %s\n", strerror(errno));
	}
	if(fsync(handle->fd) == -1)
	{
		lv2_log_error(&handle->logger, "fsync: %s\n", strerror(errno));
	}
	if(write(handle->fd, txt, txt_len) == -1)
	{
		lv2_log_error(&handle->logger, "write: %s\n", strerror(errno));
	}
	if(fsync(handle->fd) == -1)
	{
		lv2_log_error(&handle->logger, "fsync: %s\n", strerror(errno));
	}

	// change modification timestamp of file
	struct stat st;
	if(stat(handle->template, &st) == -1)
	{
		lv2_log_error(&handle->logger, "stat: %s\n", strerror(errno));
	}

	handle->modtime = time(NULL);

	const struct utimbuf btime = {
	 .actime = st.st_atime,
	 .modtime = handle->modtime
	};

	if(utime(handle->template, &btime) == -1)
	{
		lv2_log_error(&handle->logger, "utime: %s\n", strerror(errno));
	}

	handle->reinit = true;
}

static void
_intercept_font_height(void *data, int64_t frames __attribute__((unused)),
	props_impl_t *impl __attribute__((unused)))
{
	plughandle_t *handle = data;

	_update_font_height(handle);
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = NOTES__text,
		.offset = offsetof(plugstate_t, text),
		.type = LV2_ATOM__String,
		.event_cb = _intercept_text,
		.max_size = CODE_SIZE
	},
	{
		.property = NOTES__fontHeight,
		.offset = offsetof(plugstate_t, font_height),
		.type = LV2_ATOM__Int,
		.event_cb = _intercept_font_height
	},
	{
		.property = NOTES__image,
		.offset = offsetof(plugstate_t, image),
		.type = LV2_ATOM__Path,
		.max_size = PATH_MAX
	}
};

static void
_message_set_key(plughandle_t *handle, LV2_URID key)
{
	ser_atom_t ser;
	props_impl_t *impl = _props_impl_get(&handle->props, key);
	if(!impl)
	{
		return;
	}

	ser_atom_init(&ser);
	ser_atom_reset(&ser, &handle->forge);

	LV2_Atom_Forge_Ref ref = 1;

	props_set(&handle->props, &handle->forge, 0, key, &ref);

	const LV2_Atom_Event *ev = (const LV2_Atom_Event *)ser_atom_get(&ser);
	const LV2_Atom *atom = &ev->body;
	handle->writer(handle->controller, 0, lv2_atom_total_size(atom),
		handle->atom_eventTransfer, atom);

	ser_atom_deinit(&ser);
}

static void
_message_get(plughandle_t *handle, LV2_URID key)
{
	ser_atom_t ser;
	props_impl_t *impl = _props_impl_get(&handle->props, key);
	if(!impl)
	{
		return;
	}

	ser_atom_init(&ser);
	ser_atom_reset(&ser, &handle->forge);

	LV2_Atom_Forge_Ref ref = 1;

	props_get(&handle->props, &handle->forge, 0, key, &ref);

	const LV2_Atom_Event *ev = (const LV2_Atom_Event *)ser_atom_get(&ser);
	const LV2_Atom *atom = &ev->body;
	handle->writer(handle->controller, 0, lv2_atom_total_size(atom),
		handle->atom_eventTransfer, atom);

	ser_atom_deinit(&ser);
}

static inline void
_expose_header(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	const d2tk_coord_t frac [3] = { 1, 1, 1 };
	D2TK_BASE_LAYOUT(rect, 3, frac, D2TK_FLAG_LAYOUT_X_REL, lay)
	{
		const unsigned k = d2tk_layout_get_index(lay);
		const d2tk_rect_t *lrect = d2tk_layout_get_rect(lay);

		switch(k)
		{
			case 0:
			{
				d2tk_base_label(base, -1, "Open•Music•Kontrollers", 0.5f, lrect,
					D2TK_ALIGN_LEFT | D2TK_ALIGN_TOP);
			} break;
			case 1:
			{
				d2tk_base_label(base, -1, "N•O•T•E•S", 1.f, lrect,
					D2TK_ALIGN_CENTER | D2TK_ALIGN_TOP);
			} break;
			case 2:
			{
				d2tk_base_label(base, -1, "Version "NOTES_VERSION, 0.5f, lrect,
					D2TK_ALIGN_RIGHT | D2TK_ALIGN_TOP);
			} break;
		}
	}
}

static inline void
_expose_font_height(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	static const char lbl [] = "font-height•px";

	if(d2tk_base_spinner_int32_is_changed(base, D2TK_ID, rect,
		sizeof(lbl), lbl, 10, &handle->state.font_height, 25))
	{
		_message_set_key(handle, handle->urid_fontHeight);
		_update_font_height(handle);
	}
}

#ifdef _LV2_HAS_REQUEST_VALUE
static inline void
_expose_image_load(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	static const char lbl [] = "load";

	if(!handle->request)
	{
		return;
	}

	if(d2tk_base_button_label_is_changed(base, D2TK_ID, sizeof(lbl), lbl,
		D2TK_ALIGN_CENTERED, rect))
	{
		const LV2_URID key = handle->urid_image;
		const LV2_URID type = handle->forge.Path;

		const LV2UI_Request_Value_Status status = handle->request->request(
			handle->request->handle, key, type, NULL);

		if(status != LV2UI_REQUEST_VALUE_SUCCESS)
		{
			lv2_log_error(&handle->logger, "[%s] requestValue failed: %i", __func__, status);
		}
	}
}
#endif

static void
_update_image(plughandle_t *handle, const char *img, size_t img_len)
{
	ser_atom_t ser;
	ser_atom_init(&ser);
	ser_atom_reset(&ser, &handle->forge);

	lv2_atom_forge_path(&handle->forge, img, img_len);

	const LV2_Atom *atom = ser_atom_get(&ser);

	props_impl_t *impl = _props_impl_get(&handle->props, handle->urid_image);
	_props_impl_set(&handle->props, impl, atom->type, atom->size,
		LV2_ATOM_BODY_CONST(atom));

	ser_atom_deinit(&ser);

	_message_set_key(handle, handle->urid_image);
}

static inline void
_expose_image_clear(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	static const char lbl [] = "clear";
	static const char none [] = "";

	if(d2tk_base_button_label_is_changed(base, D2TK_ID, sizeof(lbl), lbl,
		D2TK_ALIGN_CENTERED, rect))
	{
		_update_image(handle, none, sizeof(none));
	}
}

static inline void
_expose_image_copy(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	static const char lbl [] = "copy";

	if(d2tk_base_button_label_is_changed(base, D2TK_ID, sizeof(lbl), lbl,
		D2TK_ALIGN_CENTERED, rect))
	{
		const int fd = open(handle->state.image, O_RDONLY);
		if(fd == -1)
		{
			lv2_log_error(&handle->logger, "[%s] open failed: %s", __func__,
				strerror(errno));
			return;
		}

		lseek(fd, 0, SEEK_SET);
		const size_t buf_len = lseek(fd, 0, SEEK_END);

		lseek(fd, 0, SEEK_SET);

		char *buf = alloca(buf_len + 1);
		if(!buf)
		{
			lv2_log_error(&handle->logger, "[%s] alloca failed", __func__);
			close(fd);
			return;
		}

		read(fd, buf, buf_len);
		close(fd);

		lv2_log_note(&handle->logger, "[%s] copying image: %zu", __func__, buf_len);

		if(  strcasestr(handle->state.image, ".jpg")
			|| strcasestr(handle->state.image, ".jpeg"))
		{
			d2tk_frontend_set_clipboard(dpugl, "image/jpeg", buf, buf_len);
		}
		else if(strcasestr(handle->state.image, ".png"))
		{
			d2tk_frontend_set_clipboard(dpugl, "image/png", buf, buf_len);
		}
		else
		{
			lv2_log_error(&handle->logger, "[%s] image type not supported", __func__);
		}
	}
}

static inline void
_expose_image(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	d2tk_base_image(base, -1, handle->state.image, rect, D2TK_ALIGN_CENTERED);
}

/* list of tested console editors:
 *
 * e3
 * joe
 * nano
 * vi
 * vis
 * vim
 * neovim
 * emacs
 * zile
 * mg
 * kakoune
 */

/* list of tested graphical editors:
 *
 * acme
 * adie
 * beaver
 * deepin-editor
 * gedit         (does not work properly)
 * gobby
 * howl
 * jedit         (does not work properly)
 * xed           (does not work properly)
 * leafpad
 * mousepad
 * nedit
 * notepadqq
 * pluma         (does not work properly)
 * sublime3      (needs to be started with -w)
 */

static inline void
_expose_term(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	char *editor = getenv("EDITOR");

	char *args [] = {
		editor ? editor : "vi",
		handle->template,
		NULL
	};

	D2TK_BASE_PTY(base, D2TK_ID, args,
		handle->font_height, rect, handle->reinit, pty)
	{
		const d2tk_state_t state = d2tk_pty_get_state(pty);
		const uint32_t max_red = d2tk_pty_get_max_red(pty);

		if(max_red != handle->max_red)
		{
			handle->max_red = max_red;
			d2tk_frontend_redisplay(handle->dpugl);
		}

		if(d2tk_state_is_close(state))
		{
			handle->done = 1;
		}
	}

	handle->reinit = false;
}

#ifdef _LV2_HAS_REQUEST_VALUE
static inline void
_expose_text_load(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	static const char lbl [] = "load";

	if(!handle->request)
	{
		return;
	}

	if(d2tk_base_button_label_is_changed(base, D2TK_ID, sizeof(lbl), lbl,
		D2TK_ALIGN_CENTERED, rect))
	{
		const LV2_URID key = handle->urid_text;
		const LV2_URID type = handle->forge.String;

		const LV2UI_Request_Value_Status status = handle->request->request(
			handle->request->handle, key, type, NULL);

		if(status != LV2UI_REQUEST_VALUE_SUCCESS)
		{
			lv2_log_error(&handle->logger, "[%s] requestValue failed: %i", __func__, status);
		}
	}
}
#endif

static void
_update_text(plughandle_t *handle, const char *txt, size_t txt_len)
{
	ser_atom_t ser;
	ser_atom_init(&ser);
	ser_atom_reset(&ser, &handle->forge);

	lv2_atom_forge_string(&handle->forge, txt, txt_len);

	const LV2_Atom *atom = ser_atom_get(&ser);

	props_impl_t *impl = _props_impl_get(&handle->props, handle->urid_text);
	_props_impl_set(&handle->props, impl, atom->type, atom->size,
		LV2_ATOM_BODY_CONST(atom));

	ser_atom_deinit(&ser);

	_message_set_key(handle, handle->urid_text);
}

static inline void
_expose_text_clear(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	static const char lbl [] = "clear";
	static const char none [] = "";

	if(d2tk_base_button_label_is_changed(base, D2TK_ID, sizeof(lbl), lbl,
		D2TK_ALIGN_CENTERED, rect))
	{
		_update_text(handle, none, sizeof(none) - 1);
	}
}

static inline void
_expose_text_copy(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_frontend_t *dpugl = handle->dpugl;
	d2tk_base_t *base = d2tk_frontend_get_base(dpugl);

	static const char lbl [] = "copy";

	if(d2tk_base_button_label_is_changed(base, D2TK_ID, sizeof(lbl), lbl,
		D2TK_ALIGN_CENTERED, rect))
	{
		d2tk_frontend_set_clipboard(dpugl, "UTF8_STRING",
			handle->state.text, strlen(handle->state.text) + 1);
	}
}

static inline void
_expose_upper_footer(plughandle_t *handle, const d2tk_rect_t *rect)
{
	const d2tk_coord_t frac [4] = { 1, 1, 1, 1 };
	D2TK_BASE_LAYOUT(rect, 4, frac, D2TK_FLAG_LAYOUT_X_REL, lay)
	{
		const unsigned k = d2tk_layout_get_index(lay);
		const d2tk_rect_t *lrect = d2tk_layout_get_rect(lay);

		switch(k)
		{
			case 0:
			{
#ifdef _LV2_HAS_REQUEST_VALUE
				_expose_text_load(handle, lrect);
#endif
			} break;
			case 1:
			{
				_expose_text_clear(handle, lrect);
			} break;
			case 2:
			{
				_expose_text_copy(handle, lrect);
			} break;
			case 3:
			{
				_expose_font_height(handle, lrect);
			} break;
		}
	}
}

static inline void
_expose_upper(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_base_t *base = d2tk_frontend_get_base(handle->dpugl);
	static const char lbl [] = "Text";

	D2TK_BASE_FRAME(base, rect, sizeof(lbl), lbl, frm)
	{
		const d2tk_rect_t *frect = d2tk_frame_get_rect(frm);

		const d2tk_coord_t frac [2] = { 0, handle->footer_height };
		D2TK_BASE_LAYOUT(frect, 2, frac, D2TK_FLAG_LAYOUT_Y_ABS, lay)
		{
			const unsigned k = d2tk_layout_get_index(lay);
			const d2tk_rect_t *lrect = d2tk_layout_get_rect(lay);

			switch(k)
			{
				case 0:
				{
					_expose_term(handle, lrect);
				} break;
				case 1:
				{
					_expose_upper_footer(handle, lrect);
				} break;
			}
		}
	}
}

static inline void
_expose_lower_footer(plughandle_t *handle, const d2tk_rect_t *rect)
{
	const d2tk_coord_t frac [4] = { 1, 1, 1, 1 };
	D2TK_BASE_LAYOUT(rect, 4, frac, D2TK_FLAG_LAYOUT_X_REL, lay)
	{
		const unsigned k = d2tk_layout_get_index(lay);
		const d2tk_rect_t *lrect = d2tk_layout_get_rect(lay);

		switch(k)
		{
			case 0:
			{
#ifdef _LV2_HAS_REQUEST_VALUE
				_expose_image_load(handle, lrect);
#endif
			} break;
			case 1:
			{
				_expose_image_clear(handle, lrect);
			} break;
			case 2:
			{
				_expose_image_copy(handle, lrect);
			} break;
			case 3:
			{
				// nothing
			} break;
		}
	}
}

static inline void
_expose_lower(plughandle_t *handle, const d2tk_rect_t *rect)
{
	d2tk_base_t *base = d2tk_frontend_get_base(handle->dpugl);
	static const char lbl [] = "Image";

	D2TK_BASE_FRAME(base, rect, sizeof(lbl), lbl, frm)
	{
		const d2tk_rect_t *frect = d2tk_frame_get_rect(frm);

		const d2tk_coord_t frac [2] = { 0, handle->footer_height };
		D2TK_BASE_LAYOUT(frect, 2, frac, D2TK_FLAG_LAYOUT_Y_ABS, lay)
		{
			const unsigned k = d2tk_layout_get_index(lay);
			const d2tk_rect_t *lrect = d2tk_layout_get_rect(lay);

			switch(k)
			{
				case 0:
				{
					_expose_image(handle, lrect);
				} break;
				case 1:
				{
					_expose_lower_footer(handle, lrect);
				} break;
			}
		}
	}
}

static int
_expose(void *data, d2tk_coord_t w, d2tk_coord_t h)
{
	plughandle_t *handle = data;
	d2tk_base_t *base = d2tk_frontend_get_base(handle->dpugl);
	const d2tk_rect_t rect = D2TK_RECT(0, 0, w, h);

	const d2tk_style_t *old_style = d2tk_base_get_style(base);
	d2tk_style_t style = *old_style;
	const uint32_t light = handle->max_red;
	const uint32_t dark = (light & ~0xff) | 0x7f;
	style.fill_color[D2TK_TRIPLE_ACTIVE]           = dark;
	style.fill_color[D2TK_TRIPLE_ACTIVE_HOT]       = light;
	style.fill_color[D2TK_TRIPLE_ACTIVE_FOCUS]     = dark;
	style.fill_color[D2TK_TRIPLE_ACTIVE_HOT_FOCUS] = light;
	style.text_stroke_color[D2TK_TRIPLE_HOT]       = light;
	style.text_stroke_color[D2TK_TRIPLE_HOT_FOCUS] = light;

	d2tk_base_set_style(base, &style);

	const d2tk_coord_t frac [3] = { handle->header_height, 0, 0 };
	D2TK_BASE_LAYOUT(&rect, 3, frac, D2TK_FLAG_LAYOUT_Y_ABS, lay)
	{
		const unsigned k = d2tk_layout_get_index(lay);
		const d2tk_rect_t *lrect = d2tk_layout_get_rect(lay);

		switch(k)
		{
			case 0:
			{
				_expose_header(handle, lrect);
			} break;
			case 1:
			{
				_expose_upper(handle, lrect);
			} break;
			case 2:
			{
				_expose_lower(handle, lrect);
			} break;
		}
	}

	d2tk_base_set_style(base, old_style);

	return 0;
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor,
	const char *plugin_uri,
	const char *bundle_path,
	LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
	{
		return NULL;
	}

	void *parent = NULL;
	LV2UI_Resize *host_resize = NULL;
	LV2_Options_Option *opts = NULL;
	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_UI__parent))
		{
			parent = features[i]->data;
		}
		else if(!strcmp(features[i]->URI, LV2_UI__resize))
		{
			host_resize = features[i]->data;
		}
		else if(!strcmp(features[i]->URI, LV2_URID__map))
		{
			handle->map = features[i]->data;
		}
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
		{
			handle->log = features[i]->data;
		}
		else if(!strcmp(features[i]->URI, LV2_OPTIONS__options))
		{
			opts = features[i]->data;
		}
#ifdef _LV2_HAS_REQUEST_VALUE
		else if(!strcmp(features[i]->URI, LV2_UI__requestValue))
		{
			handle->request = features[i]->data;
		}
#endif
	}

	if(!parent)
	{
		fprintf(stderr,
			"%s: Host does not support ui:parent\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	if(!handle->map)
	{
		fprintf(stderr,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	if(handle->log)
	{
		lv2_log_logger_init(&handle->logger, handle->map, handle->log);
	}

	lv2_atom_forge_init(&handle->forge, handle->map);

	handle->atom_eventTransfer = handle->map->map(handle->map->handle,
		LV2_ATOM__eventTransfer);
	handle->atom_beatTime = handle->map->map(handle->map->handle,
		LV2_ATOM__beatTime);
	handle->urid_text = handle->map->map(handle->map->handle,
		NOTES__text);
	handle->urid_fontHeight = handle->map->map(handle->map->handle,
		NOTES__fontHeight);
	handle->urid_image = handle->map->map(handle->map->handle,
		NOTES__image);

	if(!props_init(&handle->props, plugin_uri,
		defs, MAX_NPROPS, &handle->state, &handle->stash,
		handle->map, handle))
	{
		fprintf(stderr, "failed to initialize property structure\n");
		free(handle);
		return NULL;
	}

	handle->controller = controller;
	handle->writer = write_function;

	const d2tk_coord_t w = 400;
	const d2tk_coord_t h = 800;

	d2tk_pugl_config_t *config = &handle->config;
	config->parent = (uintptr_t)parent;
	config->bundle_path = bundle_path;
	config->min_w = w/2;
	config->min_h = h/2;
	config->w = w;
	config->h = h;
	config->fixed_size = false;
	config->fixed_aspect = false;
	config->expose = _expose;
	config->data = handle;

	handle->dpugl = d2tk_pugl_new(config, (uintptr_t *)widget);
	if(!handle->dpugl)
	{
		free(handle);
		return NULL;
	}

	const LV2_URID ui_scaleFactor = handle->map->map(handle->map->handle,
		LV2_UI__scaleFactor);

	// fall-back
	for(LV2_Options_Option *opt = opts;
		opt && (opt->key != 0) && (opt->value != NULL);
		opt++)
	{
		if( (opt->key == ui_scaleFactor) && (opt->type == handle->forge.Float) )
		{
			handle->scale = *(float*)opt->value;
		}
	}

	if(handle->scale == 0.f)
	{
		handle->scale = d2tk_frontend_get_scale(handle->dpugl);
	}

	handle->header_height = 32 * handle->scale;
	handle->footer_height = 32 * handle->scale;

	handle->state.font_height = 16;
	_update_font_height(handle);

	if(host_resize)
	{
		host_resize->ui_resize(host_resize->handle, w, h);
	}

	strncpy(handle->template, "/tmp/XXXXXX.md", sizeof(handle->template));
	handle->fd = mkstemps(handle->template, 3);
	if(handle->fd == -1)
	{
		free(handle);
		return NULL;
	}

	lv2_log_note(&handle->logger, "template: %s\n", handle->template);

	for(unsigned i = 0; i < MAX_NPROPS; i++)
	{
		const props_def_t *def = &defs[i];
		const LV2_URID urid = props_map(&handle->props, def->property);

		_message_get(handle, urid);
	}

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	d2tk_frontend_free(handle->dpugl);

	unlink(handle->template);
	close(handle->fd);
	free(handle);
}

static void
port_event(LV2UI_Handle instance, uint32_t index __attribute__((unused)),
	uint32_t size __attribute__((unused)), uint32_t protocol, const void *buf)
{
	plughandle_t *handle = instance;
	const LV2_Atom_Object *obj = buf;

	if(protocol != handle->atom_eventTransfer)
	{
		return;
	}

	ser_atom_t ser;
	ser_atom_init(&ser);
	ser_atom_reset(&ser, &handle->forge);

	LV2_Atom_Forge_Ref ref = 0;
	props_advance(&handle->props, &handle->forge, 0, obj, &ref);

	ser_atom_deinit(&ser);

	d2tk_frontend_redisplay(handle->dpugl);
}

static void
_file_read(plughandle_t *handle)
{
	lseek(handle->fd, 0, SEEK_SET);
	const size_t txt_len = lseek(handle->fd, 0, SEEK_END);

	lseek(handle->fd, 0, SEEK_SET);

	char *txt = alloca(txt_len + 1);
	if(!txt)
	{
		return;
	}

	read(handle->fd, txt, txt_len);
	txt[txt_len] = '\0';

	handle->hash = d2tk_hash(txt, txt_len);

	_update_text(handle, txt, txt_len);
}

static int
_idle(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	struct stat st;
	if(stat(handle->template, &st) == -1)
	{
		lv2_log_error(&handle->logger, "stat: %s\n", strerror(errno));
	}

	if( (st.st_mtime > handle->modtime) && (handle->modtime > 0) )
	{
		_file_read(handle);

		handle->modtime = st.st_mtime;
	}

	if(d2tk_frontend_step(handle->dpugl))
	{
		handle->done = 1;
	}

	return handle->done;
}

static const LV2UI_Idle_Interface idle_ext = {
	.idle = _idle
};

static int
_resize(LV2UI_Handle instance, int width, int height)
{
	plughandle_t *handle = instance;

	return d2tk_frontend_set_size(handle->dpugl, width, height);
}

static const LV2UI_Resize resize_ext = {
	.ui_resize = _resize
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_UI__idleInterface))
		return &idle_ext;
	else if(!strcmp(uri, LV2_UI__resize))
		return &resize_ext;
		
	return NULL;
}

static const LV2UI_Descriptor notes_ui= {
	.URI            = NOTES__ui,
	.instantiate    = instantiate,
	.cleanup        = cleanup,
	.port_event     = port_event,
	.extension_data = extension_data
};

LV2_SYMBOL_EXPORT const LV2UI_Descriptor*
lv2ui_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &notes_ui;

		default:
			return NULL;
	}
}
