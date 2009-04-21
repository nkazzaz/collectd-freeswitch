/**
 * collectd - src/freeswitch.c
 * Copyright (C) 2005-2007  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

/*
#include "utils_match.h"
*/
#include <esl.h>

#define FREESWITCH_DEF_HOST "127.0.0.1"
#define FREESWITCH_DEF_PORT "8021"
#define FREESWITCH_DEF_PASSWORD "ClueCon"

static const char *config_keys[] = 
{
	"Host",
	"Port",
	"Password"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

typedef struct profilename
{
	char *name;
	struct profilename *next;
} profilename_t;

// static profilename_t *first_profilename = NULL;
static char *freeswitch_host = NULL;
static char freeswitch_port[16];
static char *freeswitch_password = NULL;

static void freeswitch_submit (const char *profile, const char *type, gauge_t inbound, gauge_t outbound)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = inbound;
	values[1].gauge = outbound;

	vl.values = values;
	vl.values_len = 2;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "freeswitch", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
        sstrncpy (vl.type_instance, profile, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void freeswitch_submit */

static int freeswitch_read (void)
{
	const char *host;
	const char *port;
	const char *password;

	host = freeswitch_host;
	if (host == NULL)
		host = FREESWITCH_DEF_HOST;

	port = freeswitch_port;
	if (port == NULL)
		port = FREESWITCH_DEF_PORT;

	password = freeswitch_password;
	if (password == NULL)
		password = FREESWITCH_DEF_PASSWORD;

	esl_handle_t handle = {{0}};
	esl_connect(&handle, host, atoi(port), password);

	esl_send_recv(&handle, "api show channels\n\n");
	
	// DO YOUR THING HERE TO PARSE &handle
	
	esl_disconnect(&handle);

DEBUG ("XFreeSWITCH SUBMIT: res-public fs_channels 3 5");

	freeswitch_submit ("res-public", "fs_channels", 3, 5);

	return (0);
} /* int freeswitch_read */

static int freeswitch_config (const char *key, const char *value)
{
        if (strcasecmp ("Host", key) == 0)
        {
		if (freeswitch_host != NULL)
                	free (freeswitch_host);
                freeswitch_host = strdup (value);
        }
        else if (strcasecmp ("Port", key) == 0)
	{
		int port = (int) (atof (value));
		if ((port > 0) && (port <= 65535))
			ssnprintf (freeswitch_port, sizeof (freeswitch_port),
					"%i", port);
		else
			sstrncpy (freeswitch_port, value, sizeof (freeswitch_port));
	}
        else if (strcasecmp ("Password", key) == 0)
        {
		if (freeswitch_password != NULL)
                	free (freeswitch_password);
                freeswitch_password = strdup (value);
        }
        else
        {
                return (-1);
        }
        return (0);
} /* int freeswitch_config */

void module_register (void)
{
        plugin_register_config ("freeswitch", freeswitch_config,
			config_keys, config_keys_num);
	plugin_register_read ("freeswitch", freeswitch_read);
} /* void module_register */
