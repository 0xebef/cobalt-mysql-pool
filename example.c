/*
 * cobalt-mysql-pool example
 *
 * This is a little example project to demonstrate the usage of the
 * cobalt-mysql-pool module
 *
 * https://github.com/0xebef/cobalt-mysql-pool
 *
 * License: LGPLv3 or later
 *
 * Copyright (c) 2018, 0xebef
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include <mysql.h>
#include <mysqld_error.h>
#include "cobalt-mysql-pool.h"

/*
 * the database connections settings
 */
#define DB_HOST            "localhost"
#define DB_USERNAME        "user"
#define DB_PASSWORD        "pass"
#define DB_DATABASE        "test"
#define DB_PORT            (3306U)
#define DB_UNIX_SOCKET     (NULL)
#define DB_CLIENT_FLAG     (0UL)

/*
 * the destructor function
 */
static void clean_up(void);

/*
 * a simple query example
 */
static int simple_query_example(void);

/*
 * a helper function to make database queries and deal with possible
 * errors
 */
static int q(MYSQL *mysql_conn, const char *q);

int main(int argc, char *argv[])
{
    /*
     * set up the destructor
     */
    if (atexit(clean_up) != 0) {
        fprintf(stderr, "atexit failed\n");
        exit(EXIT_FAILURE);
    }

    /*
     * get rid of the unused parameter warnings
     */
    if (argc < 1 || !argv) {
        fprintf(stderr, "unexpected argc\n");
        exit(EXIT_FAILURE);
    }

    /*
     * initialize the database library for the current thread
     */
    db_thread_init();

    /*
     * open the database connections
     */
    if (db_open(DB_HOST, DB_USERNAME, DB_PASSWORD, DB_DATABASE, DB_PORT,
            DB_UNIX_SOCKET, DB_CLIENT_FLAG, 1) != 0) {
        fprintf(stderr, "db: %s\n", db_error());
        exit(EXIT_FAILURE);
    }

    /*
     * just a simple query to show an example usage
     */
    if (simple_query_example() != 0) {
        return EXIT_FAILURE;
    }

    /*
     * do some useful things here
     */

    return EXIT_SUCCESS;
}

static void clean_up(void)
{
    if (db_is_closed() == 0) {
        db_close();
    }
}

static int simple_query_example(void)
{
    int q_result;
    MYSQL *mysql_conn;

    /* get a connection from the pool */
    mysql_conn = db_get_conn();
    if (!mysql_conn) {
        fprintf(stderr, "db: %s\n", db_error());
        return -1;
    }

    /* execute a query */
    q_result = q(mysql_conn,
            "INSERT INTO `example` (name) VALUES ('example')");

    /* return the connection */
    db_post_conn(mysql_conn);

    return q_result;
}

static int q(MYSQL *mysql_conn, const char *q)
{
    int result;

    result = mysql_query(mysql_conn, q);

    if (result != 0) {
        unsigned int result_errno;

        result_errno = mysql_errno(mysql_conn);

        /*
         * an example how to deal with different database errors
         *
         * in this case we treat specially only ER_LOCK_DEADLOCK
         */
        switch (result_errno) {
            /*
             * deadlock found when trying to get lock
             */
            case ER_LOCK_DEADLOCK:
            {
                struct timeval stv;

                /*
                 * wait for a small random amount of time (up to 1s)
                 * and retry
                 */
                stv.tv_sec = 0;
                stv.tv_usec = rand() % 1000000;
                select(0, NULL, NULL, NULL, &stv);

                result = mysql_query(mysql_conn, q);
                if (result != 0)
                {
                    /* tje retry was unsuccessful too */
                    result_errno = mysql_errno(mysql_conn);
                    fprintf(stderr, "db query: %s; errno: %u\n",
                            q, result_errno);
                }

                break;
            }

            /*
             * all other errors
             */
            default:
            {
                fprintf(stderr, "db query: %s; errno: %u\n",
                        q, result_errno);

                /* ping the database */
                if (db_ping(mysql_conn) != 0)
                {
                    fprintf(stderr, "db: %s\n", db_error());
                }
            }
        }
    }

    return result;
}
