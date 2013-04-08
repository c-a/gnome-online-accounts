/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2012 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: David Zeuthen <davidz@redhat.com>
 *          Debarshi Ray <debarshir@gnome.org>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <rest/oauth-proxy.h>
#include <json-glib/json-glib.h>

#include "goalogging.h"
#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goaoauth2provider.h"
#include "goagoogleprovider.h"
#include "goahttpclient.h"
#include "goautils.h"

/**
 * GoaGoogleProvider:
 *
 * The #GoaGoogleProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaGoogleProvider
{
  /*< private >*/
  GoaOAuth2Provider parent_instance;
};

typedef struct _GoaGoogleProviderClass GoaGoogleProviderClass;

struct _GoaGoogleProviderClass
{
  GoaOAuth2ProviderClass parent_class;
};

/**
 * SECTION:goagoogleprovider
 * @title: GoaGoogleProvider
 * @short_description: A provider for Google
 *
 * #GoaGoogleProvider is used for handling Google accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaGoogleProvider, goa_google_provider, GOA_TYPE_OAUTH2_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "google",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *CALDAV_ENDPOINT = "https://www.google.com/calendar/dav/%s/events/";

static const gchar *
get_provider_type (GoaProvider *_provider)
{
  return "google";
}

static gchar *
get_provider_name (GoaProvider *_provider,
                   GoaObject   *object)
{
  return g_strdup (_("Google"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *_provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *_provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED |
         GOA_PROVIDER_FEATURE_MAIL |
         GOA_PROVIDER_FEATURE_CALENDAR |
         GOA_PROVIDER_FEATURE_CONTACTS |
         GOA_PROVIDER_FEATURE_CHAT |
         GOA_PROVIDER_FEATURE_DOCUMENTS;
}

static const gchar *
get_authorization_uri (GoaOAuth2Provider *provider)
{
  return "https://accounts.google.com/o/oauth2/auth";
}

static const gchar *
get_token_uri (GoaOAuth2Provider *provider)
{
  return "https://accounts.google.com/o/oauth2/token";
}

static const gchar *
get_redirect_uri (GoaOAuth2Provider *provider)
{
  return "http://localhost";
}

static const gchar *
get_scope (GoaOAuth2Provider *provider)
{
  return /* Read-only access to the user's email address */
         "https://www.googleapis.com/auth/userinfo.email "

         /* Google Calendar API */
         "https://www.googleapis.com/auth/calendar "

         /* Google Contacts API */
         "https://www.google.com/m8/feeds/ "

         /* Google Documents List Data API */
         "https://docs.google.com/feeds/ "
         "https://docs.googleusercontent.com/ "
         "https://spreadsheets.google.com/feeds/ "

         /* GMail IMAP and SMTP access */
         "https://mail.google.com/ "

         /* Google Talk */
         "https://www.googleapis.com/auth/googletalk "

         /* Tasks API: https://developers.google.com/google-apps/tasks/auth */
         "https://www.googleapis.com/auth/tasks";
}

static guint
get_credentials_generation (GoaProvider *provider)
{
  return 4;
}

static const gchar *
get_client_id (GoaOAuth2Provider *provider)
{
  return GOA_GOOGLE_CLIENT_ID;
}

static const gchar *
get_client_secret (GoaOAuth2Provider *provider)
{
  return GOA_GOOGLE_CLIENT_SECRET;
}

