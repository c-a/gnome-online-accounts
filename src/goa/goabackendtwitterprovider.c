/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <rest/oauth-proxy.h>
#include <json-glib/json-glib.h>

#include "goabackendprovider.h"
#include "goabackendoauthprovider.h"
#include "goabackendtwitterprovider.h"

/**
 * GoaBackendTwitterProvider:
 *
 * The #GoaBackendTwitterProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendTwitterProvider
{
  /*< private >*/
  GoaBackendOAuthProvider parent_instance;
};

typedef struct _GoaBackendTwitterProviderClass GoaBackendTwitterProviderClass;

struct _GoaBackendTwitterProviderClass
{
  GoaBackendOAuthProviderClass parent_class;
};

/**
 * SECTION:goabackendtwitterprovider
 * @title: GoaBackendTwitterProvider
 * @short_description: A provider for Twitter
 *
 * #GoaBackendTwitterProvider is used for handling Twitter accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaBackendTwitterProvider, goa_backend_twitter_provider, GOA_TYPE_BACKEND_OAUTH_PROVIDER,
                         g_io_extension_point_implement (GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "twitter",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaBackendProvider *_provider)
{
  return "twitter";
}

static const gchar *
get_name (GoaBackendProvider *_provider)
{
  return _("Twitter Account");
}

static const gchar *
get_consumer_key (GoaBackendOAuthProvider *provider)
{
  return "tlVEAXvkgqr0VUFyqVQ";
}

static const gchar *
get_consumer_secret (GoaBackendOAuthProvider *provider)
{
  return "RN2FBARWy7scDmWFwfhIA6Qwf6kPYxZ0PIpVWzgpdU";
}

static const gchar *
get_request_uri (GoaBackendOAuthProvider *provider)
{
  return "https://api.twitter.com/oauth/request_token";
}

static gchar **
get_request_uri_params (GoaBackendOAuthProvider *provider)
{
  return NULL;
  GPtrArray *p;
  p = g_ptr_array_new ();
  g_ptr_array_add (p, g_strdup ("xoauth_displayname"));
  g_ptr_array_add (p, g_strdup ("GNOME"));

  g_ptr_array_add (p, g_strdup ("scope"));
  g_ptr_array_add (p, g_strdup (
    /* Display email address: cf. https://sites.twitter.com/site/oauthgoog/Home/emaildisplayscope */
    "https://www.twitterapis.com/auth/userinfo#email "
    /* IMAP, SMTP access: http://code.twitter.com/apis/gmail/oauth/protocol.html */
    "https://mail.twitter.com/ "
    /* Calendar data API: http://code.twitter.com/apis/calendar/data/2.0/developers_guide.html */
    "https://www.twitter.com/calendar/feeds"));
  g_ptr_array_add (p, NULL);
  return (gchar **) g_ptr_array_free (p, FALSE);
}


static const gchar *
get_authorization_uri (GoaBackendOAuthProvider *provider)
{
  return "https://api.twitter.com/oauth/authorize";
}

static const gchar *
get_token_uri (GoaBackendOAuthProvider *provider)
{
  return "https://api.twitter.com/oauth/access_token";
}

static const gchar *
get_callback_uri (GoaBackendOAuthProvider *provider)
{
  return "https://www.gnome.org/goa-1.0/oauth";
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_identity_sync (GoaBackendOAuthProvider  *provider,
                   const gchar              *access_token,
                   const gchar              *access_token_secret,
                   gchar                   **out_name,
                   GCancellable             *cancellable,
                   GError                  **error)
{
  RestProxy *proxy;
  RestProxyCall *call;
  JsonParser *parser;
  JsonObject *json_object;
  gchar *ret;
  gchar *id;
  gchar *name;

  ret = NULL;
  proxy = NULL;
  call = NULL;
  parser = NULL;
  id = NULL;
  name = NULL;

  /* TODO: cancellable */

  proxy = oauth_proxy_new_with_token (goa_backend_oauth_provider_get_consumer_key (provider),
                                      goa_backend_oauth_provider_get_consumer_secret (provider),
                                      access_token,
                                      access_token_secret,
                                      "https://api.twitter.com/1/account/verify_credentials.json",
                                      FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "GET");

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
                                   error))
    {
      g_prefix_error (error, _("Error parsing response as JSON: "));
      goto out;
    }

  json_object = json_node_get_object (json_parser_get_root (parser));
  id = g_strdup (json_object_get_string_member (json_object, "id_str"));
  if (id == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find id_str member in JSON data"));
      goto out;
    }
  name = g_strdup (json_object_get_string_member (json_object, "screen_name"));
  if (name == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find screen_name member in JSON data"));
      goto out;
    }

  ret = id;
  id = NULL;
  if (out_name != NULL)
    {
      *out_name = name;
      name = NULL;
    }

 out:
  g_free (id);
  g_free (name);
  if (call != NULL)
    g_object_unref (call);
  if (proxy != NULL)
    g_object_unref (proxy);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_backend_twitter_provider_build_object (GoaBackendProvider  *provider,
                                          GoaObjectSkeleton   *object,
                                          GKeyFile            *key_file,
                                          const gchar         *group,
                                          GError             **error)
{
  GoaAccount *account;
  GoaTwitterAccount *twitter_account;
  gboolean ret;
  gchar *id;

  id = NULL;
  account = NULL;
  twitter_account = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_BACKEND_PROVIDER_CLASS (goa_backend_twitter_provider_parent_class)->build_object (provider,
                                                                                            object,
                                                                                            key_file,
                                                                                            group,
                                                                                            error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));
  twitter_account = goa_object_get_twitter_account (GOA_OBJECT (object));
  if (twitter_account == NULL)
    {
      twitter_account = goa_twitter_account_skeleton_new ();
      goa_object_skeleton_set_twitter_account (object, twitter_account);
    }

  id = g_key_file_get_string (key_file, group, "Identity", NULL);
  if (id == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Invalid identity %s for id %s",
                   id,
                   goa_account_get_id (account));
      goto out;
    }

  goa_twitter_account_set_id (twitter_account, id);

  ret = TRUE;

 out:
  g_free (id);
  if (twitter_account != NULL)
    g_object_unref (twitter_account);
  if (account != NULL)
    g_object_unref (account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
get_use_external_browser (GoaBackendOAuthProvider *provider)
{
  /* For some reason this only works in a browser - bad callback URL? TODO: investigate */
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_twitter_provider_init (GoaBackendTwitterProvider *client)
{
}

static void
goa_backend_twitter_provider_class_init (GoaBackendTwitterProviderClass *klass)
{
  GoaBackendProviderClass *provider_class;
  GoaBackendOAuthProviderClass *oauth_class;

  provider_class = GOA_BACKEND_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_name                   = get_name;
  provider_class->build_object               = goa_backend_twitter_provider_build_object;

  oauth_class = GOA_BACKEND_OAUTH_PROVIDER_CLASS (klass);
  oauth_class->get_identity_sync        = get_identity_sync;
  oauth_class->get_consumer_key         = get_consumer_key;
  oauth_class->get_consumer_secret      = get_consumer_secret;
  oauth_class->get_request_uri          = get_request_uri;
  oauth_class->get_request_uri_params   = get_request_uri_params;
  oauth_class->get_authorization_uri    = get_authorization_uri;
  oauth_class->get_token_uri            = get_token_uri;
  oauth_class->get_callback_uri         = get_callback_uri;
  oauth_class->get_use_external_browser = get_use_external_browser;
}