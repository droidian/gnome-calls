/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "cui-call.h"
#include "cui-enums.h"

#include <gio/gio.h>

G_DEFINE_INTERFACE (CuiCall, cui_call, G_TYPE_OBJECT)


/**
 * CuiCall:
 *
 * An interface for phone calls.
 *
 * Objects implementing the `CuiCall` interface can be handled in a
 * [class@Cui.CallDisplay]. [class@Cui.CallDisplay] will invoke
 * this interface's implementation to accept, hang-up calls. etc.
 */

void
cui_call_default_init (CuiCallInterface *iface)
{
  /**
   * CuiCall:display-name:
   *
   * The display name. E.g. the name of the caller instead of the plain
   * phone number.
   */
  g_object_interface_install_property (
    iface,
    g_param_spec_string ("display-name",
                         "",
                         "",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY));
  /**
   * CuiCall:avatar-icon:
   *
   * The call's avatar icon
   */
  g_object_interface_install_property (
    iface,
    g_param_spec_object ("avatar-icon",
                         "",
                         "",
                         G_TYPE_LOADABLE_ICON,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY));
  /**
   * CuiCall:id:
   *
   * The call's id, e.g. a phone number.
   */
  g_object_interface_install_property (
    iface,
    g_param_spec_string ("id",
                         "",
                         "",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY));

  /**
   * CuiCall:state:
   *
   * The call's state.
   */
  g_object_interface_install_property (
    iface,
    g_param_spec_enum ("state",
                       "",
                       "",
                       CUI_TYPE_CALL_STATE,
                       0,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY));

  /**
   * CuiCall:encrypted:
   *
   * Whether the call is encrypted
   */
  g_object_interface_install_property (
    iface,
    g_param_spec_boolean ("encrypted",
                          "",
                          "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY));

  /**
   * CuiCall:can-dtmf
   *
   * Whether the call can have DTMF
   */
  g_object_interface_install_property (
    iface,
    g_param_spec_boolean ("can-dtmf",
                          "",
                          "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY));
}


/**
 * cui_call_get_avatar_icon:
 * @self: The call
 *
 * Get the avatar icon.
 *
 * Returns: (transfer none)(nullable): The icon as `GLoadableIcon`
 */
GLoadableIcon *
cui_call_get_avatar_icon (CuiCall *self)
{
  CuiCallInterface *iface;

  g_return_val_if_fail (CUI_IS_CALL (self), NULL);

  iface = CUI_CALL_GET_IFACE (self);
  g_return_val_if_fail (iface->get_avatar_icon, NULL);

  return iface->get_avatar_icon (self);
}


const char *
cui_call_get_display_name (CuiCall *self)
{
  CuiCallInterface *iface;

  g_return_val_if_fail (CUI_IS_CALL (self), NULL);

  iface = CUI_CALL_GET_IFACE (self);
  g_return_val_if_fail (iface->get_display_name, NULL);

  return iface->get_display_name (self);
}


const char *
cui_call_get_id (CuiCall *self)
{
  CuiCallInterface *iface;

  g_return_val_if_fail (CUI_IS_CALL (self), NULL);

  iface = CUI_CALL_GET_IFACE (self);
  g_return_val_if_fail (iface->get_id, NULL);

  return iface->get_id (self);
}


CuiCallState
cui_call_get_state (CuiCall *self)
{
  CuiCallInterface *iface;

  g_return_val_if_fail (CUI_IS_CALL (self), CUI_CALL_STATE_UNKNOWN);

  iface = CUI_CALL_GET_IFACE (self);
  g_return_val_if_fail (iface->get_state, CUI_CALL_STATE_UNKNOWN);

  return iface->get_state (self);
}


gboolean
cui_call_get_encrypted (CuiCall *self)
{
  CuiCallInterface *iface;

  g_return_val_if_fail (CUI_IS_CALL (self), FALSE);

  iface = CUI_CALL_GET_IFACE (self);
  g_return_val_if_fail (iface->get_encrypted, FALSE);

  return iface->get_encrypted (self);
}


gboolean
cui_call_get_can_dtmf (CuiCall *self)
{
  CuiCallInterface *iface;

  g_return_val_if_fail (CUI_IS_CALL (self), FALSE);

  iface = CUI_CALL_GET_IFACE (self);
  g_return_val_if_fail (iface->get_can_dtmf, FALSE);

  return iface->get_can_dtmf (self);
}

/**
 * cui_call_accept:
 * @self: The call
 *
 * Accept the call.
 */
void
cui_call_accept (CuiCall *self)
{
  CuiCallInterface *iface;

  g_return_if_fail (CUI_IS_CALL (self));

  iface = CUI_CALL_GET_IFACE (self);
  g_return_if_fail (iface->accept);

  iface->accept (self);
}

/**
 * cui_call_hang_up:
 * @self: The call
 *
 * Hang up the call.
 */
void
cui_call_hang_up (CuiCall *self)
{
  CuiCallInterface *iface;

  g_return_if_fail (CUI_IS_CALL (self));

  iface = CUI_CALL_GET_IFACE (self);
  g_return_if_fail (iface->hang_up);

  iface->hang_up (self);
}

/**
 * cui_call_send_dtmf
 * @self: The call
 * @dtmf: The DTMF data
 *
 * Send DTMF to the call.
 */
void
cui_call_send_dtmf (CuiCall *self, const gchar *dtmf)
{
  CuiCallInterface *iface;

  g_return_if_fail (CUI_IS_CALL (self));

  if (!cui_call_get_can_dtmf (self))
    return;

  iface = CUI_CALL_GET_IFACE (self);
  g_return_if_fail (iface->send_dtmf);

  iface->send_dtmf (self, dtmf);
}
