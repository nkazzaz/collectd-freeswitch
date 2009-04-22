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
 *   Leon de Rooij <leon@scarlet-internet.nl>
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

static esl_handle_t handle = {{0}};
// static int thread_running = 0;

static char *freeswitch_host = NULL;
static char *freeswitch_port = NULL;
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
/*
	const char *host;
	const char *port;
	const char *password;
*/


	esl_send_recv(&handle, "api show channels\n\n");
	if (handle.last_sr_event && handle.last_sr_event->body) {
		DEBUG ("OUTPUT FROM FREESWITCH:\n%s\n\n", handle.last_sr_event->body);
	}

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
		if (freeswitch_port != NULL)
			free (freeswitch_port);
		freeswitch_port = strdup (value);
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

/*
static void *msg_thread_run(esl_thread_t *me, void *obj)
{
	esl_handle_t *handle = (esl_handle_t *) obj;
	thread_running = 1;

	// Maybe do some more in this loop later, like receive subscribed events,
	// and create statistics of them
	// see fs_cli.c function static void *msg_thread_run(), around line 198
	while (thread_running && handle->connected)
	{
		esl_status_t status = esl_recv_event_timed(handle, 10, 1, NULL);
		if (status == ESL_FAIL)
		{
			//DEBUG ("Disconnected [%s]\n", ESL_LOG_WARNING); // todo fixit
			DEBUG ("Disconnected [%s]\n", "ESL_LOG_WARNING");
			thread_running = 0;
		}
		usleep(1000);
	}

	thread_running = 0;
	return (NULL);
} */ /* void *msg_thread_run */

static int freeswitch_init (void)
{
	/* Set some default configuration variables */
	if (freeswitch_host == NULL)
		freeswitch_host = FREESWITCH_DEF_HOST;

	if (freeswitch_port == NULL)
		freeswitch_port = "8021";

	if (freeswitch_password == NULL)
		freeswitch_password = FREESWITCH_DEF_PASSWORD;

	/* Connect to FreeSWITCH over ESL */
	DEBUG ("Making ESL connection to %s %s %s\n", freeswitch_host, freeswitch_port, freeswitch_password);
	esl_connect(&handle, freeswitch_host, atoi(freeswitch_port), freeswitch_password);

	/* Start a seperate thread for incoming events */
	//esl_thread_create_detached(msg_thread_run, &handle);

	return(0);
} /* int freeswitch_init */

void module_register (void)
{
        plugin_register_config ("freeswitch", freeswitch_config,
			config_keys, config_keys_num);
	plugin_register_init ("freeswitch", freeswitch_init);
	plugin_register_read ("freeswitch", freeswitch_read);
} /* void module_register */
