/*
 * Copyright (C) 2021 Purism SPC
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
 * Author: Evangelos Ribeiro Tzaras <evangelos.tzaras@puri.sm>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#define G_LOG_DOMAIN "CallsSipProvider"

#define SIP_ACCOUNT_FILE "sip-account.cfg"

#include "calls-account-provider.h"
#include "calls-credentials.h"
#include "calls-message-source.h"
#include "calls-provider.h"
#include "calls-sip-enums.h"
#include "calls-sip-origin.h"
#include "calls-sip-provider.h"
#include "calls-sip-util.h"
#include "config.h"

#include <libpeas/peas.h>
#include <sofia-sip/nua.h>
#include <sofia-sip/su_glib.h>

static const char * const supported_protocols[] = {
  "tel",
  "sip",
  "sips",
  NULL
};

/**
 * SECTION:sip-provider
 * @short_description: A #CallsProvider for the SIP protocol
 * @Title: CallsSipProvider
 *
 * #CallsSipProvider is derived from #CallsProvider and is responsible
 * for setting up the sofia-sip stack.
 */

enum {
  PROP_0,
  PROP_SIP_STATE,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

struct _CallsSipProvider
{
  CallsProvider parent_instance;

  GListStore *origins;
  GHashTable *credentials; /* key = credentials, value = origin */
  /* SIP */
  CallsSipContext *ctx;
  SipEngineState sip_state;

  gchar *filename;
};

static void calls_sip_provider_message_source_interface_init (CallsMessageSourceInterface *iface);
static void calls_sip_provider_account_provider_interface_init (CallsAccountProviderInterface *iface);


G_DEFINE_DYNAMIC_TYPE_EXTENDED
(CallsSipProvider, calls_sip_provider, CALLS_TYPE_PROVIDER, 0,
 G_IMPLEMENT_INTERFACE_DYNAMIC (CALLS_TYPE_MESSAGE_SOURCE,
                                calls_sip_provider_message_source_interface_init)
 G_IMPLEMENT_INTERFACE_DYNAMIC (CALLS_TYPE_ACCOUNT_PROVIDER,
                                calls_sip_provider_account_provider_interface_init));


static void
calls_sip_provider_load_accounts (CallsSipProvider *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GKeyFile) key_file = g_key_file_new ();
  gchar **groups = NULL;

  g_assert (CALLS_IS_SIP_PROVIDER (self));

  if (!g_key_file_load_from_file (key_file, self->filename, G_KEY_FILE_NONE, &error)) {
    g_warning ("Error loading key file: %s", error->message);
    return;
  }

  groups = g_key_file_get_groups (key_file, NULL);

  for (gsize i = 0; groups[i] != NULL; i++) {
    g_autoptr (CallsCredentials) credentials = calls_credentials_new ();
    gint local_port = 0;
    gboolean direct_connection =
      g_key_file_get_boolean (key_file, groups[i], "Direct", NULL);

    if (direct_connection) {
      g_object_set (credentials, "name", groups[i], NULL);

      local_port = g_key_file_get_integer (key_file, groups[i], "LocalPort", NULL);
      /* direct connection mode, needs a local port set */
      if (local_port == 0)
        local_port = 5060;
    } else {
      calls_credentials_update_from_keyfile (credentials, key_file, groups[i]);
    }
    g_debug ("Adding origin for SIP account %s", groups[i]);

    calls_sip_provider_add_origin (self, g_steal_pointer (&credentials), local_port, direct_connection);
  }

  g_strfreev (groups);
}


static const char *
calls_sip_provider_get_name (CallsProvider *provider)
{
  return "SIP provider";
}


static const char *
calls_sip_provider_get_status (CallsProvider *provider)
{
  CallsSipProvider *self = CALLS_SIP_PROVIDER (provider);

  switch (self->sip_state) {
  case SIP_ENGINE_ERROR:
    return "Error";

  case SIP_ENGINE_READY:
    return "Normal";

  default:
    break;
  }
  return "Unknown";
}


static GListModel *
calls_sip_provider_get_origins (CallsProvider *provider)
{
  CallsSipProvider *self = CALLS_SIP_PROVIDER (provider);

  return G_LIST_MODEL (self->origins);
}

static const char * const *
calls_sip_provider_get_protocols (CallsProvider *provider)
{
  return supported_protocols;
}


