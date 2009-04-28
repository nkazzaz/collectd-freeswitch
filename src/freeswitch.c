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
#include "utils_match.h"
#include "esl.h"

#define FS_DEF_HOST "127.0.0.1"
#define FS_DEF_PORT "8021"
#define FS_DEF_PASS "ClueCon"

/*
 *	<Plugin freeswitch>
 *		Host "127.0.0.1"
 *		Port "8021"
 *		Pass "ClueCon"
 *		<Command "api sofia status profile res-public">
 *			Instance "profile-sofia-res-public"
 *			<Match>
 *				Instance "calls-in"
 *				Regex "CALLS-IN\\s+([0-9]+)"
 *				DSType "GaugeLast"
 *				Type "gauge"
 *			</Match>
 *		</Command>
 *	</Plugin>
 */

/*
 * Data types
 */
struct fs_match_s;
typedef struct fs_match_s fs_match_t;
struct fs_match_s
{
	char *regex;
	int dstype;
	char *type;
	char *instance;
	cu_match_t *match;
	fs_match_t *next;
};

struct fs_command_s;
typedef struct fs_command_s fs_command_t;
struct fs_command_s
{
	char *line;		// "api sofia status profile res-public"
	char *instance;		// "profile-sofia-res-public"
	char *buffer;		// <output from esl command as a char*>
	size_t buffer_size;	// strlen(*buffer)+3
	size_t buffer_fill;	// 0 or 1
	fs_match_t *matches;
	fs_command_t *next;
};

static fs_command_t *fs_commands_g = NULL;

static char *fs_host = NULL;
static char *fs_port = NULL;
static char *fs_pass = NULL;

static esl_handle_t esl_handle = {{0}};
// static int thread_running = 0; // for when subscribing to esl events

/*
 * Private functions
 */

static int fs_config_add_string (const char *name, char **dest, oconfig_item_t *ci)
{
	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		WARNING ("freeswitch plugin: '%s' needs exactly one string argument.", name);
		return (-1);
	}

	sfree (*dest);
	*dest = strdup (ci->values[0].value.string);
	if (*dest == NULL)
		return (-1);

	return (0);
} /* int fs_config_add_string */

static void fs_match_free (fs_match_t *fm)
{
	if (fm == NULL)
		return;

	sfree (fm->regex);
	sfree (fm->type);
	sfree (fm->instance);
	match_destroy (fm->match);
	fs_match_free (fm->next);
	sfree (fm);
} /* void fs_match_free */

static void fs_command_free (fs_command_t *fc)
{
	if (fc == NULL)
		return;

	sfree (fc->line);
	sfree (fc->instance);
	sfree (fc->buffer);
	fs_match_free (fc->matches);
	fs_command_free (fc->next);
	sfree (fc);
} /* void fs_command_free */

static int fs_config_add_match_dstype (int *dstype_ret, oconfig_item_t *ci)
{
	int dstype;

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		WARNING ("freeswitch plugin: 'DSType' needs exactly one string argument.");
		return (-1);
	}

	if (strncasecmp ("Gauge", ci->values[0].value.string, strlen ("Gauge")) == 0)
	{
		dstype = UTILS_MATCH_DS_TYPE_GAUGE;
		if (strcasecmp ("GaugeAverage", ci->values[0].value.string) == 0)
			dstype |= UTILS_MATCH_CF_GAUGE_AVERAGE;
		else if (strcasecmp ("GaugeMin", ci->values[0].value.string) == 0)
			dstype |= UTILS_MATCH_CF_GAUGE_MIN;
		else if (strcasecmp ("GaugeMax", ci->values[0].value.string) == 0)
			dstype |= UTILS_MATCH_CF_GAUGE_MAX;
		else if (strcasecmp ("GaugeLast", ci->values[0].value.string) == 0)
			dstype |= UTILS_MATCH_CF_GAUGE_LAST;
		else
			dstype = 0;
	}
	else if (strncasecmp ("Counter", ci->values[0].value.string, strlen ("Counter")) == 0)
	{
		dstype = UTILS_MATCH_DS_TYPE_COUNTER;
		if (strcasecmp ("CounterSet", ci->values[0].value.string) == 0)
			dstype |= UTILS_MATCH_CF_COUNTER_SET;
		else if (strcasecmp ("CounterAdd", ci->values[0].value.string) == 0)
			dstype |= UTILS_MATCH_CF_COUNTER_ADD;
		else if (strcasecmp ("CounterInc", ci->values[0].value.string) == 0)
			dstype |= UTILS_MATCH_CF_COUNTER_INC;
		else
			dstype = 0;
	}
	else
	{
		dstype = 0;
	}

	if (dstype == 0)
	{
		WARNING ("freeswitch plugin: `%s' is not a valid argument to `DSType'.",
		ci->values[0].value.string);
		return (-1);
	}

	*dstype_ret = dstype;
	return (0);
} /* int fs_config_add_match_dstype */

