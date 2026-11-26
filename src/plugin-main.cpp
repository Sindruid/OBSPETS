/*
OBSPETS: a pet for OBS Studio that reacts to Twitch and Kick.
Copyright (C) 2026 Sindruid

This program is free software; you can redistribute it and/or modify it under the terms of the GNU
General Public License as published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If
not, see <https://www.gnu.org/licenses/>
*/

#include "chat-server.hpp"
#include "chat-source.hpp"
#include "editor.hpp"
#include "hub.hpp"
#include "pet.hpp"
#include "settings.hpp"
#include "updater.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <cstring>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

using namespace obspet;

MODULE_EXPORT const char *obs_module_description(void)
{
	return "OBSPETS: a pet that lives in your scene and reacts to your Twitch and Kick chat and alerts.";
}

// The plugin never adds sources by itself; the streamer adds them from Sources or the Tools menu.

// Tools, "OBSPETS: Add a pet to this scene": opens the properties of the pet already in the scene,
// or adds the existing pet from another scene, or a new one, sized to the canvas. The scene is the
// one being edited (the preview in Studio Mode).
static void add_pet(void *)
{
	bool studio = obs_frontend_preview_program_mode_active();
	obs_source_t *scene_src = studio ? obs_frontend_get_current_preview_scene() : obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(scene_src);
	if (!scene) {
		obs_source_release(scene_src);
		return;
	}
	obs_source_t *pet = nullptr;
	obs_enum_sources(
		[](void *p, obs_source_t *s) {
			const char *id = obs_source_get_unversioned_id(s);
			if (id && strcmp(id, "obspet") == 0) {
				*static_cast<obs_source_t **>(p) = obs_source_get_ref(s);
				return false;
			}
			return true;
		},
		&pet);
	if (!pet) {
		std::string name = "OBSPETS";
		for (int i = 2;; i++) {
			obs_source_t *e = obs_get_source_by_name(name.c_str());
			if (!e)
				break;
			obs_source_release(e);
			name = "OBSPETS " + std::to_string(i);
		}
		pet = obs_source_create("obspet", name.c_str(), nullptr, nullptr);
	}
	if (pet && !obs_scene_find_source_recursive(scene, obs_source_get_name(pet)))
		obs_scene_add(scene, pet);
	if (pet)
		obs_frontend_open_source_properties(pet);
	obs_source_release(pet);
	obs_source_release(scene_src);
}

static void on_event(enum obs_frontend_event ev, void *)
{
	switch (ev) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		updater_loaded();
		break;
	// the Update card waits while any of these runs: updating closes OBS
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
	case OBS_FRONTEND_EVENT_RECORDING_STARTING:
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
		updater_output_changed(ev == OBS_FRONTEND_EVENT_STREAMING_STARTING || ev == OBS_FRONTEND_EVENT_RECORDING_STARTING ||
				       ev == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING);
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		updater_stop();
		Hub::get().stop();
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	register_pet_source();
	register_chat_source();
	Hub::get().start();
	updater_start();
	obs_frontend_add_event_callback(on_event, nullptr);
	obs_frontend_add_tools_menu_item("OBSPETS: Add a pet to this scene", add_pet, nullptr);
	obs_frontend_add_tools_menu_item("OBSPETS: Settings", [](void *) { open_settings(); }, nullptr);
	obs_frontend_add_tools_menu_item("OBSPETS: Pet editor", [](void *) { open_editor(""); }, nullptr);
	obs_log(LOG_INFO, "loaded (version %s), backend %s", PLUGIN_VERSION, Hub::backend().c_str());
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_event, nullptr);
	updater_stop();
	Hub::get().stop();
	chat_server::stop();
	obs_log(LOG_INFO, "unloaded");
}
