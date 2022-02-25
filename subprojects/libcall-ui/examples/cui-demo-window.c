/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "cui-demo-call.h"
#include "cui-demo-window.h"

#include <glib/gi18n.h>


struct _CuiDemoWindow
{
  HdyApplicationWindow parent_instance;

  GtkImage            *theme_variant_image;
  HdyLeaflet          *content_box;
  GtkButton           *incoming_call;

  CuiCallDisplay      *call_display;
  CuiDialpad          *dialpad;
  CuiKeypad           *keypad;
  CuiDemoCall         *call1;
};

G_DEFINE_TYPE (CuiDemoWindow, cui_demo_window, HDY_TYPE_APPLICATION_WINDOW)


static void
theme_variant_button_clicked_cb (CuiDemoWindow *self)
{
  GtkSettings *settings = gtk_settings_get_default ();
  gboolean prefer_dark_theme;

  g_object_get (settings, "gtk-application-prefer-dark-theme", &prefer_dark_theme, NULL);
  g_object_set (settings, "gtk-application-prefer-dark-theme", !prefer_dark_theme, NULL);
}


static gboolean
prefer_dark_theme_to_icon_name_cb (GBinding     *binding,
                                   const GValue *from_value,
                                   GValue       *to_value,
                                   gpointer      user_data)
{
  g_value_set_string (to_value,
                      g_value_get_boolean (from_value) ? "light-mode-symbolic" :
                                                         "dark-mode-symbolic");

  return TRUE;
}


static void
back_clicked_cb (CuiDemoWindow *self)
{
  hdy_leaflet_navigate (self->content_box, HDY_NAVIGATION_DIRECTION_BACK);
}


static void
on_call_state_changed (CuiDemoCall *call, GParamSpec *pspec, gpointer user_data)
{
  CuiDemoWindow *self = CUI_DEMO_WINDOW (user_data);
  CuiCallState state = cui_call_get_state (CUI_CALL (call));

  g_return_if_fail (call == self->call1);

  if (state == CUI_CALL_STATE_DISCONNECTED)
    g_clear_object (&self->call1);

  gtk_widget_set_sensitive (GTK_WIDGET (self->incoming_call),
                            state == CUI_CALL_STATE_DISCONNECTED);
}


static void
on_incoming_call_clicked (CuiDemoWindow *self)
{
  if (!self->call1) {
    self->call1 = cui_demo_call_new ();

    g_signal_connect (self->call1,
                      "notify::state",
                      G_CALLBACK (on_call_state_changed),
                      self);
    on_call_state_changed (self->call1, NULL, self);

    cui_call_display_set_call (self->call_display, CUI_CALL (self->call1));
  }
}


static gboolean
key_pressed_cb (GtkWidget     *sender,
                GdkEvent      *event,
                CuiDemoWindow *self)
{
  GdkModifierType default_modifiers = gtk_accelerator_get_default_mod_mask ();
  guint keyval;
  GdkModifierType state;
  GdkKeymap *keymap;
  GdkEventKey *key_event = (GdkEventKey *) event;

  gdk_event_get_state (event, &state);

  keymap = gdk_keymap_get_for_display (gtk_widget_get_display (sender));

  gdk_keymap_translate_keyboard_state (keymap,
                                       key_event->hardware_keycode,
                                       state,
                                       key_event->group,
                                       &keyval, NULL, NULL, NULL);

  if ((keyval == GDK_KEY_q || keyval == GDK_KEY_Q) &&
      (state & default_modifiers) == GDK_CONTROL_MASK) {
    gtk_widget_destroy (GTK_WIDGET (self));

    return TRUE;
  }

  return FALSE;
}

static void
on_dial (CuiDemoWindow *self, const char number[], GtkWidget *sender)
{
  GtkDialogFlags flags;
  GtkWidget *dialog;

  g_debug ("Dialling %s", number);

  flags = GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR;
  dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                   flags,
                                   GTK_MESSAGE_INFO,
                                   GTK_BUTTONS_OK,
                                   "Dialling %s", number);

  g_signal_connect_swapped (dialog, "response",
                            G_CALLBACK (gtk_widget_destroy),
                            dialog);

  gtk_widget_show_all (dialog);
}

static void
cui_demo_window_class_init (CuiDemoWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/CallUI/Demo/ui/cui-demo-window.ui");
  gtk_widget_class_bind_template_child (widget_class, CuiDemoWindow, call_display);
  gtk_widget_class_bind_template_child (widget_class, CuiDemoWindow, dialpad);
  gtk_widget_class_bind_template_child (widget_class, CuiDemoWindow, keypad);
  gtk_widget_class_bind_template_child (widget_class, CuiDemoWindow, content_box);
  gtk_widget_class_bind_template_child (widget_class, CuiDemoWindow, incoming_call);
  gtk_widget_class_bind_template_child (widget_class, CuiDemoWindow, theme_variant_image);
  gtk_widget_class_bind_template_callback (widget_class, back_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, key_pressed_cb);
  gtk_widget_class_bind_template_callback (widget_class, theme_variant_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_incoming_call_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_dial);
}


static void
cui_demo_window_init (CuiDemoWindow *self)
{
  GtkSettings *settings = gtk_settings_get_default ();

  gtk_widget_init_template (GTK_WIDGET (self));

  g_object_bind_property_full (settings, "gtk-application-prefer-dark-theme",
                               self->theme_variant_image, "icon-name",
                               G_BINDING_SYNC_CREATE,
                               prefer_dark_theme_to_icon_name_cb,
                               NULL,
                               NULL,
                               NULL);
}

CuiDemoWindow *
cui_demo_window_new (GtkApplication *application)
{
  return g_object_new (CUI_TYPE_DEMO_WINDOW, "application", application, NULL);
}
