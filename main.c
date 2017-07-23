/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include <libcmk/cmk.h>
#include <libcmk/cmk-icon-loader.h>
#include "pages.h"

static const CmkNamedColor GrapheneColors[] = {
	{"background", {84,  110, 122, 255}},
	{"foreground", {255, 255, 255, 204}},
	{"primary",    {208, 39,  39,  180}}, // vosred, #D02727
	{"hover",      {255, 255, 255, 40}},
	{"selected",   {255, 255, 255, 50}},
	{"error",      {120, 0,   0,   220}},
	NULL
};

CmkWidget *window;
static void next_page(ClutterActor *current)
{
	ClutterActor *next = clutter_actor_get_next_sibling(current);
	cmk_widget_fade_out(CMK_WIDGET(current), FALSE);
	cmk_widget_fade_in(CMK_WIDGET(next));
	cmk_focus_stack_pop(CMK_WIDGET(current));
	cmk_focus_stack_push(CMK_WIDGET(next));
}

static void prev_page(ClutterActor *current)
{
	ClutterActor *prev = clutter_actor_get_previous_sibling(current);
	cmk_widget_fade_out(CMK_WIDGET(current), FALSE);
	cmk_widget_fade_in(CMK_WIDGET(prev));
	cmk_focus_stack_pop(CMK_WIDGET(current));
	cmk_focus_stack_push(CMK_WIDGET(prev));
}

int main(int argc, char **argv)
{
	if(!cmk_init(&argc, &argv))
		return 1;

	CmkIconLoader *l = cmk_icon_loader_get_default();
	//cmk_icon_loader_lookup(l, "drive-removable-media", 512);
	cmk_icon_loader_load(l, cmk_icon_loader_lookup(l, "drive-harddisk", 256), 256, 2, TRUE);
	
	ClutterStage *stage;
	CmkWidget *window = cmk_window_new("Velt Installer", 600, 450, &stage);
	clutter_stage_set_user_resizable(stage, FALSE);
	cmk_widget_set_named_colors(window, GrapheneColors);
	g_signal_connect(window, "destroy", G_CALLBACK(clutter_main_quit), NULL);
	
	CmkWidget *home = page_home_new();
	cmk_widget_add_child(window, home);
	cmk_widget_bind_fill(home);
	g_signal_connect(home, "replace", G_CALLBACK(next_page), NULL);
	cmk_focus_stack_push(CMK_WIDGET(home));
	
	CmkWidget *ds = page_drive_select_new();
	clutter_actor_hide(CLUTTER_ACTOR(ds));
	cmk_widget_add_child(window, ds);
	cmk_widget_bind_fill(ds);
	g_signal_connect(ds, "replace", G_CALLBACK(next_page), NULL);

	CmkWidget *profile = page_profile_new();
	clutter_actor_hide(CLUTTER_ACTOR(profile));
	cmk_widget_add_child(window, profile);
	cmk_widget_bind_fill(profile);
	g_signal_connect(profile, "replace", G_CALLBACK(next_page), NULL);
	g_signal_connect(profile, "back", G_CALLBACK(prev_page), NULL);
	
	cmk_main();
	return 0;
}
