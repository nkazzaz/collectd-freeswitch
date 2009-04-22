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
#include <pthread.h>
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

static pthread_t esl_thread;
static int esl_thread_init = 0;

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

	/* Set some default configuration variables */
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

	/* Connect from freeswitch_init for a persistent ESL connection */
	if (esl_connect(&handle, host, atoi(port), password)) {
		DEBUG ("Error connecting to FreeSWITCH ESL interface [%s]\n", handle.err);
		return -1;
	}

	esl_send_recv(&handle, "api show channels\n\n");

	if (handle.last_sr_event && handle.last_sr_event->body) {
		// handle.last_sr_event->body now contains the string with all active channels...
	}
	
	/* Disconnect from freeswitch_shutdown for a persistent ESL connection */
	esl_disconnect(&handle);

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

static void *esl_child_loop (void __attribute__((unused)) *dummy)
{

	DEBUG ("child is exiting");

	esl_thread_init = 0;
	pthread_exit (NULL);

	return (NULL);
} /* void *esl_child_loop */

static int freeswitch_init (void)
{
	/* clean up an old thread */
	int status;

/*
        pthread_mutex_lock (&traffic_mutex);
        tr_queries   = 0;
        tr_responses = 0;
        pthread_mutex_unlock (&traffic_mutex);
*/

	if (esl_thread_init != 0)
		return (-1);

	status = pthread_create (&esl_thread, NULL, esl_child_loop,
			(void *) 0);
	if (status != 0)
	{
		char errbuf[1024];
		ERROR ("freeswitch plugin: pthread_create failed: %s",
			sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	esl_thread_init = 1;

	return(0);
} /* int freeswitch_init */

static int freeswitch_shutdown (void)
{
	return(0);
} /* int freeswitch_shutdown */

void module_register (void)
{
        plugin_register_config ("freeswitch", freeswitch_config,
			config_keys, config_keys_num);
	plugin_register_init ("freeswitch", freeswitch_init);
	plugin_register_shutdown ("freeswitch", freeswitch_shutdown);
	plugin_register_read ("freeswitch", freeswitch_read);
} /* void module_register */
