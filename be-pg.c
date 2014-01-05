/*
 * Copyright (c) 2013 Jan-Piet Mens <jpmens()gmail.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of mosquitto nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef BE_PG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>
#include "be-pg.h"
#include "log.h"
#include "hash.h"
#include "backends.h"

struct pg_backend {
        PGconn *pg;
	char *host;
	int port;
        char *conninfo;
	char *dbname;
	char *user;
	char *pass;
        char *userquery;        // MUST return 1 row, 1 column
        char *superquery;       // MUST return 1 row, 1 column, [0, 1]
        char *aclquery;         // MAY return n rows, 1 column, string
};

void *be_pg_init()
{
	struct pg_backend *conf;
	char *host, *user, *pass, *dbname, *p;
	char *userquery;
	int port;

	_log(LOG_DEBUG, "}}}} PG");

	host		= p_stab("host");
	p		= p_stab("port");
	user		= p_stab("user");
	pass		= p_stab("pass");
	dbname		= p_stab("dbname");

	host = (host) ? host : strdup("localhost");
	port = (!p) ? 5432 : atoi(p);

	userquery = p_stab("userquery");

	if (!userquery) {
		_fatal("Mandatory option 'userquery' is missing");
		return (NULL);
	}

	if ((conf = (struct pg_backend *)malloc(sizeof(struct pg_backend))) == NULL)
		return (NULL);

	conf->host		= host;
	conf->port		= port;
	conf->user		= user;
	conf->pass		= pass;
	conf->dbname		= dbname;
	conf->userquery		= userquery;
	conf->superquery	= p_stab("superquery");
	conf->aclquery		= p_stab("aclquery");

        //sprintf(conninfo, "dbname = %s" , conf->dbname);

	conf->conninfo          = "dbname = mosquitto"; //conninfo;

	if (!(conf->pg = PQconnectdb(conf->conninfo))) {
		fprintf(stderr, "%s\n", PQerrorMessage(conf->pg));
		PQfinish(conf->pg);
		free(conf);
		return (NULL);
	}

	return ((void *)conf);
}

void be_pg_destroy(void *handle)
{
	struct pg_backend *conf = (struct pg_backend *)handle;

	if (conf) {
                PQfinish(conf->pg);
               
		if (conf->userquery)
			free(conf->userquery);
		if (conf->superquery)
			free(conf->superquery);
		if (conf->aclquery)
			free(conf->aclquery);
		free(conf);
	}
}

static char *escape(void *handle, const char *value, long *vlen)
{
        struct pg_backend *conf = (struct pg_backend *)handle;
        char *v;

        //*vlen = strlen(value) * 2 + 1;
        //if ((v = malloc(*vlen)) == NULL)
        //        return (NULL);

        v = PQescapeLiteral(conf->pg, value, strlen(value));

        return (v);
}

char *be_pg_getuser(void *handle, const char *username)
{
	struct pg_backend *conf = (struct pg_backend *)handle;
	char *query = NULL, *u = NULL, *value = NULL, *v;
	long nrows, ulen;
	PGresult *res = NULL;

	if (!conf || !conf->userquery || !username || !*username)
		return (NULL);

        if ((u = escape(conf, username, &ulen)) == NULL) {
		return (NULL);
        }

	if ((query = malloc(strlen(conf->userquery) + ulen + 128)) == NULL) {
		PQfreemem(u);
		return (NULL);
	}

	sprintf(query, conf->userquery, u);
	PQfreemem(u);

	// DEBUG puts(query);

	res = PQexec(conf->pg, query);

	/* Command OK, but no data (No such user) */
	if (PQresultStatus(res) == PGRES_COMMAND_OK) {
	        fprintf(stdout, "No such user %s", username);
	        goto out;
	}

	if (PQresultStatus(res) != PGRES_TUPLES_OK ) {
		fprintf(stderr, "%s\n", PQerrorMessage(conf->pg));
		goto out;
	}

	if ((nrows = PQntuples(res)) != 1) {
		fprintf(stderr, "rowcount = %ld; not ok\n", nrows);
		goto out;
	}

	if (PQnfields(res) != 1) {
		fprintf(stderr, "numfields not ok\n");
		goto out;
	}

	if ((v = PQgetvalue(res, 0, 0)) == NULL) {
		goto out;
	}

	value = (v) ? strdup(v) : NULL;

   out:

	PQclear(res);
	free(query);

	return (value);
}

