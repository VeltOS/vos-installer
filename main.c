/*
 * This file is part of vos-installer.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include <libcmk/cmk.h>
#include "pages.h"

static const CmkNamedColor GrapheneColors[] = {
	{"background", {84,  110, 122, 255}},
	{"foreground", {255, 255, 255, 204}},
	{"hover",      {255, 255, 255, 40}},
	{"selected",   {255, 255, 255, 25}},
	{"accent",     {208, 39,  39,  255}}, // vosred, #D02727
	NULL
};

static const float GrapheneBevelRadius = 3.0;
static const float GraphenePadding = 10.0;

CmkWidget *window;
static void next_page(ClutterActor *current)
{
	ClutterActor *next = clutter_actor_get_next_sibling(current);
	clutter_actor_hide(current);
	clutter_actor_show(next);
}

int main(int argc, char **argv)
{
	cmk_auto_dpi_scale();
	if(!cmk_init(&argc, &argv))
		return 1;
	
	CmkWidget *style = cmk_widget_get_style_default();
	cmk_widget_style_set_colors(style, GrapheneColors);
	cmk_widget_style_set_bevel_radius(style, GrapheneBevelRadius);
	cmk_widget_style_set_padding(style, GraphenePadding);
	
	CmkWidget *window = cmk_window_new("Velt Installer", 800, 600, NULL);
	g_signal_connect(window, "destroy", G_CALLBACK(clutter_main_quit), NULL);
	
	CmkWidget *home = page_home_new();
	cmk_widget_add_child(window, home);
	cmk_widget_bind_fill(home);
	g_signal_connect(home, "replace", G_CALLBACK(next_page), NULL);
	
	CmkWidget *ds = page_drive_select_new();
	cmk_widget_add_child(window, ds);
	cmk_widget_bind_fill(ds);
	clutter_actor_hide(CLUTTER_ACTOR(ds));
	g_signal_connect(home, "replace", G_CALLBACK(next_page), NULL);
	
	cmk_main();
	return 0;
}

//int main(int argc, char **argv)
//{
//	g_setenv("GDK_SCALE", "1", TRUE);
//	g_setenv("GDK_DPU_SCALE", "1", TRUE);
//	g_setenv("CLUTTER_SCALE", "1", TRUE);
//	gdk_set_allowed_backends("wayland");
//	gdk_init(&argc, &argv);
//
//	GdkWindowAttr attr;
//	attr.event_mask =GDK_ALL_EVENTS_MASK |GDK_EXPOSURE_MASK |
//                     GDK_BUTTON_PRESS_MASK |
//                     GDK_BUTTON_RELEASE_MASK |
//                     GDK_BUTTON_MOTION_MASK |
//                     GDK_KEY_PRESS_MASK |
//                     GDK_KEY_RELEASE_MASK |
//                     GDK_ENTER_NOTIFY_MASK |
//                     GDK_LEAVE_NOTIFY_MASK |
//                     GDK_FOCUS_CHANGE_MASK |
//                     GDK_STRUCTURE_MASK;
//	attr.wclass = GDK_INPUT_OUTPUT;
//	attr.window_type = GDK_WINDOW_TOPLEVEL;
//	attr.x = 10;
//	attr.y = 10;
//	attr.width = 10;
//	attr.height = 10;
//	
//	GdkWindow *root = gdk_screen_get_root_window(gdk_display_get_default_screen(gdk_display_get_default()));
//	g_message("hoi %p", root);
//	GdkWindow *w = gdk_window_new(NULL, &attr, GDK_WA_X | GDK_WA_Y);
//	
//	//GdkColor c = {0};
//	//c.red = 65535;
//	//gdk_window_set_background(w, &c);
//	
//	
//	//gdk_wayland_window_set_use_custom_surface(w, TRUE);
//	struct wl_surface *s = gdk_wayland_window_get_wl_surface(w);
//	g_message("s: %p", s);
//	
//	gdk_window_move_resize(w, 50, 50, 50, 50);
//	gdk_window_show(w);
//	
//	
//	
//	clutter_wayland_set_display(gdk_wayland_display_get_wl_display(gdk_display_get_default()));
//	clutter_wayland_disable_event_retrieval();
//	clutter_set_windowing_backend("wayland");
//	clutter_init(NULL, NULL);
//	
//	ClutterActor *stage = clutter_stage_new();
//	clutter_wayland_stage_set_wl_surface(CLUTTER_STAGE(stage), s);
//	
//	ClutterColor c = {255, 0, 0, 255};
//	clutter_actor_set_background_color(stage, &c);
//	
//	clutter_actor_show(stage);
//	// BUG FIX: GDK sets the surface's userdata to the GdkWindow, but clutter
//	// overwrites it (even though it knows its a custom surface >.>), which
//	// makes GDK segfault. So... fix that. It must be called after
//	// clutter_actor_show on the stage, but hopefully not again in the future?
//	// The problem comes from clutter_stage_wayland_realize in clutter-stage-wayland.c
//	wl_surface_set_user_data(s, w);
//
//	
//	clutter_actor_set_size(stage, 100, 100);
//	clutter_actor_set_position(stage, 10, 10);
//	//clutter_stage_set_user_resizable(stage, TRUE);
//	
//	//gdk_window_begin_move_drag(w, 0, 0, 0, GDK_CURRENT_TIME);
//	
//	ClutterActor *l = CLUTTER_ACTOR(cmk_label_new_with_text("The quick brown fox"));
//	clutter_actor_add_child(CLUTTER_ACTOR(stage), l);
//	clutter_actor_set_position(l, 0, 0);
//	clutter_actor_set_reactive(l, TRUE);
//	clutter_text_set_editable(cmk_label_get_clutter_text(CMK_LABEL(l)), TRUE);
//	clutter_text_set_selectable(cmk_label_get_clutter_text(CMK_LABEL(l)), TRUE);
//	
//	
//	
//	GMainLoop *loop = g_main_loop_new(NULL, TRUE);
//	g_main_loop_run(loop);
//	gdk_flush();
//	g_main_loop_unref(loop);	
//	return 0;
//}

// All this crap from testing Cmk client stuff

////void cairo_surface_set_device_scale(cairo_surface_t *t, double x, double y)
////{
////	g_message("set device scale");
////}
//
////gint
////gtk_widget_get_scale_factor (GtkWidget *widget)
////{
////	return 100;
////}
////
////gint gdk_monitor_get_scale_factor(GdkMonitor *monitor)
////{
////	g_message("get scale factor");
////	return 100;
////}
////
////gint gdk_window_get_scale_factor(GdkWindow *window)
////{
////	g_message("get scale factor");
////	return 100;
////}
//
//static void on_global_scale_changed(CmkIconLoader *iconLoader)
//{
//	CmkWidget *style = cmk_widget_get_style_default();
//	gfloat scale = cmk_icon_loader_get_scale(iconLoader);
//	// TODO: Fix. cogl doesn't handle wayland scaling properly, so
//	// just set cmk's scaling to 1. Everything will look blurry...
//	if(clutter_check_windowing_backend(CLUTTER_WINDOWING_WAYLAND))
//		scale = 1;
//	cmk_widget_style_set_scale_factor(style, 1);
//}
//
//static gboolean
//onpaint (GtkWidget *widget,
//         cairo_t   *cr,
//         gpointer   data)
//{
//	//cairo_surface_t *s = cairo_get_target(cr);
//	//cairo_surface_type_t t = cairo_surface_get_type(s);
//	////g_message("type: %i", t == CAIRO_SURFACE_TYPE_IMAGE);
//	//int width = cairo_image_surface_get_width(s);
//	//int height = cairo_image_surface_get_height(s);
//	//g_message("size: %i, %i", width, height);
//	
//	//	cairo_set_source_rgba(cr, 1, 0, 0, 1);
//	cairo_paint(cr);
//	return FALSE;
//}
//
//#define GTK_TYPE_EMBEDDER                 (gtk_embedder_get_type ())
//#define GTK_EMBEDDER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_EMBEDDER, GtkEmbedder))
//#define GTK_EMBEDDER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_EMBEDDER, GtkEmbedderClass))
//#define GTK_IS_EMBEDDER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_EMBEDDER))
//#define GTK_IS_EMBEDDER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_EMBEDDER))
//#define GTK_EMBEDDER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_EMBEDDER, GtkEmbedderClass))
//
//typedef struct _GtkEmbedder             GtkEmbedder;
//typedef struct _GtkEmbedderClass        GtkEmbedderClass;
//
//struct _GtkEmbedder
//{
//  /*< private >*/
//  GtkWidget bin;
//	GdkWindow *event_window;
//};
//
//struct _GtkEmbedderClass
//{
//  GtkWidgetClass        parent_class;
//};
//
//G_DEFINE_TYPE(GtkEmbedder, gtk_embedder, GTK_TYPE_WIDGET)
//
//
//static GtkEmbedder * gtk_embedder_new(void)
//{
//	return GTK_EMBEDDER(g_object_new(GTK_TYPE_EMBEDDER, NULL));
//}
//
//static GdkWindow *
//gdk_window_new_input (GdkWindow          *parent,
//                      gint                event_mask,
//                      const GdkRectangle *position)
//{
//  GdkWindowAttr attr;
//
//  g_return_val_if_fail (GDK_IS_WINDOW (parent), NULL);
//
//  attr.event_mask = event_mask;
//  attr.wclass = GDK_INPUT_ONLY;
//  attr.x = position->x;
//  attr.y = position->y;
//  attr.width = position->width;
//  attr.height = position->height;
//  attr.window_type = GDK_WINDOW_CHILD;
//
//  return gdk_window_new (parent, &attr, GDK_WA_X | GDK_WA_Y);
//}
//
//#include <wayland-client.h>
//#include <gdk/gdkwayland.h>
//
//static void embedder_on_realize(GtkWidget *self_)
//{
//	GtkEmbedder *self = GTK_EMBEDDER(self_);
//
//
//	GTK_WIDGET_CLASS(gtk_embedder_parent_class)->realize(self_);
//	g_message("realize");
//
//	GtkAllocation allocation;
//	gtk_widget_get_allocation (self_, &allocation);
//	
//	self->event_window = gdk_window_new_input (gtk_widget_get_window (self_),
//                                             gtk_widget_get_events (self_)
//                                             | GDK_BUTTON_PRESS_MASK
//                                             | GDK_BUTTON_RELEASE_MASK
//                                             | GDK_TOUCH_MASK
//                                             | GDK_ENTER_NOTIFY_MASK
//                                             | GDK_LEAVE_NOTIFY_MASK,
//                                             &allocation);
//	//gdk_wayland_window_set_use_custom_surface(self->event_window);
//	gtk_widget_register_window(self_, self->event_window);
//	
//	struct wl_surface *surf = gdk_wayland_window_get_wl_surface(gdk_window_get_toplevel(self->event_window));
//	g_message("surf: %p", surf);
//	
//	ClutterActor *stage = clutter_stage_new();
//	clutter_wayland_stage_set_wl_surface(CLUTTER_STAGE(stage), surf);
//	g_message("%i, %i", allocation.width, allocation.height);
//
//	clutter_actor_set_size(stage, allocation.width, allocation.height);
//	ClutterColor x = {255,0,255,255};
//	clutter_actor_set_background_color(stage, &x);
//	clutter_actor_show(stage);
//	//gtk_widget_queue_draw(self_);
//	
//	//CoglOnscreen *onscreen = cogl_onscreen_new (backend->cogl_context,
//}
//
//static void gtk_embedder_class_init(GtkEmbedderClass *class)
//{
//	GTK_WIDGET_CLASS(class)->realize = embedder_on_realize;
//}
//
//static void gtk_embedder_init(GtkEmbedder *self)
//{
//	gtk_widget_set_can_focus(GTK_WIDGET(self), TRUE);
//	gtk_widget_set_receives_default(GTK_WIDGET(self), TRUE);
//	gtk_widget_set_has_window(GTK_WIDGET(self), FALSE);
//	gtk_widget_set_app_paintable(GTK_WIDGET(self), TRUE);
//}
//
//static void alloc(ClutterActor *a)
//{
//	gfloat w,h;
//	clutter_actor_get_size(a, &w, &h);
//	g_message("size: %f, %f", w, h);
//}
//
//extern void asdf(void);



