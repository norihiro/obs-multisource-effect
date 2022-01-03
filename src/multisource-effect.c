/*
OBS Multi Source Effect
Copyright (C) 2021 Norihiro Kamae <norihiro@nagater.net>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <stdio.h>
#include <obs-module.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <sys/stat.h>

#include "plugin-macros.generated.h"
#include "source_list.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define N_SRC 4

struct msrc
{
	obs_source_t *self;
	bool in_enum;
	bool in_callback;

	// properties and objects
	char *effect_name;
	gs_effect_t *effect;
	bool bypass_cache;
	uint64_t m_time;
	bool update_file;
	uint64_t last_checked;
	int n_src;
	char *src_name[N_SRC];
	obs_weak_source_t *src_ref[N_SRC];

	gs_texrender_t *texrender[N_SRC];
	bool rendered[N_SRC];
};

static uint64_t get_modified_timestamp(const char *filename)
{
	struct stat stats;

	if (os_stat(filename, &stats) != 0)
		return -1;

#if defined(_WIN32) || defined(__APPLE__)
	// It might cause an issue when the file is too frequenty updated.
	// TODO: Use GetFileTime for Windows.
	return stats.st_mtime;
#else
	return stats.st_mtim.tv_sec * 1000000000ULL + stats.st_mtim.tv_nsec;
#endif
}

static const char *msrc_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Multi Source Effect");
}

static void msrc_update(void *, obs_data_t *);

static void *msrc_create(obs_data_t *settings, obs_source_t *source)
{
	struct msrc *s = bzalloc(sizeof(struct msrc));
	s->self = source;
	msrc_update(s, settings);
	return s;
}

static void msrc_destroy(void *data)
{
	struct msrc *s = data;

	obs_enter_graphics();
	gs_effect_destroy(s->effect);
	for (int i = 0; i < N_SRC; i++)
		gs_texrender_destroy(s->texrender[i]);
	obs_leave_graphics();

	for (int i = 0; i < N_SRC; i++)
		obs_weak_source_release(s->src_ref[i]);

	bfree(s->effect_name);
	for (int i = 0; i < N_SRC; i++)
		bfree(s->src_name[i]);

	bfree(s);
}

static inline gs_effect_t *effect_create_without_cache(const char *file, char **error_string)
{
	char *file_string;
	gs_effect_t *effect = NULL;

	file_string = os_quick_read_utf8_file(file);
	if (!file_string) {
		blog(LOG_ERROR, "Could not load effect file '%s'", file);
		return NULL;
	}

	effect = gs_effect_create(file_string, file, error_string);
	bfree(file_string);

	return effect;
}

static void update_effect_internal(struct msrc *s, const char *effect_name)
{
	obs_enter_graphics();
	gs_effect_destroy(s->effect);
	if (s->bypass_cache) {
		s->m_time = get_modified_timestamp(effect_name);
		s->effect = effect_create_without_cache(effect_name, NULL);
	}
	else {
		s->effect = gs_effect_create_from_file(effect_name, NULL);
	}
	if (!s->effect)
		blog(LOG_ERROR, "Cannot load '%s'", effect_name);
	else
		blog(LOG_INFO, "Successfully loaded '%s'", effect_name);
	obs_leave_graphics();
}

static void update_effect(struct msrc *s, const char *effect_name)
{
	if (!effect_name || !*effect_name)
		return;

	if (s->effect_name && strcmp(effect_name, s->effect_name) == 0)
		return;

	bfree(s->effect_name);
	s->effect_name = bstrdup(effect_name);

	update_effect_internal(s, effect_name);
}

static void update_src(struct msrc *s, int ix, const char *src_name)
{
	if (!src_name || !*src_name)
		return;

	if (s->src_name[ix] && strcmp(src_name, s->src_name[ix]) == 0)
		return;

	bfree(s->src_name[ix]);
	s->src_name[ix] = bstrdup(src_name);

	obs_weak_source_release(s->src_ref[ix]);
	s->src_ref[ix] = NULL;
}

static void msrc_update(void *data, obs_data_t *settings)
{
	struct msrc *s = data;

	s->bypass_cache = obs_data_get_bool(settings, "bypass_cache");
	update_effect(s, obs_data_get_string(settings, "effect"));
	s->n_src = obs_data_get_int(settings, "n_src");
	if (s->n_src <= 0)
		s->n_src = 1;
	else if (s->n_src > N_SRC)
		s->n_src = N_SRC;

	for (int i = 0; i < N_SRC && i < s->n_src; i++) {
		char name[16];
		snprintf(name, sizeof(name), "src%d", i);
		update_src(s, i, obs_data_get_string(settings, name));
	}
}

static void msrc_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "n_src", 2);
}

#define FILE_FILTER "Effect Files (*.effect);;"

static void properties_add_source(struct msrc *s, obs_properties_t *pp, const char *name, const char *desc)
{
	obs_property_t *p;
	p = obs_properties_add_list(pp, name, desc, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	property_list_add_sources(p, s ? s->self : NULL);
}

static bool n_src_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	int n_src = obs_data_get_int(settings, "n_src");

	for (int i = 0; i < N_SRC; i++) {
		char name[16];
		snprintf(name, sizeof(name), "src%d", i);

		obs_property_t *p = obs_properties_get(props, name);
		obs_property_set_enabled(p, i < n_src);
	}
}

static obs_properties_t *msrc_get_properties(void *data)
{
	struct msrc *s = data;

	obs_properties_t *pp = obs_properties_create();
	obs_property_t *p;

	struct dstr path = {0};
	if (s && s->effect_name && *s->effect_name)
		dstr_copy(&path, s->effect_name);
	else {
		dstr_init_move_array(&path, obs_module_file("examples"));
		dstr_cat_ch(&path, '/');
	}
	obs_properties_add_path(pp, "effect", obs_module_text("Effect file"), OBS_PATH_FILE, FILE_FILTER, path.array);
	dstr_free(&path);

	obs_properties_add_bool(pp, "bypass_cache", obs_module_text("Do not use cache"));

	p = obs_properties_add_int(pp, "n_src", obs_module_text("Number of sources"), 1, N_SRC, 1);
	obs_property_set_modified_callback(p, n_src_modified);

	for (int i = 0; i < N_SRC; i++) {
		char name[16];
		snprintf(name, sizeof(name), "src%d", i);
		struct dstr desc = {0};
		dstr_printf(&desc, obs_module_text("Source %d"), i);

		properties_add_source(s, pp, name, desc.array);

		dstr_free(&desc);
	}

	return pp;
}

static inline obs_source_t *msrc_get_source(struct msrc *s, int ix)
{
	if (s->src_ref[ix])
		return obs_weak_source_get_source(s->src_ref[ix]);

	return NULL;
}

static uint32_t msrc_get_width(void *data)
{
	struct msrc *s = data;
	if (s->in_callback)
		return 0;
	s->in_callback = true;

	uint32_t ret = 0;
	for (int i = 0; i < N_SRC && i < s->n_src; i++) {
		obs_source_t *src = msrc_get_source(s, i);
		if (!src)
			continue;
		uint32_t n = obs_source_get_width(src);
		if (n > ret)
			ret = n;
		obs_source_release(src);
	}
	s->in_callback = false;
	return ret;
}

static uint32_t msrc_get_height(void *data)
{
	struct msrc *s = data;
	if (s->in_callback)
		return 0;
	s->in_callback = true;

	uint32_t ret = 0;
	for (int i = 0; i < N_SRC && i < s->n_src; i++) {
		obs_source_t *src = msrc_get_source(s, i);
		if (!src)
			continue;
		uint32_t n = obs_source_get_height(src);
		if (n > ret)
			ret = n;
		obs_source_release(src);
	}
	s->in_callback = false;
	return ret;
}

static void msrc_enum_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	struct msrc *s = data;

	if (s->in_enum)
		return;

	s->in_enum = true;

	for (int i = 0; i < N_SRC && i < s->n_src; i++) {
		obs_source_t *src = msrc_get_source(s, i);
		if (!src)
			continue;
		enum_callback(s->self, src, param);
		obs_source_release(src);
	}

	s->in_enum = false;
}

static void msrc_render(void *data, gs_effect_t *effect)
{
	struct msrc *s = data;
	UNUSED_PARAMETER(effect);

	if (!s->effect)
		return;

	uint32_t ww = 0; // = msrc_get_width(s);
	uint32_t hh = 0; // = msrc_get_height(s);

	gs_blend_state_push();
	gs_reset_blend_state();

	for (int i = 0; i < N_SRC && i < s->n_src; i++) {
		obs_source_t *src = msrc_get_source(s, i);
		if (!src)
			continue;

		uint32_t w = obs_source_get_width(src);
		uint32_t h = obs_source_get_height(src);

		if (ww < w)
			ww = w;
		if (hh < h)
			hh = h;

		if (s->rendered[i]) {
			obs_source_release(src);
			continue;
		}
		s->rendered[i] = true;

		if (!s->texrender[i])
			s->texrender[i] = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
		gs_texrender_reset(s->texrender[i]);
		if (!gs_texrender_begin(s->texrender[i], w, h)) {
			obs_source_release(src);
			continue;
		}

		struct vec4 background;
		vec4_zero(&background);

		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);

		obs_source_video_render(src);

		gs_texrender_end(s->texrender[i]);

		obs_source_release(src);
	}

	gs_eparam_t *n_src_param = gs_effect_get_param_by_name(s->effect, "n_src");
	if (n_src_param)
		gs_effect_set_int(n_src_param, s->n_src);

	for (int i = 0; i < N_SRC && i < s->n_src; i++) {
		if (!s->rendered[i])
			continue;
		gs_texture_t *tex = gs_texrender_get_texture(s->texrender[i]);
		char name[16];
		snprintf(name, sizeof(name), "src%d", i);
		gs_effect_set_texture(gs_effect_get_param_by_name(s->effect, name), tex);
	}

	while (gs_effect_loop(s->effect, "Draw")) {
		gs_draw_sprite(NULL, 0, ww, hh);
	}

	gs_blend_state_pop();
}

static void check_reload_mtime(struct msrc *s)
{
	if (s->update_file) {
		update_effect_internal(s, s->effect_name);
		s->update_file = false;
	}

	if (os_gettime_ns() - s->last_checked >= 1000000000) {
		uint64_t t = get_modified_timestamp(s->effect_name);
		s->last_checked = os_gettime_ns();

		if (t != s->m_time)
			s->update_file = true;
	}
}

static void msrc_tick(void *data, float second)
{
	struct msrc *s = data;
	UNUSED_PARAMETER(second);

	if (s->bypass_cache && obs_source_showing(s->self))
		check_reload_mtime(s);

	for (int i = 0; i < N_SRC; i++)
		s->rendered[i] = false;

	for (int i = 0; i < N_SRC && i < s->n_src; i++) {
		if (!s->src_name[i] || !*s->src_name[i])
			continue;

		bool fail = false;
		obs_source_t *src = msrc_get_source(s, i);

		if (!src)
			fail = true;

		const char *name = obs_source_get_name(src);
		if (!name || s->src_name[i])
			fail = true;
		else if (strcmp(name, s->src_name[i]) != 0)
			fail = true;

		obs_source_release(src);

		if (fail) {
			obs_weak_source_release(s->src_ref[i]);
			src = obs_get_source_by_name(s->src_name[i]);
			s->src_ref[i] = obs_source_get_weak_source(src);
			obs_source_release(src);
		}
	}
}

static struct obs_source_info msrc_info = {
	.id = "net.nagater.obs-multisource-effect",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_DO_NOT_DUPLICATE,
	.get_name = msrc_get_name,
	.create = msrc_create,
	.destroy = msrc_destroy,
	.update = msrc_update,
	.get_defaults = msrc_get_defaults,
	.get_properties = msrc_get_properties,
	.get_width = msrc_get_width,
	.get_height = msrc_get_height,
	.enum_active_sources = msrc_enum_sources,
	.video_render = msrc_render,
	.video_tick = msrc_tick,
};

bool obs_module_load(void)
{
	obs_register_source(&msrc_info);
	blog(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload()
{
	blog(LOG_INFO, "plugin unloaded");
}
