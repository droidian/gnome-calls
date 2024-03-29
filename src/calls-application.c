/* calls-application.c
 *
 * Copyright (C) 2018, 2019 Purism SPC
 * Copyright (C) 2018 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * This file is part of Calls.
 *
 * Calls is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Calls is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Calls.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *      Bob Ham <bob.ham@puri.sm>
 *      Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"
#include "calls-dbus-manager.h"
#include "calls-history-box.h"
#include "calls-new-call-box.h"
#include "calls-encryption-indicator.h"
#include "calls-ringer.h"
#include "calls-notifier.h"
#include "calls-record-store.h"
#include "calls-call-window.h"
#include "calls-main-window.h"
#include "calls-manager.h"
#include "calls-settings.h"
#include "calls-application.h"
#include "version.h"

#include <glib/gi18n.h>
#include <handy.h>
#include <libcallaudio.h>
#include <libebook-contacts/libebook-contacts.h>

/**
 * SECTION: calls-application
 * @title: CallsApplication
 * @short_description: Base Application class
 * @include: "calls-application.h"
 */

#define DEFAULT_PROVIDER_PLUGIN "mm"

struct _CallsApplication
{
  GtkApplication parent_instance;

  gboolean          daemon;
  CallsManager     *manager;
  CallsRinger      *ringer;
  CallsNotifier    *notifier;
  CallsRecordStore *record_store;
  CallsMainWindow  *main_window;
  CallsCallWindow  *call_window;
  CallsSettings    *settings;
  CallsDBusManager *dbus_manager;

  char             *uri;
};

G_DEFINE_TYPE (CallsApplication, calls_application, GTK_TYPE_APPLICATION);


static gboolean start_proper (CallsApplication *self);


static gboolean
calls_application_dbus_register (GApplication    *application,
                                 GDBusConnection *connection,
                                 const gchar     *object_path,
                                 GError         **error)
{
  CallsApplication *self = CALLS_APPLICATION (application);

  G_APPLICATION_CLASS (calls_application_parent_class)->dbus_register (application,
                                                                       connection,
                                                                       object_path,
                                                                       error);

  self->dbus_manager = calls_dbus_manager_new ();
  return calls_dbus_manager_register (self->dbus_manager, connection, object_path, error);
}


static void
calls_application_dbus_unregister (GApplication    *application,
                                   GDBusConnection *connection,
                                   const gchar     *object_path)
{
  CallsApplication *self = CALLS_APPLICATION (application);

  g_clear_object (&self->dbus_manager);

  G_APPLICATION_CLASS (calls_application_parent_class)->dbus_unregister (application,
                                                                         connection,
                                                                         object_path);
}


static gint
handle_local_options (GApplication *application,
                      GVariantDict *options)
{
  gboolean ok;
  g_autoptr (GError) error = NULL;
  const char *arg;

  g_debug ("Registering application");
  ok = g_application_register (application, NULL, &error);
  if (!ok) {
    g_error ("Error registering application: %s",
             error->message);
  }

  ok = g_variant_dict_contains (options, "version");
  if (ok) {
    char * version = g_str_equal (VCS_TAG, "") ? PACKAGE_VERSION : VCS_TAG;

    g_print ("%s %s\n", APP_DATA_NAME, version);
    exit (0);
  }

  ok = g_variant_dict_lookup (options, "provider", "&s", &arg);
  if (ok) {
    g_action_group_activate_action (G_ACTION_GROUP (application),
                                    "set-provider-name",
                                    g_variant_new_string (arg));
  } else {
    if (!calls_manager_has_any_provider (calls_manager_get_default ()))
      g_action_group_activate_action (G_ACTION_GROUP (application),
                                      "set-provider-name",
                                      g_variant_new_string (DEFAULT_PROVIDER_PLUGIN));
  }

  ok = g_variant_dict_contains (options, "daemon");
  if (ok) {
    g_action_group_activate_action (G_ACTION_GROUP (application),
                                    "set-daemon",
                                    NULL);
  }

  ok = g_variant_dict_lookup (options, "dial", "&s", &arg);
  if (ok) {
    g_action_group_activate_action (G_ACTION_GROUP (application),
                                    "dial",
                                    g_variant_new_string (arg));
  }

  return -1; // Continue processing signal
}