static void
calls_sip_provider_deinit_sip (CallsSipProvider *self)
{
  GSource *gsource;

  if (self->sip_state == SIP_ENGINE_NULL)
    return;

  /* clean up sofia */
  if (!self->ctx)
    goto bail;


  if (self->ctx->root) {
    gsource = su_glib_root_gsource (self->ctx->root);
    g_source_destroy (gsource);
    su_root_destroy (self->ctx->root);

    if (su_home_unref (self->ctx->home) != 1)
      g_error ("Error in su_home_unref ()");
  }
  g_clear_pointer (&self->ctx, g_free);

 bail:
  self->sip_state = SIP_ENGINE_NULL;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SIP_STATE]);
}


static gboolean
calls_sip_provider_init_sofia (CallsSipProvider *self,
                               GError          **error)
{
  GSource *gsource;

  g_assert (CALLS_SIP_PROVIDER (self));

  self->ctx = g_new0 (CallsSipContext, 1);
  if (self->ctx == NULL) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "Could not allocate memory for the SIP context");
    goto err;
  }

  if (su_init () != su_success) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "su_init () failed");
    goto err;
  }

  if (su_home_init (self->ctx->home) != su_success) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "su_home_init () failed");
    goto err;
  }

  self->ctx->root = su_glib_root_create (self);
  if (self->ctx->root == NULL) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "su_glib_root_create () failed");
    goto err;
  }
  gsource = su_glib_root_gsource (self->ctx->root);
  if (gsource == NULL) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "su_glib_root_gsource () failed");
    goto err;
  }

  g_source_attach (gsource, NULL);
  self->sip_state = SIP_ENGINE_READY;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SIP_STATE]);
  return TRUE;

 err:
  self->sip_state = SIP_ENGINE_ERROR;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SIP_STATE]);
  return FALSE;
}