static int fs_config_add_match (fs_command_t *fs_command, oconfig_item_t *ci)
{
	fs_match_t *fs_match;
	int status;
	int i;

	if (ci->values_num != 0)
	{
		WARNING ("freeswitch plugin: Ignoring arguments for the 'Match' block.");
	}

	fs_match = (fs_match_t *) malloc (sizeof (*fs_match));
	if (fs_match == NULL)
	{
		ERROR ("freeswitch plugin: malloc failed.");
		return (-1);
	}
	memset (fs_match, 0, sizeof (*fs_match));

	status = 0;
	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("Regex", child->key) == 0)
			status = fs_config_add_string ("Regex", &fs_match->regex, child);
		else if (strcasecmp ("DSType", child->key) == 0)
			status = fs_config_add_match_dstype (&fs_match->dstype, child);
		else if (strcasecmp ("Type", child->key) == 0)
			status = fs_config_add_string ("Type", &fs_match->type, child);
		else if (strcasecmp ("Instance", child->key) == 0)
			status = fs_config_add_string ("Instance", &fs_match->instance, child);
		else
		{
			WARNING ("freeswitch plugin: Option `%s' not allowed here.", child->key);
			status = -1;
		}

		if (status != 0)
			break;
	} /* for (i = 0; i < ci->children_num; i++) */

	while (status == 0)
	{
		if (fs_match->regex == NULL)
		{
			WARNING ("freeswitch plugin: `Regex' missing in `Match' block.");
			status = -1;
		}

		if (fs_match->type == NULL)
		{
			WARNING ("freeswitch plugin: `Type' missing in `Match' block.");
			status = -1;
		}

		if (fs_match->dstype == 0)
		{
			WARNING ("freeswitch plugin: `DSType' missing in `Match' block.");
			status = -1;
		}

		break;
	} /* while (status == 0) */

	if (status != 0)
	return (status);

	fs_match->match = match_create_simple (fs_match->regex, fs_match->dstype);
	if (fs_match->match == NULL)
	{
		ERROR ("freeswitch plugin: tail_match_add_match_simple failed.");
		fs_match_free (fs_match);
		return (-1);
	}
	else
	{
		fs_match_t *prev;

		prev = fs_command->matches;
		while ((prev != NULL) && (prev->next != NULL))
			prev = prev->next;

		if (prev == NULL)
			fs_command->matches = fs_match;
		else
			prev->next = fs_match;
	}

	return (0);
} /* int fs_config_add_match */

static int fs_config_add_command (oconfig_item_t *ci)
{
	fs_command_t *command;
	int status;
	int i;

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		WARNING ("freeswitch plugin: 'Command' blocks need exactly one string argument.");
		return (-1);
	}

	command = (fs_command_t *) malloc (sizeof (*command));
	if (command == NULL)
	{
		ERROR ("freeswitch plugin: malloc failed.");
		return (-1);
	}
	memset (command, 0, sizeof (*command));

	command->line = NULL;
	command->line = strdup (ci->values[0].value.string);

	if (command->line == NULL)
	{
		ERROR ("freeswitch plugin: strdup failed.");
		sfree (command);
		return (-1);
	}

	/* Process all children */
	status = 0;
	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("Instance", child->key) == 0)
			status = fs_config_add_string ("Instance", &command->instance, child);
		else if (strcasecmp ("Match", child->key) == 0)
			fs_config_add_match (command, child);
		else
		{
			WARNING ("freeswitch plugin: Option '%s' not allowed here.", child->key);
			status = -1;
		}

		if (status != 0)
			break;
	}

	if (status != 0)
	{
		fs_command_free (command);
		return (status);
	}

	/* Add the new command to the linked list */
	if (fs_commands_g == NULL)
		fs_commands_g = command;
	else
	{
		fs_command_t *prev;

		prev = fs_commands_g;
		while ((prev != NULL) && (prev->next != NULL))
			prev = prev->next;
		prev->next = command;
	}

	return (0);
} /* int fs_config_add_command */