//int mainx2(int argc, char **argv)
//{
//	return;
//	//cmk_init();
//	
//	clutter_set_windowing_backend(CLUTTER_WINDOWING_X11);
//	cmk_disable_system_guiscale();
//
//	//if(!gtk_init_check(&argc, &argv))
//	//	return CLUTTER_INIT_ERROR_UNKNOWN;
//
//	//clutter_wayland_disable_event_retrieval();
//	//clutter_wayland_set_display(gdk_wayland_display_get_wl_display(gdk_display_get_default()));
//	//clutter_disable_accessibility();
//	
//	clutter_init(&argc, &argv);
//
//	ClutterActor *stage = clutter_stage_new();
//	g_message("about to show");
//
//	clutter_actor_show(stage);
//	struct wl_surface *s = clutter_wayland_stage_get_wl_surface(CLUTTER_STAGE(stage));
//	//wl_surface_set_buffer_scale(s, 2);
//	g_message("surf: %p", s);
//
//	//GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
//	//g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
//	//gtk_widget_show(window);
//
//	//GtkClutterEmbed *e = GTK_CLUTTER_EMBED(gtk_clutter_embed_new());
//	//GtkEmbedder *e = gtk_embedder_new();
//	//gtk_widget_show(GTK_WIDGET(e));
//	//gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(e));
//
//	//ClutterActor *a = gtk_clutter_embed_get_stage(e);
//	//ClutterColor x = {255,0,255,255};
//	//clutter_actor_set_background_color(a, &x);
//
//	////g_signal_connect(a, "allocation-changed", G_CALLBACK(alloc), NULL); 
//
//	ClutterActor *t = clutter_text_new();
//	clutter_text_set_text(t, "The quick brown fox");
//	clutter_actor_add_child(stage, t);
//	clutter_actor_set_position(t, 0, 0);
//	clutter_actor_set_reactive(t, TRUE);
//	g_signal_connect(t, "button-press-event", G_CALLBACK(asdf), NULL);
//
//	//CoglContext *ctx = clutter_backend_get_cogl_context(clutter_get_default_backend());
//	////CoglFramebuffer *fb = cogl_get_draw_framebuffer ();
//
//	//CoglTexture2D *tex = cogl_texture_2d_new_with_size(ctx, 300, 300);
//	//CoglFramebuffer *fb = cogl_offscreen_new_to_texture(tex);
//	//cogl_framebuffer_set_viewport(fb, 0, 0, 300, 300);
//	//cogl_set_framebuffer(fb);
//	//cogl_push_framebuffer(fb);
//
//	
//	//GtkWidget *area = gtk_drawing_area_new();
//	//gtk_widget_set_size_request (area, 100, 100);
//	//gtk_container_add(GTK_CONTAINER(window), area);
//	//
//	//g_signal_connect(area, "draw", G_CALLBACK(onpaint), NULL);
//	//gtk_widget_show(area);
//	
//	gtk_main();
//	return 0;
//}