static const gchar *
get_authentication_cookie (GoaOAuth2Provider *provider)
{
  return "LSID";
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_identity_sync (GoaOAuth2Provider  *provider,
                   const gchar        *access_token,
                   gchar             **out_presentation_identity,
                   GCancellable       *cancellable,
                   GError            **error)
{
  GError *identity_error;
  RestProxy *proxy;
  RestProxyCall *call;
  JsonParser *parser;
  JsonObject *json_object;
  gchar *ret;
  gchar *email;

  ret = NULL;

  identity_error = NULL;
  proxy = NULL;
  call = NULL;
  parser = NULL;
  email = NULL;

  /* TODO: cancellable */

  proxy = rest_proxy_new ("https://www.googleapis.com/oauth2/v2/userinfo", FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "GET");
  rest_proxy_call_add_param (call, "access_token", access_token);

  if (!rest_proxy_call_sync (call, error))
    goto out;
  if (rest_proxy_call_get_status_code (call) != 200)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Expected status 200 when requesting guid, instead got status %d (%s)"),
                   rest_proxy_call_get_status_code (call),
                   rest_proxy_call_get_status_message (call));
      goto out;
    }

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser,
                                   rest_proxy_call_get_payload (call),
                                   rest_proxy_call_get_payload_length (call),
                                   &identity_error))
    {
      goa_warning ("json_parser_load_from_data() failed: %s (%s, %d)",
                   identity_error->message,
                   g_quark_to_string (identity_error->domain),
                   identity_error->code);
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }

  json_object = json_node_get_object (json_parser_get_root (parser));
  email = g_strdup (json_object_get_string_member (json_object, "email"));
  if (email == NULL)
    {
      goa_warning ("Did not find email in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }


  ret = email;
  email = NULL;
  if (out_presentation_identity != NULL)
    *out_presentation_identity = g_strdup (ret); /* for now: use email as presentation identity */

 out:
  g_clear_error (&identity_error);
  g_free (email);
  if (call != NULL)
    g_object_unref (call);
  if (proxy != NULL)
    g_object_unref (proxy);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_deny_node (GoaOAuth2Provider *provider, WebKitDOMNode *node)
{
  WebKitDOMHTMLElement *element;
  gboolean ret;
  gchar *id;

  id = NULL;
  ret = FALSE;

  if (!WEBKIT_DOM_IS_HTML_BUTTON_ELEMENT (node))
    goto out;

  element = WEBKIT_DOM_HTML_ELEMENT (node);
  id = webkit_dom_html_element_get_id (element);
  if (g_strcmp0 (id, "submit_deny_access") != 0)
    goto out;

  ret = TRUE;

 out:
  g_free (id);
  return ret;
}

static gboolean
is_identity_node (GoaOAuth2Provider *provider, WebKitDOMHTMLInputElement *element)
{
  gboolean ret;
  gchar *element_type;
  gchar *id;
  gchar *name;

  element_type = NULL;
  id = NULL;
  name = NULL;

  ret = FALSE;

  g_object_get (element, "type", &element_type, NULL);
  if (g_strcmp0 (element_type, "email") != 0)
    goto out;

  id = webkit_dom_html_element_get_id (WEBKIT_DOM_HTML_ELEMENT (element));
  if (g_strcmp0 (id, "Email") != 0)
    goto out;

  name = webkit_dom_html_input_element_get_name (element);
  if (g_strcmp0 (name, "Email") != 0)
    goto out;

  ret = TRUE;

 out:
  g_free (element_type);
  g_free (id);
  g_free (name);
  return ret;
}

static gboolean
is_password_node (GoaOAuth2Provider *provider, WebKitDOMHTMLInputElement *element)
{
  gboolean ret;
  gchar *element_type;
  gchar *id;
  gchar *name;

  element_type = NULL;
  id = NULL;
  name = NULL;

  ret = FALSE;

  g_object_get (element, "type", &element_type, NULL);
  if (g_strcmp0 (element_type, "password") != 0)
    goto out;

  id = webkit_dom_html_element_get_id (WEBKIT_DOM_HTML_ELEMENT (element));
  if (g_strcmp0 (id, "Passwd") != 0)
    goto out;

  name = webkit_dom_html_input_element_get_name (element);
  if (g_strcmp0 (name, "Passwd") != 0)
    goto out;

  ret = TRUE;

 out:
  g_free (element_type);
  g_free (id);
  g_free (name);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean on_handle_get_password (GoaPasswordBased      *interface,
                                        GDBusMethodInvocation *invocation,
                                        const gchar           *id,
                                        gpointer               user_data);

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const gchar         *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  GoaAccount *account;
  GoaMail *mail;
  GoaCalendar *calendar;
  GoaContacts *contacts;
  GoaChat *chat;
  GoaDocuments *documents;
  GoaPasswordBased *password_based;
  GoaTasks *tasks;
  gboolean ret;
  gboolean mail_enabled;
  gboolean calendar_enabled;
  gboolean contacts_enabled;
  gboolean chat_enabled;
  gboolean documents_enabled;
  gboolean tasks_enabled;

  account = NULL;
  mail = NULL;
  calendar = NULL;
  contacts = NULL;
  chat = NULL;
  documents = NULL;
  tasks = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_google_provider_parent_class)->build_object (provider,
                                                                            object,
                                                                            key_file,
                                                                            group,
                                                                            connection,
                                                                            just_added,
                                                                            error))
    goto out;

  password_based = goa_object_get_password_based (GOA_OBJECT (object));
  if (password_based == NULL)
    {
      password_based = goa_password_based_skeleton_new ();
      /* Ensure D-Bus method invocations run in their own thread */
      g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (password_based),
                                           G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
      goa_object_skeleton_set_password_based (object, password_based);
      g_signal_connect (password_based,
                        "handle-get-password",
                        G_CALLBACK (on_handle_get_password),
                        NULL);
    }

  account = goa_object_get_account (GOA_OBJECT (object));

  /* Email */
  mail = goa_object_get_mail (GOA_OBJECT (object));
  mail_enabled = g_key_file_get_boolean (key_file, group, "MailEnabled", NULL);
  if (mail_enabled)
    {
      if (mail == NULL)
        {
          const gchar *email_address;
          email_address = goa_account_get_identity (account);
          mail = goa_mail_skeleton_new ();
          g_object_set (G_OBJECT (mail),
                        "email-address",   email_address,
                        "imap-supported",  TRUE,
                        "imap-host",       "imap.gmail.com",
                        "imap-user-name",  email_address,
                        "imap-use-ssl",    TRUE,
                        "smtp-supported",  TRUE,
                        "smtp-host",       "smtp.gmail.com",
                        "smtp-user-name",  email_address,
                        "smtp-use-tls",    TRUE,
                        NULL);
          goa_object_skeleton_set_mail (object, mail);
        }
    }
  else
    {
      if (mail != NULL)
        goa_object_skeleton_set_mail (object, NULL);
    }

  /* Calendar */
  calendar = goa_object_get_calendar (GOA_OBJECT (object));
  calendar_enabled = g_key_file_get_boolean (key_file, group, "CalendarEnabled", NULL);
  if (calendar_enabled)
    {
      if (calendar == NULL)
        {
          calendar = goa_calendar_skeleton_new ();
          goa_object_skeleton_set_calendar (object, calendar);
        }
    }
  else
    {
      if (calendar != NULL)
        goa_object_skeleton_set_calendar (object, NULL);
    }

  /* Contacts */
  contacts = goa_object_get_contacts (GOA_OBJECT (object));
  contacts_enabled = g_key_file_get_boolean (key_file, group, "ContactsEnabled", NULL);
  if (contacts_enabled)
    {
      if (contacts == NULL)
        {
          contacts = goa_contacts_skeleton_new ();
          goa_object_skeleton_set_contacts (object, contacts);
        }
    }
  else
    {
      if (contacts != NULL)
        goa_object_skeleton_set_contacts (object, NULL);
    }

  /* Chat */
  chat = goa_object_get_chat (GOA_OBJECT (object));
  chat_enabled = g_key_file_get_boolean (key_file, group, "ChatEnabled", NULL);
  if (chat_enabled)
    {
      if (chat == NULL)
        {
          chat = goa_chat_skeleton_new ();
          goa_object_skeleton_set_chat (object, chat);
        }
    }
  else
    {
      if (chat != NULL)
        goa_object_skeleton_set_chat (object, NULL);
    }

  /* Documents */
  documents = goa_object_get_documents (GOA_OBJECT (object));
  documents_enabled = g_key_file_get_boolean (key_file, group, "DocumentsEnabled", NULL);

  if (documents_enabled)
    {
      if (documents == NULL)
        {
          documents = goa_documents_skeleton_new ();
          goa_object_skeleton_set_documents (object, documents);
        }
    }
  else
    {
      if (documents != NULL)
        goa_object_skeleton_set_documents (object, NULL);
    }

  /* Tasks */
  tasks = goa_object_get_tasks (GOA_OBJECT (object));
  tasks_enabled = g_key_file_get_boolean (key_file, group, "TasksEnabled", NULL);

  if (tasks_enabled)
    {
      if (tasks == NULL)
        {
          tasks = goa_tasks_skeleton_new ();
          goa_object_skeleton_set_tasks (object, tasks);
        }
    }
  else
    {
      if (tasks != NULL)
        goa_object_skeleton_set_tasks (object, NULL);
    }

  if (just_added)
    {
      goa_account_set_mail_disabled (account, !mail_enabled);
      goa_account_set_calendar_disabled (account, !calendar_enabled);
      goa_account_set_contacts_disabled (account, !contacts_enabled);
      goa_account_set_chat_disabled (account, !chat_enabled);
      goa_account_set_documents_disabled (account, !documents_enabled);
      goa_account_set_tasks_disabled (account, !tasks_enabled);

      g_signal_connect (account,
                        "notify::mail-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "MailEnabled");
      g_signal_connect (account,
                        "notify::calendar-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "CalendarEnabled");
      g_signal_connect (account,
                        "notify::contacts-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "ContactsEnabled");
      g_signal_connect (account,
                        "notify::chat-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "ChatEnabled");
      g_signal_connect (account,
                        "notify::documents-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "DocumentsEnabled");
      g_signal_connect (account,
                        "notify::tasks-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "TasksEnabled");
    }

  ret = TRUE;

 out:
  g_clear_object (&tasks);
  g_clear_object (&documents);
  g_clear_object (&chat);
  g_clear_object (&contacts);
  g_clear_object (&calendar);
  g_clear_object (&mail);
  g_clear_object (&account);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
ensure_credentials_sync (GoaProvider   *provider,
                         GoaObject     *object,
                         gint          *out_expires_in,
                         GCancellable  *cancellable,
                         GError       **error)
{
  GVariant *credentials;
  GoaAccount *account;
  GoaHttpClient *http_client;
  gboolean ret;
  const gchar *username;
  gchar *password;
  gchar *uri_caldav;

  credentials = NULL;
  http_client = NULL;
  password = NULL;
  uri_caldav = NULL;

  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_google_provider_parent_class)->ensure_credentials_sync (provider,
                                                                                       object,
                                                                                       out_expires_in,
                                                                                       cancellable,
                                                                                       error))
    goto out;

  credentials = goa_utils_lookup_credentials_sync (provider,
                                                   object,
                                                   cancellable,
                                                   error);
  if (credentials == NULL)
    {
      if (error != NULL)
        {
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  account = goa_object_peek_account (object);
  username = goa_account_get_presentation_identity (account);
  uri_caldav = g_strdup_printf (CALDAV_ENDPOINT, username);

  if (!g_variant_lookup (credentials, "password", "s", &password))
    {
      if (error != NULL)
        {
          *error = g_error_new (GOA_ERROR,
                                GOA_ERROR_NOT_AUTHORIZED,
                                _("Did not find password with identity `%s' in credentials"),
                                username);
        }
      goto out;
    }

  http_client = goa_http_client_new ();
  ret = goa_http_client_check_sync (http_client,
                                    uri_caldav,
                                    username,
                                    password,
                                    FALSE,
                                    cancellable,
                                    error);
  if (!ret)
    {
      if (error != NULL)
        {
          g_prefix_error (error,
                          /* Translators: the first %s is the username
                           * (eg., debarshi.ray@gmail.com or rishi), and the
                           * (%s, %d) is the error domain and code.
                           */
                          _("Invalid password with username `%s' (%s, %d): "),
                          username,
                          g_quark_to_string ((*error)->domain),
                          (*error)->code);
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

 out:
  g_clear_object (&http_client);
  g_free (password);
  g_free (uri_caldav);
  g_clear_pointer (&credentials, (GDestroyNotify) g_variant_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
get_use_mobile_browser (GoaOAuth2Provider *provider)
{
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              GtkGrid             *left,
              GtkGrid             *right)
{
  /* Chain up */
  GOA_PROVIDER_CLASS (goa_google_provider_parent_class)->show_account (provider,
                                                                       client,
                                                                       object,
                                                                       vbox,
                                                                       left,
                                                                       right);

  goa_util_add_row_switch_from_keyfile_with_blurb (left, right, object,
                                                   _("Use for"),
                                                   "mail-disabled",
                                                   _("_Mail"));

  goa_util_add_row_switch_from_keyfile_with_blurb (left, right, object,
                                                   NULL,
                                                   "calendar-disabled",
                                                   _("Cale_ndar"));

  goa_util_add_row_switch_from_keyfile_with_blurb (left, right, object,
                                                   NULL,
                                                   "contacts-disabled",
                                                   _("_Contacts"));

  goa_util_add_row_switch_from_keyfile_with_blurb (left, right, object,
                                                   NULL,
                                                   "chat-disabled",
                                                   _("C_hat"));

  goa_util_add_row_switch_from_keyfile_with_blurb (left, right, object,
                                                   NULL,
                                                   "documents-disabled",
                                                   _("_Documents"));

  goa_util_add_row_switch_from_keyfile_with_blurb (left, right, object,
                                                   NULL,
                                                   "tasks-disabled",
                                                   _("_Tasks"));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_key_values (GoaOAuth2Provider  *provider,
                        GVariantBuilder   *builder)
{
  g_variant_builder_add (builder, "{ss}", "MailEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "CalendarEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "ContactsEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "ChatEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "DocumentsEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "TasksEnabled", "true");
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_google_provider_init (GoaGoogleProvider *client)
{
}

static void
goa_google_provider_class_init (GoaGoogleProviderClass *klass)
{
  GoaProviderClass *provider_class;
  GoaOAuth2ProviderClass *oauth2_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->build_object               = build_object;
  provider_class->ensure_credentials_sync    = ensure_credentials_sync;
  provider_class->show_account               = show_account;
  provider_class->get_credentials_generation = get_credentials_generation;

  oauth2_class = GOA_OAUTH2_PROVIDER_CLASS (klass);
  oauth2_class->get_authentication_cookie = get_authentication_cookie;
  oauth2_class->get_authorization_uri     = get_authorization_uri;
  oauth2_class->get_client_id             = get_client_id;
  oauth2_class->get_client_secret         = get_client_secret;
  oauth2_class->get_identity_sync         = get_identity_sync;
  oauth2_class->get_redirect_uri          = get_redirect_uri;
  oauth2_class->get_scope                 = get_scope;
  oauth2_class->is_deny_node              = is_deny_node;
  oauth2_class->is_identity_node          = is_identity_node;
  oauth2_class->is_password_node          = is_password_node;
  oauth2_class->get_token_uri             = get_token_uri;
  oauth2_class->get_use_mobile_browser    = get_use_mobile_browser;
  oauth2_class->add_account_key_values    = add_account_key_values;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in a thread dedicated to handling @invocation */
static gboolean
on_handle_get_password (GoaPasswordBased      *interface,
                        GDBusMethodInvocation *invocation,
                        const gchar           *id, /* unused */
                        gpointer               user_data)
{
  GoaObject *object;
  GoaAccount *account;
  GoaProvider *provider;
  GError *error;
  GVariant *credentials;
  const gchar *identity;
  gchar *password;

  /* TODO: maybe log what app is requesting access */

  password = NULL;
  credentials = NULL;

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (interface)));
  account = goa_object_peek_account (object);
  identity = goa_account_get_identity (account);
  provider = goa_provider_get_for_provider_type (goa_account_get_provider_type (account));

  error = NULL;
  credentials = goa_utils_lookup_credentials_sync (provider,
                                                   object,
                                                   NULL, /* GCancellable* */
                                                   &error);
  if (credentials == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!g_variant_lookup (credentials, "password", "s", &password))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             GOA_ERROR,
                                             GOA_ERROR_FAILED, /* TODO: more specific */
                                             _("Did not find password with identity `%s' in credentials"),
                                             identity);
      goto out;
    }

  goa_password_based_complete_get_password (interface, invocation, password);

 out:
  g_free (password);
  g_clear_pointer (&credentials, (GDestroyNotify) g_variant_unref);
  g_object_unref (provider);
  return TRUE; /* invocation was handled */
}