static int fs_complex_config (oconfig_item_t *ci)
{
	int success;
	int errors;
	int status;
	int i;

	success = 0;
	errors = 0;

	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("Host", child->key) == 0)
		{
			if (fs_host != NULL) free (fs_host);
			fs_host = strdup(child->values[0].value.string);
		}
		else if (strcasecmp ("Port", child->key) == 0)
		{
			if (fs_port != NULL) free (fs_port);
			fs_port = strdup(child->values[0].value.string);
		}
		else if (strcasecmp ("Pass", child->key) == 0)
		{
			if (fs_pass != NULL) free (fs_pass);
			fs_pass = strdup(child->values[0].value.string);
		}
		else if (strcasecmp ("Command", child->key) == 0)
		{
			status = fs_config_add_command(child);
			if (status == 0)
				success++;
			else
				errors++;
		}
		else
		{
			WARNING ("freeswitch plugin: Option '%s' not allowed here.", child->key);
			errors++;
		}
	}

	if ((success == 0) && (errors > 0))
	{
		ERROR ("freeswitch plugin: All statements failed.");
		return (-1);
	}

	return (0);
} /* int fs_complex_config */

static void fs_submit (const fs_command_t *fc,
	const fs_match_t *fm, const cu_match_value_t *mv)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0] = mv->value;

	vl.values = values;
	vl.values_len = 1;
	vl.time = time (NULL);

	strncpy (vl.host, hostname_g, sizeof (vl.host));
	strncpy (vl.plugin, "freeswitch", sizeof (vl.plugin));
	strncpy (vl.plugin_instance, fc->instance, sizeof (vl.plugin_instance));
	strncpy (vl.type, fm->type, sizeof (vl.type));
	strncpy (vl.type_instance, fm->instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void fs_submit */

static int fs_read_command (fs_command_t *fc)
{
	fs_match_t *fm;
	int status;

	/* can't the following be done nicer ? */
	char *line;
	line = (char *) malloc (strlen(fc->line)+3);
	snprintf(line, strlen(fc->line)+3, "%s\n\n", fc->line);
	esl_send_recv(&esl_handle, line);

	fc->buffer_fill = 0;

	if (esl_handle.last_sr_event && esl_handle.last_sr_event->body)
	{
		sfree(fc->buffer);
		fc->buffer = strdup(esl_handle.last_sr_event->body);
		fc->buffer_size = strlen(fc->buffer);
		fc->buffer_fill = 1;
	}

	for (fm = fc->matches; fm != NULL; fm = fm->next)
	{
		cu_match_value_t *mv;

		status = match_apply (fm->match, fc->buffer);
		if (status != 0)
		{
			WARNING ("freeswitch plugin: match_apply failed.");
			continue;
		}

		mv = match_get_user_data (fm->match);
		if (mv == NULL)
		{
			WARNING ("freeswitch plugin: match_get_user_data returned NULL.");
			continue;
		}

		fs_submit (fc, fm, mv);
	} /* for (fm = fc->matches; fm != NULL; fm = fm->next) */

	return (0);
} /* int fs_read_command */

static int fs_read (void)
{
	fs_command_t *fc;

	for (fc = fs_commands_g; fc != NULL; fc = fc->next)
		fs_read_command (fc);

	return (0);
} /* int fs_read */

/*
static void *msg_thread_run(esl_thread_t *me, void *obj)
{
	esl_handle_t *esl_handle = (esl_handle_t *) obj;
	thread_running = 1;

	// Maybe do some more in this loop later, like receive subscribed events,
	// and create statistics of them
	// see fs_cli.c function static void *msg_thread_run(), around line 198
	while (thread_running && esl_handle->connected)
	{
		esl_status_t status = esl_recv_event_timed(esl_handle, 10, 1, NULL);
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

static int fs_init (void)
{
	/* Set some default configuration variables */
	if (fs_host == NULL) fs_host = FS_DEF_HOST;
	if (fs_port == NULL) fs_port = FS_DEF_PORT;
	if (fs_pass == NULL) fs_pass = FS_DEF_PASS;

	/* Connect to FreeSWITCH over ESL */
	DEBUG ("freeswitch plugin: making ESL connection to %s %s %s\n", fs_host, fs_port, fs_pass);
	esl_connect(&esl_handle, fs_host, atoi(fs_port), fs_pass);

	/* Start a seperate thread for incoming events here */
	//esl_thread_create_detached(msg_thread_run, &esl_handle);

	return(0);
} /* int fs_init */

static int fs_shutdown (void)
{
	DEBUG ("freeswitch plugin: disconnecting");
	esl_disconnect(&esl_handle);
	fs_command_free (fs_commands_g);
	fs_commands_g = NULL;
	return (0);
} /* int fs_shutdown */

void module_register (void)
{
	plugin_register_complex_config ("freeswitch", fs_complex_config);
	plugin_register_init ("freeswitch", fs_init);
	plugin_register_read ("freeswitch", fs_read);
	plugin_register_shutdown ("freeswitch", fs_shutdown);
} /* void module_register */