//int mainx(int argc, char **argv)
//{
//	//g_unsetenv("CLUTTER_BACKEND");
//	// Don't use GDK backend. It looks like crap on Wayland
//	// (even more like crap than the regular wayland backend)
//	//clutter_set_windowing_backend("x11,wayland");
//	clutter_set_windowing_backend("wayland");
//	cmk_disable_system_guiscale();
//	g_setenv("GDK_SCALE", "1", TRUE);
//	g_setenv("GDK_DPI_SCALE", "1", TRUE);
//
//	if(clutter_init(&argc, &argv) != CLUTTER_INIT_SUCCESS)
//		return 1;
//
//	//GdkScreen *screen = gdk_screen_get_default();
//	////GdkX11Screen *sx11 = GDK_X11_SCREEN(screen);
//
//	//g_message("sfx %i", gdk_monitor_get_scale_factor(gdk_display_get_monitor(gdk_display_get_default(), 0)));
//
//	//CmkWidget *bg;
//	//{
//	//CmkWidget *style = cmk_widget_get_style_default();
//	//CmkIconLoader *iconLoader = cmk_icon_loader_get_default();
//	//g_signal_connect(iconLoader, "notify::scale", G_CALLBACK(on_global_scale_changed), NULL);
//	//on_global_scale_changed(iconLoader);
//
//	//gfloat scale = cmk_widget_style_get_scale_factor(style);
//
//	////ClutterActor *stage = clutter_stage_new();
//
//	////clutter_stage_set_title(CLUTTER_STAGE(stage), title);
//	////clutter_stage_set_user_resizable(CLUTTER_STAGE(stage), TRUE);
//	////clutter_stage_set_minimum_size(CLUTTER_STAGE(stage), 100*scale, 100*scale);
//	////clutter_stage_set_no_clear_hint(CLUTTER_STAGE(stage), TRUE);
//	////clutter_actor_set_size(stage, width*scale, height*scale);
//
//	//GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
//	//GtkWidget *e = gtk_clutter_embed_new();	
//	//ClutterActor *stage = gtk_clutter_embed_get_stage(GTK_CLUTTER_EMBED(e));
//	//gtk_container_add(GTK_CONTAINER(w), e);
//	//gtk_widget_show(e);
//	//gtk_widget_show(w);
//	//g_signal_connect(w, "destroy", G_CALLBACK(gtk_main_quit), NULL);
//
//
//	////wltest();
//	////wl_surface_set_buffer_scale(surface, 2);
//	////clutter_wayland_stage_set_wl_surface(stage, surface);
//
//	//clutter_actor_show(stage);
//
//	//GdkWindow *gw = gdk_screen_get_root_window(gdk_screen_get_default());
//	//g_message("window scale factor: %i", gdk_window_get_scale_factor(gw));
//	//	
//	//bg = cmk_widget_new();
//	//cmk_widget_set_background_color_name(bg, "background");
//	//cmk_widget_set_draw_background_color(bg, TRUE);
//	//clutter_actor_add_child(stage, CLUTTER_ACTOR(bg));
//
//	//// TODO: This glitches during maximize/unmaximize. How to fix?
//	//clutter_actor_add_constraint(CLUTTER_ACTOR(bg), clutter_bind_constraint_new(CLUTTER_ACTOR(stage), CLUTTER_BIND_ALL, 0));
//	//}
//
//	//CmkWidget *window = bg;
//
//	
//	
//	//CmkWidget *window = cmk_window_new("VeltOS Installer", 800, 500);
//	g_signal_connect(window, "destroy", G_CALLBACK(clutter_main_quit), NULL);
//
//	CmkWidget *style = cmk_widget_get_style_default();
//	cmk_widget_style_set_color(style, "background", &GrapheneColors[0]);
//	cmk_widget_style_set_color(style, "foreground", &GrapheneColors[1]);
//	cmk_widget_style_set_color(style, "accent", &GrapheneColors[4]);
//	cmk_widget_style_set_color(style, "hover", &GrapheneColors[2]);
//	cmk_widget_style_set_color(style, "selected", &GrapheneColors[3]);
//	cmk_widget_style_set_bevel_radius(style, GrapheneBevelRadius);
//	cmk_widget_style_set_padding(style, GraphenePadding);
//	g_object_unref(style);
//	
//	CmkWidget *home = page_home_new();
//	g_signal_connect(home, "replace", G_CALLBACK(next_page), NULL);
//	clutter_actor_add_child(CLUTTER_ACTOR(window), CLUTTER_ACTOR(home));
//	clutter_actor_add_constraint(CLUTTER_ACTOR(home), clutter_bind_constraint_new(CLUTTER_ACTOR(window), CLUTTER_BIND_ALL, 0));
//	
//	CmkWidget *ds = page_drive_select_new();
//	g_signal_connect(home, "replace", G_CALLBACK(next_page), NULL);
//	clutter_actor_add_child(CLUTTER_ACTOR(window), CLUTTER_ACTOR(ds));
//	clutter_actor_add_constraint(CLUTTER_ACTOR(ds), clutter_bind_constraint_new(CLUTTER_ACTOR(window), CLUTTER_BIND_ALL, 0));
//	clutter_actor_hide(CLUTTER_ACTOR(ds));
//
//	clutter_main();
//	return 0;
//}