static void
set_provider_name_action (GSimpleAction *action,
                          GVariant      *parameter,
                          gpointer       user_data)
{
  const char *name;

  name = g_variant_get_string (parameter, NULL);
  g_return_if_fail (name != NULL);

  if (calls_manager_has_provider (calls_manager_get_default (), name)) {
    g_warning ("Cannot add provider `%s' because it is already loaded",
               name);
    return;
  }

  g_debug ("Start loading provider `%s'", name);
  calls_manager_add_provider (calls_manager_get_default (), name);
}


static void
set_daemon_action (GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer       user_data)
{
  CallsApplication *self = CALLS_APPLICATION (user_data);

  if (self->main_window) {
    g_warning ("Cannot set application as a daemon"
               " because application is already started");
    return;
  }

  self->daemon = TRUE;

  g_debug ("Application marked as daemon");
}


#define DIALLING    "0-9*#+ABCD"
#define SIGNALLING  ",TP!W@X"
#define VISUAL      "[:space:]\\-.()t/"
#define REJECT_RE   "[^" DIALLING SIGNALLING VISUAL "]"
#define VISUAL_RE   "[" VISUAL "]"

static gboolean
check_dial_number (const char *number)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GRegex) reject = g_regex_new (REJECT_RE, 0, 0, &error);
  gboolean matches;

  if (!reject) {
    g_warning ("Could not compile regex for"
               " dial number checking: %s",
               error->message);
    return FALSE;
  }

  matches = g_regex_match (reject, number, 0, NULL);

  return !matches;
}


static char *
extract_dial_string (const char *number)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GRegex) replace_visual = g_regex_new (VISUAL_RE, 0, 0, &error);
  char *dial_string;

  if (!replace_visual) {
    g_warning ("Could not compile regex for"
               " dial number extracting: %s",
               error->message);
    return NULL;
  }

  dial_string = g_regex_replace_literal
    (replace_visual, number, -1, 0, "", 0, &error);

  if (!dial_string) {
    g_warning ("Error replacing visual separators"
               " in dial number: %s",
               error->message);
    return NULL;
  }

  return dial_string;
}


static void
dial_action (GSimpleAction *action,
             GVariant      *parameter,
             gpointer       user_data)
{
  CallsApplication *self = CALLS_APPLICATION (user_data);
  const char *number;
  gboolean number_ok;
  g_autofree char *dial_string = NULL;

  number = g_variant_get_string (parameter, NULL);
  g_return_if_fail (number != NULL);

  if (g_str_has_prefix (number, "sip:") ||
      g_str_has_prefix (number, "sips:")) {
    dial_string = g_strdup (number);
    goto proper;
  }

  number_ok = check_dial_number (number);
  if (!number_ok) {
    g_warning ("Dial number `%s' is not a valid dial string",
               number);
    return;
  }

  dial_string = extract_dial_string (number);
  if (!dial_string) {
    return;
  }

  g_debug ("Dialing dial string `%s' extracted from number `%s'",
           dial_string, number);

 proper:
  start_proper (self);

  calls_main_window_dial (self->main_window,
                          dial_string);
}

static void
copy_number (GSimpleAction *action,
             GVariant      *parameter,
             gpointer       user_data)
{
  const char *number = g_variant_get_string (parameter, NULL);
  GtkClipboard *clipboard =
    gtk_clipboard_get_default (gdk_display_get_default ());

  gtk_clipboard_set_text (clipboard, number, -1);

  g_debug ("Copied `%s' to clipboard", number);
}

static void
manager_state_changed_cb (GApplication *application)
{
  GAction* dial_action = g_action_map_lookup_action (G_ACTION_MAP (application), "dial");
  CallsManagerState state = calls_manager_get_state (calls_manager_get_default ());

  g_simple_action_set_enabled (G_SIMPLE_ACTION (dial_action), state == CALLS_MANAGER_STATE_READY);
}

static const GActionEntry actions[] =
{
  { "set-provider-name", set_provider_name_action, "s" },
  { "set-daemon", set_daemon_action, NULL },
  { "dial", dial_action, "s" },
  { "copy-number", copy_number, "s"},
};


