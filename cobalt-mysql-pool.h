/*
 * cobalt-daemon
 *
 * This is a little C language module to include into your Linux (or
 * other POSIX compatible) applications, which will allow to easily
 * create and operate on a pool of MySQL (or compatible) database
 * connections.
 *
 * See the provided example program for a quick start.
 *
 * https://github.com/0xebef/cobalt-mysql-pool
 *
 * License: LGPLv3 or later
 *
 * Copyright (c) 2018, 0xebef
 */

#ifndef COBALT_MYSQL_POOL_H_INCLUDED
#define COBALT_MYSQL_POOL_H_INCLUDED

#if defined(__cplusplus)
extern "C" {
#endif

#include <mysql.h>

/* the number of connections in the database pool */
#define DB_POOL_CONN_COUNT        (8U)

/* how long (in seconds) should we wait for a mutex before a timeout */
#define DEFAULT_MUTEX_TIMEOUT_SEC (30)

/*
 * all threads must call this function before calling any other
 * functions
 */
void db_thread_init(void);

/*
 * all threads must call this function when the database is no longer
 * needed
 */
void db_thread_end(void);

/*
 * if any of the functions which return a negative value on error return
 * a negative value then this function will return the error message
 */
const char *db_error(void);

/*
 * open database connections and fill the pool with them
 *
 * please see the mysql_real_connect documentation for the parameters
 *
 * the value of `autocommit_mode` will be passed to the mysql_autocommit
 * function after the connection is established, it should be 0 or 1
 *
 * returns zero on success or a negative value on error
 */
int db_open(const char *host,
            const char *user,
            const char *passwd,
            const char *db,
            unsigned int port,
            const char *unix_socket,
            unsigned long client_flag,
            my_bool autocommit_mode);

/*
 * close all the database connections in the pool
 *
 * returns zero on success or a negative value on error
 */
int db_close(void);

/*
 * check if the pool connections are open
 *
 * returns 0 or 1 like a boolean
 */
int db_is_open(void);

/*
 * check if the pool connections are explicitly closed,
 * i.e using `db_close`
 *
 * returns 0 or 1 like a boolean
 */
int db_is_closed(void);

/*
 * get a `MYSQL` connection from the pool
 */
MYSQL *db_get_conn(void);

/*
 * return a `MYSQL` connection to the pool
 *
 * returns zero on success or a negative value on error
 */
int db_post_conn(MYSQL *mysql_conn);

/*
 * ping `MYSQL` connection, it can help to reconnect a lost connection
 *
 * returns zero on success or a negative value on error
 */
int db_ping(MYSQL *mysql_conn);

#if defined(__cplusplus)
}
#endif

#endif /* COBALT_MYSQL_POOL_H_INCLUDED */