static void
calls_sip_provider_get_property (GObject      *object,
                                 guint         property_id,
                                 GValue       *value,
                                 GParamSpec   *pspec)
{
  CallsSipProvider *self = CALLS_SIP_PROVIDER (object);

  switch (property_id) {
  case PROP_SIP_STATE:
    g_value_set_enum (value, self->sip_state);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
calls_sip_provider_constructed (GObject *object)
{
  CallsSipProvider *self = CALLS_SIP_PROVIDER (object);
  g_autoptr (GError) error = NULL;
  gboolean auto_load_accounts = TRUE;
  const gchar *env_do_not_auto_load;

  env_do_not_auto_load = g_getenv ("CALLS_SIP_DO_NOT_AUTOLOAD");
  if (env_do_not_auto_load && env_do_not_auto_load[0] != '\0')
    auto_load_accounts = FALSE;

  if (calls_sip_provider_init_sofia (self, &error)) {
    if (auto_load_accounts)
      calls_sip_provider_load_accounts (self);
  }
  else
    g_warning ("Could not initialize sofia stack: %s", error->message);

  G_OBJECT_CLASS (calls_sip_provider_parent_class)->constructed (object);
}


static void
calls_sip_provider_dispose (GObject *object)
{
  CallsSipProvider *self = CALLS_SIP_PROVIDER (object);

  g_list_store_remove_all (self->origins);
  g_clear_object (&self->origins);

  g_hash_table_remove_all (self->credentials);
  g_clear_pointer (&self->credentials, g_hash_table_unref);

  g_clear_pointer (&self->filename, g_free);

  calls_sip_provider_deinit_sip (self);

  G_OBJECT_CLASS (calls_sip_provider_parent_class)->dispose (object);
}


static void
calls_sip_provider_class_finalize (CallsSipProviderClass *klass)
{
}


static void
calls_sip_provider_class_init (CallsSipProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CallsProviderClass *provider_class = CALLS_PROVIDER_CLASS (klass);

  object_class->constructed = calls_sip_provider_constructed;
  object_class->dispose = calls_sip_provider_dispose;
  object_class->get_property = calls_sip_provider_get_property;

  provider_class->get_name = calls_sip_provider_get_name;
  provider_class->get_status = calls_sip_provider_get_status;
  provider_class->get_origins = calls_sip_provider_get_origins;
  provider_class->get_protocols = calls_sip_provider_get_protocols;

  props[PROP_SIP_STATE] =
    g_param_spec_enum ("sip-state",
                       "SIP state",
                       "The state of the SIP engine",
                       SIP_TYPE_ENGINE_STATE,
                       SIP_ENGINE_NULL,
                       G_PARAM_READABLE);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
calls_sip_provider_message_source_interface_init (CallsMessageSourceInterface *iface)
{
}

static gboolean
add_account (CallsAccountProvider *acc_provider,
             CallsCredentials     *credentials)
{
  CallsSipProvider *self;

  g_assert (CALLS_IS_ACCOUNT_PROVIDER (acc_provider));
  g_assert (CALLS_IS_SIP_PROVIDER (acc_provider));
  g_assert (CALLS_IS_CREDENTIALS (credentials));

  self = CALLS_SIP_PROVIDER (acc_provider);

  return !!calls_sip_provider_add_origin (self, credentials, 0, FALSE);
}

static gboolean
remove_account (CallsAccountProvider *acc_provider,
                CallsCredentials     *credentials)
{
  CallsSipProvider *self;
  CallsSipOrigin *origin;
  guint position;

  g_assert (CALLS_IS_ACCOUNT_PROVIDER (acc_provider));
  g_assert (CALLS_IS_SIP_PROVIDER (acc_provider));
  g_assert (CALLS_IS_CREDENTIALS (credentials));

  self = CALLS_SIP_PROVIDER (acc_provider);

  origin = g_hash_table_lookup (self->credentials, credentials);

  if (origin == NULL)
    return FALSE;

  if (!g_list_store_find (self->origins, origin, &position))
    return FALSE;

  g_hash_table_remove (self->credentials, credentials);
  g_list_store_remove (self->origins, position);

  return TRUE;
}

static CallsAccount *
get_account (CallsAccountProvider *acc_provider,
             CallsCredentials     *credentials)
{
  CallsSipProvider *self;
  CallsSipOrigin *origin;

  g_assert (CALLS_IS_ACCOUNT_PROVIDER (acc_provider));
  g_assert (CALLS_IS_SIP_PROVIDER (acc_provider));
  g_assert (CALLS_IS_CREDENTIALS (credentials));

  self = CALLS_SIP_PROVIDER (acc_provider);

  origin = g_hash_table_lookup (self->credentials, credentials);
  if (origin)
    return CALLS_ACCOUNT (origin);
  else
    return NULL;
}

static void
calls_sip_provider_account_provider_interface_init (CallsAccountProviderInterface *iface)
{
  iface->add_account = add_account;
  iface->remove_account = remove_account;
  iface->get_account = get_account;
}

static void
calls_sip_provider_init (CallsSipProvider *self)
{
  const char *filename_env = g_getenv ("CALLS_SIP_ACCOUNT_FILE");

  self->credentials =
    g_hash_table_new_full (NULL, NULL, g_object_unref, g_object_unref);

  self->origins = g_list_store_new (CALLS_TYPE_ORIGIN);

  if (filename_env && filename_env[0] != '\0')
    self->filename = g_strdup (filename_env);
  else
    self->filename = g_build_filename (g_get_user_config_dir (),
                                       APP_DATA_NAME,
                                       SIP_ACCOUNT_FILE,
                                       NULL);
}

/**
 * calls_sip_provider_add_origin:
 * @self: A #CallsSipProvider
 * @credentials: A #CallsCredentials
 * @local_port: The local port to bind to or 0
 * @direct_connection: %TRUE to use a direct connection to peers, %FALSE otherwise
 *
 * Adds a new origin (SIP account). If @direct_connection is set
 * some properties of @credentials can be set automatically
 * (f.e. use the username and hostname).
 *
 * Return: (transfer none): A #CallsSipOrigin
 */
CallsSipOrigin *
calls_sip_provider_add_origin (CallsSipProvider *self,
                               CallsCredentials *credentials,
                               gint              local_port,
                               gboolean          direct_connection)
{
  g_autoptr (CallsSipOrigin) origin = NULL;

  g_return_val_if_fail (CALLS_IS_SIP_PROVIDER (self), NULL);
  g_return_val_if_fail (CALLS_IS_CREDENTIALS (credentials), NULL);

  if (g_hash_table_contains (self->credentials, credentials)) {
    g_autofree char *name = NULL;
    g_object_get (credentials, "name", &name, NULL);

    /* This could be a INFO level log once we have improved logging */
    g_debug ("Cannot add credentials with name '%s' multiple times", name);
    return NULL;
  }

  origin = calls_sip_origin_new (self->ctx,
                                 credentials,
                                 local_port,
                                 direct_connection);

  g_hash_table_insert (self->credentials,
                       g_object_ref (credentials), g_object_ref (origin));
  g_list_store_append (self->origins, origin);

  return origin;
}


CallsSipProvider *
calls_sip_provider_new (void)
{
  return g_object_new (CALLS_TYPE_SIP_PROVIDER, NULL);
}


G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
  calls_sip_provider_register_type (G_TYPE_MODULE (module));

  peas_object_module_register_extension_type (module,
                                              CALLS_TYPE_PROVIDER,
                                              CALLS_TYPE_SIP_PROVIDER);
}