static void
startup (GApplication *application)
{
  g_autoptr (GtkCssProvider) provider = NULL;
  g_autoptr (GError) error = NULL;
  CallsApplication *self = CALLS_APPLICATION (application);
  CallsManager *manager;

  G_APPLICATION_CLASS (calls_application_parent_class)->startup (application);

  hdy_init ();

  if (!call_audio_init (&error))
    {
      g_warning ("Failed to init libcallaudio: %s", error->message);
    }

  g_set_prgname (APP_ID);
  g_set_application_name (_("Calls"));

  g_action_map_add_action_entries (G_ACTION_MAP (application),
                                   actions,
                                   G_N_ELEMENTS (actions),
                                   application);

  self->settings = calls_settings_new ();
  g_assert (self->settings != NULL);

  manager = calls_manager_get_default ();
  g_object_bind_property (self->settings, "country-code",
                          manager, "country-code",
                          G_BINDING_SYNC_CREATE);
  g_signal_connect_swapped (manager,
                            "notify::state",
                            G_CALLBACK (manager_state_changed_cb),
                            application);

  manager_state_changed_cb (application);

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/org/gnome/Calls/style.css");
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}


static void
notify_window_visible_cb (GtkWidget       *window,
                          GParamSpec      *pspec,
                          CallsApplication *application)
{
  CallsManager *manager = calls_manager_get_default ();

  g_return_if_fail (CALLS_IS_APPLICATION (application));
  g_return_if_fail (CALLS_IS_CALL_WINDOW (window));

  /* The UI is being closed, hang up active calls */
  if (!gtk_widget_is_visible (window))
    calls_manager_hang_up_all_calls (manager);
}


static gboolean
start_proper (CallsApplication  *self)
{
  GtkApplication *gtk_app;

  if (self->main_window) {
    return TRUE;
  }

  gtk_app = GTK_APPLICATION (self);

  self->ringer = calls_ringer_new ();
  g_assert (self->ringer != NULL);

  self->record_store = calls_record_store_new ();
  g_assert (self->record_store != NULL);

  self->notifier = calls_notifier_new ();
  g_assert (CALLS_IS_NOTIFIER (self->notifier));

  self->main_window = calls_main_window_new
    (gtk_app,
     G_LIST_MODEL (self->record_store));
  g_assert (self->main_window != NULL);

  self->call_window = calls_call_window_new (gtk_app);
  g_assert (self->call_window != NULL);

  g_signal_connect (self->call_window,
                    "notify::visible",
                    G_CALLBACK (notify_window_visible_cb),
                    self);

  return TRUE;
}

static void
open_sip_uri (CallsApplication *self,
              const char       *uri)
{
  char **tokens = NULL;
  g_assert (uri);

  tokens = g_strsplit (uri, "///", 2);

  if (tokens) {
    /* Remove "///" from "sip:///user@host" */
    g_autofree char *dial_string = g_strconcat (tokens[0], tokens[1], NULL);

    calls_main_window_dial (self->main_window, dial_string);

    g_strfreev (tokens);
  } else {
    /* Dial the uri as it is */
    calls_main_window_dial (self->main_window, uri);
  }
}

static void
open_tel_uri (CallsApplication *self,
              const char       *uri)
{
  g_autoptr (EPhoneNumber) number = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *dial_str = NULL;
  g_autofree char *country_code = NULL;

  g_object_get (calls_manager_get_default (),
                "country-code", &country_code,
                NULL);

  g_debug ("Opening tel URI `%s'", uri);

  number = e_phone_number_from_string (uri, country_code, &error);
  if (!number) {
    g_autofree char *msg =
      g_strdup_printf (_("Tried dialing unparsable tel URI `%s'"), uri);

    g_signal_emit_by_name (calls_manager_get_default (),
                           "error",
                           msg);
    g_warning ("Ignoring unparsable tel URI `%s': %s",
               uri, error->message);
    return;
  }

  dial_str = e_phone_number_to_string
    (number, E_PHONE_NUMBER_FORMAT_E164);

  calls_main_window_dial (self->main_window,
                          dial_str);
}

static void
activate (GApplication *application)
{
  CallsApplication *self = CALLS_APPLICATION (application);
  gboolean present;

  g_debug ("Activated");

  if (self->main_window) {
    present = TRUE;
  } else {
    gboolean ok = start_proper (self);
    if (!ok)
      return;

    present = !self->daemon;
  }

  if (present || self->uri) {
    gtk_window_present (GTK_WINDOW (self->main_window));
  }

  if (self->uri) {
    if (g_str_has_prefix (self->uri, "tel:"))
      open_tel_uri (self, self->uri);

    else if (g_str_has_prefix (self->uri, "sip:") ||
             g_str_has_prefix (self->uri, "sips:"))
      open_sip_uri (self, self->uri);
  }

  g_clear_pointer (&self->uri, g_free);
}