/*
 * Return T/F if user is superuser
 */

int be_pg_superuser(void *handle, const char *username)
{
	struct pg_backend *conf = (struct pg_backend *)handle;
	char *query = NULL, *u = NULL, *data = NULL;
	long nrows, ulen;
	int issuper = FALSE;
	PGresult *res = NULL;


	if (!conf || !conf->superquery)
		return (FALSE);

	if ((u = escape(conf, username, &ulen)) == NULL)
		return (FALSE);

	if ((query = malloc(strlen(conf->superquery) + ulen + 128)) == NULL) {
		PQfreemem(u);
		return (FALSE);
	}
	sprintf(query, conf->superquery, u);
	PQfreemem(u);

	// puts(query);

	res = PQexec(conf->pg, query);
	/* Command OK, but no data (No such user) */
	if (PQresultStatus(res) == PGRES_COMMAND_OK) {
	        // DEBUG fprintf(stdout, "No such user %s", username)
	        goto out;
	}
	if (PQresultStatus(res) != PGRES_TUPLES_OK ) {
		fprintf(stderr, "%s\n", PQerrorMessage(conf->pg));
		goto out;
	}

	if ((nrows = PQntuples(res)) != 1) {
		goto out;
	}

	if (PQnfields(res) != 1) {
		// DEBUG fprintf(stderr, "numfields not ok\n");
		goto out;
	}

	if ((data = PQgetvalue(res, 0, 0)) == NULL) {
		goto out;
	}

	issuper = atoi(data);

   out:

	PQclear(res);
	free(query);

	return (issuper);
}

/*
 * Check ACL.
 * username is the name of the connected user attempting
 * to access
 * topic is the topic user is trying to access (may contain
 * wildcards)
 * acc is desired type of access: read/write
 *	for subscriptions (READ) (1)
 *	for publish (WRITE) (2)
 *
 * SELECT topic FROM table WHERE username = %s AND (acc & %d)		// may user SUB or PUB topic?
 * SELECT topic FROM table WHERE username = %s              		// ignore ACC
 */

int be_pg_aclcheck(void *handle, const char *username, const char *topic, int acc)
{
	struct pg_backend *conf = (struct pg_backend *)handle;
	char *query = NULL, *u = NULL, *v;
	long ulen;
	int match = 0;
	bool bf;
	PGresult *res = NULL;

	if (!conf || !conf->aclquery)
		return (FALSE);

        if ((u = escape(conf, username, &ulen)) == NULL)
		return (FALSE);

	if ((query = malloc(strlen(conf->aclquery) + ulen + 128)) == NULL) {
		PQfreemem(u);
		return (FALSE);
	}
	sprintf(query, conf->aclquery, u, acc);
	PQfreemem(u);

	_log(LOG_DEBUG, "SQL: %s", query);

	res = PQexec(conf->pg, query);
	if (PQresultStatus(res) == PGRES_COMMAND_OK) {
		_log(LOG_NOTICE, "%s", PQerrorMessage(conf->pg));
		goto out;
	}

	if (PQnfields(res) != 1) {
		fprintf(stderr, "numfields not ok\n");
		goto out;
	}

        int row = 0;
	while (match == 0 && (v = PQgetvalue(res, row, 0)) != NULL) {
		if (v != NULL) {

			/* Check mosquitto_match_topic. If true,
			 * if true, set match and break out of loop. */

			// FIXME: does this need special work for %c and %u ???
			mosquitto_topic_matches_sub(v, topic, &bf);
			match |= bf;
			_log(LOG_DEBUG, "  pg: topic_matches(%s, %s) == %d",
				topic, v, bf);
		}
		row++;
	}

   out:

	PQclear(res);
	free(query);
	
	return (match);
}
#endif /* BE_PG */