static void
app_open (GApplication  *application,
          GFile        **files,
          gint           n_files,
          const char    *hint)
{
  CallsApplication *self = CALLS_APPLICATION (application);

  g_assert (n_files > 0);

  if (n_files > 1)
    g_warning ("Calls can handle only one call a time. %u items provided", n_files);

  if (g_file_has_uri_scheme (files[0], "tel") ||
      g_file_has_uri_scheme (files[0], "sip") ||
      g_file_has_uri_scheme (files[0], "sips")) {

    g_free (self->uri);
    self->uri = g_file_get_uri (files[0]);
    g_debug ("Opening %s", self->uri);

    g_application_activate (application);
  } else {
    g_autofree char *msg = NULL;
    g_autofree char *uri = NULL;

    uri = g_file_get_parse_name (files[0]);
    g_warning ("Don't know how to"
               " open file `%s', ignoring",
               uri);

    msg = g_strdup_printf (_("Don't know how to open `%s'"), uri);

    g_signal_emit_by_name (calls_manager_get_default (),
                           "error", msg);
  }
}


static void
finalize (GObject *object)
{
  CallsApplication *self = (CallsApplication *)object;

  g_clear_object (&self->call_window);
  g_clear_object (&self->main_window);
  g_clear_object (&self->record_store);
  g_clear_object (&self->ringer);
  g_clear_object (&self->notifier);
  g_clear_object (&self->settings);
  g_free (self->uri);

  G_OBJECT_CLASS (calls_application_parent_class)->finalize (object);
}


static void
calls_application_class_init (CallsApplicationClass *klass)
{
  GApplicationClass *application_class = G_APPLICATION_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = finalize;

  application_class->handle_local_options = handle_local_options;
  application_class->startup = startup;
  application_class->activate = activate;
  application_class->open = app_open;
  application_class->dbus_register  = calls_application_dbus_register;
  application_class->dbus_unregister  = calls_application_dbus_unregister;

  g_type_ensure (CALLS_TYPE_ENCRYPTION_INDICATOR);
  g_type_ensure (CALLS_TYPE_HISTORY_BOX);
  g_type_ensure (CALLS_TYPE_NEW_CALL_BOX);
}


static void
calls_application_init (CallsApplication *self)
{
  const GOptionEntry options[] = {
    {
      "provider", 'p', G_OPTION_FLAG_NONE,
      G_OPTION_ARG_STRING, NULL,
      _("The name of the plugin to use for the call Provider"),
      _("PLUGIN")
    },
    {
      "daemon", 'd', G_OPTION_FLAG_NONE,
      G_OPTION_ARG_NONE, NULL,
      _("Whether to present the main window on startup"),
      NULL
    },
    {
      "dial", 'l', G_OPTION_FLAG_NONE,
      G_OPTION_ARG_STRING, NULL,
      _("Dial a number"),
      _("NUMBER")
    },
    {
      "version", 'v', G_OPTION_FLAG_NONE,
      G_OPTION_ARG_NONE, NULL,
      _("Print current version"),
      NULL
    },
    {
      NULL
    }
  };

  g_application_add_main_option_entries (G_APPLICATION (self), options);
}


CallsApplication *
calls_application_new (void)
{
  return g_object_new (CALLS_TYPE_APPLICATION,
                       "application-id", APP_ID,
                       "flags", G_APPLICATION_HANDLES_OPEN,
                       "register-session", TRUE,
                       NULL);
}

gboolean
calls_application_get_use_default_origins_setting (CallsApplication *self)
{
  g_return_val_if_fail (CALLS_IS_APPLICATION (self), FALSE);

  return calls_settings_get_use_default_origins (self->settings);
}

void
calls_application_set_use_default_origins_setting (CallsApplication *self,
                                                   gboolean enabled)
{
  g_return_if_fail (CALLS_IS_APPLICATION (self));

  calls_settings_set_use_default_origins (self->settings, enabled);
}

char *
calls_application_get_country_code_setting (CallsApplication *self)
{
  g_return_val_if_fail (CALLS_IS_APPLICATION (self), FALSE);

  return calls_settings_get_country_code (self->settings);
}

void
calls_application_set_country_code_setting (CallsApplication *self,
                                            const char       *country_code)
{
  g_return_if_fail (CALLS_IS_APPLICATION (self));

  calls_settings_set_country_code (self->settings, country_code);
}
