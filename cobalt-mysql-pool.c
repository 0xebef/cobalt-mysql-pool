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

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <endian.h>
#include <mysql.h>
#include "cobalt-mysql-pool.h"

static char *err_not_inited = "database library can not be initialized";
static char *err_not_thread_safe = "database library is not thread-safe";
static char *err_not_open = "database connection is closed";
static char *err_input = "invalid input parameters";
static char *err_unknown = "unknown error or no error";
static char *err_init_lib = "failed to initialize the database library";
static char *err_init_mutex = "failed to initialize the mutex";
static char *err_init_rwlock = "failed to initialize the rw-lock";
static char *err_init_semaphore = "failed to initialize the semaphore";
static char *err_connect = "can not connect to the database";
static char *err_reconnect = "can not reconnect to the database";
static char *err_init = "the database is not initialized";
static char *err_ping = "database ping was not successful";
static char *err_mutex = "can not acquire the database mutex";
static char *err_rwlock = "can not acquire the database rw-lock";
static char *err_semaphore_wait = "semaphore wait error";
static char *err_semaphore_post = "semaphore post error";
static char *err_clock_gettime = "clock_gettime failed";
static char *err_no_free_slot_bug = "no free slot found, this is a bug";
static char *err_no_busy_slot_bug = "no busy slot found, this is a bug";
static char *err_last = NULL;
static pthread_mutex_t db_mutex;
static pthread_rwlock_t db_rw_lock;
static sem_t db_sem;

/*
 * access to these variables must be protected by the `db_mutex` mutex
 */
static MYSQL *volatile mysql_conns[DB_POOL_CONN_COUNT] = {NULL};
static volatile int mysql_conns_busy[DB_POOL_CONN_COUNT] = {0};

/*
 * `is_thread_safe` = 0
 * changes to 1 after the first successful `db_connect` call and
 * remains 1 throughout the life of the process
 *
 * `is_inited` = 0
 * changes to 1 after the first successful `db_connect` call and
 * remains 1 throughout the life of the process
 */
static volatile int is_thread_safe = 0;
static volatile int is_inited = 0;

/*
 * access to these variables must be protected by the `db_rwlock`
 * read-write lock
 *
 * `is_open` = 0, `is_closed` = 1
 * initial mode or the mode after successful call to `db_close`
 *
 * `is_open` = 1, `is_closed` = 0
 * normal operation mode
 *
 * `is_open` = 0, `is_closed` = 0
 * not explicitly closed, but the connection was lost during the execution
 *
 * `is_open` = 1, `is_closed` = 1
 * should be impossible
 */
static volatile int is_open = 0;
static volatile int is_closed = 1;

/*
 * please check the functions comments in the header file
 */

void db_thread_init(void)
{
    mysql_thread_init();
}

void db_thread_end(void)
{
    mysql_thread_end();
}

const char *db_error(void)
{
    if (err_last) {
        return err_last;
    }

    if (!is_thread_safe) {
        return err_not_thread_safe;
    }

    if (!is_inited) {
        return  err_not_inited;
    }

    if (pthread_rwlock_rdlock(&db_rw_lock) != 0) {
        return err_rwlock;
    }

    if (!is_open) {
        pthread_rwlock_unlock(&db_rw_lock);
        return err_not_open;
    }

    pthread_rwlock_unlock(&db_rw_lock);

    return err_unknown;
}

int db_open(const char *host,
            const char *user,
            const char *passwd,
            const char *db,
            unsigned int port,
            const char *unix_socket,
            unsigned long client_flag,
            my_bool autocommit_mode)
{
    size_t i;

    if (!is_inited) {
        if (!mysql_thread_safe()) {
            is_thread_safe = 0;
            return -1;
        }

        is_thread_safe = 1;

        if (mysql_library_init(0, NULL, NULL) != 0) {
            err_last = err_init_lib;
            return -1;
        }

        if (pthread_mutex_init(&db_mutex, NULL) != 0) {
            err_last = err_init_mutex;
            return -1;
        }

        if (pthread_rwlock_init(&db_rw_lock, NULL) != 0) {
            pthread_mutex_destroy(&db_mutex);
            err_last = err_init_rwlock;
            return -1;
        }

        if (sem_init(&db_sem, 0, DB_POOL_CONN_COUNT) != 0) {
            pthread_rwlock_destroy(&db_rw_lock);
            pthread_mutex_destroy(&db_mutex);
            err_last = err_init_semaphore;
            return -1;
        }

        is_inited = 1;
    }

    if (pthread_mutex_lock(&db_mutex) != 0) {
        err_last = err_mutex;
        return -1;
    }

    for (i = 0U; i < DB_POOL_CONN_COUNT; i++) {
        if (!mysql_conns[i]) {
            const my_bool reconnect = 1; /* autoreconnect on ping */

            mysql_conns[i] = mysql_init(NULL);
            if (!mysql_conns[i] ||
                    mysql_options(mysql_conns[i], MYSQL_OPT_RECONNECT,
                        &reconnect) != 0 ||
                    mysql_real_connect(mysql_conns[i], host,
                        user, passwd, db, port, unix_socket,
                        client_flag) == NULL ||
                    mysql_autocommit(mysql_conns[i],
                        autocommit_mode) != 0) {
                /*
                 * close all the connections
                 */
                for (;;) {
                    if (mysql_conns[i]) {
                        mysql_close(mysql_conns[i]);
                        mysql_conns[i] = NULL;
                    }

                    if (i == 0U) {
                        break;
                    } else {
                        i--;
                    }
                }
                pthread_mutex_unlock(&db_mutex);

                err_last = err_connect;
                return -1;
            }
        } else {
            /*
             * reuse a previously created connection
             *
             * we are setting the MYSQL_OPT_RECONNECT option when
             * creating a connection so a simple ping should do a
             * reconnect (given that the server is responding) if the
             * connection was lost for some reason (i.e. timeout)
             */
            if (mysql_ping(mysql_conns[i]) != 0) {
                pthread_mutex_unlock(&db_mutex);

                err_last = err_reconnect;
                return -1;
            }
        }
    }

    if (pthread_rwlock_wrlock(&db_rw_lock) != 0) {
        pthread_mutex_unlock(&db_mutex);

        err_last = err_rwlock;
        return -1;
    }

    is_open = 1;
    is_closed = 0;

    pthread_rwlock_unlock(&db_rw_lock);

    pthread_mutex_unlock(&db_mutex);

    return 0;
}

int db_close(void)
{
    size_t i;
    int ready;

    if (!is_inited) {
        err_last = err_init;
        return -1;
    }

    if (pthread_rwlock_wrlock(&db_rw_lock) != 0) {
        err_last = err_rwlock;
        return -1;
    }

    if (is_closed) {
        pthread_rwlock_unlock(&db_rw_lock);
        return 0;
    }

    /*
     * after this the `db_get_conn` function won't return any more
     * connections from the pool until `is_open` becomes 1
     *
     * by setting the `is_closed` to 1 we can inform the application
     * the connections was closed intentionally and there is no need
     * to reconnect
     */
    is_open = 0;
    is_closed = 1;

    pthread_rwlock_unlock(&db_rw_lock);

    /*
     * try to wait for all the connections from the pool that are being
     * used and close them
     *
     * no more connections will be returned from the pool by the
     * `db_get_conn` funtion until `db_connect` is called again
     */
    ready = 0;
    while (!ready) {
        for (i = 0U; i < DB_POOL_CONN_COUNT; i++) {
            ready = 1;

            if (pthread_mutex_lock(&db_mutex) != 0) {
                err_last = err_mutex;
                return -1;
            }
            if (mysql_conns_busy[i] == 1) {
                pthread_mutex_unlock(&db_mutex);

                if (sem_wait(&db_sem) != 0) {
                    err_last = err_semaphore_wait;
                    return -1;
                }
                ready = 0;
            } else {
                pthread_mutex_unlock(&db_mutex);
            }

            if (!ready) {
                break;
            }
        }
    }

    return 0;
}

int db_is_open(void) {
    int result;

    if (!is_inited) {
        return 0;
    }

    if (pthread_rwlock_rdlock(&db_rw_lock) != 0) {
        err_last = err_rwlock;
        return -1;
    }

    result = is_open;

    pthread_rwlock_unlock(&db_rw_lock);

    return result;
}

int db_is_closed(void)
{
    int result;

    if (!is_inited) {
        return 1;
    }

    if (pthread_rwlock_rdlock(&db_rw_lock) != 0) {
        err_last = err_rwlock;
        return -1;
    }

    result = is_closed;

    pthread_rwlock_unlock(&db_rw_lock);

    return result;
}

MYSQL *db_get_conn(void)
{
    size_t i;
    struct timespec tmo_timespec;
    MYSQL *mysql_conn = NULL;

    if (!is_inited) {
        err_last = err_init;
        return NULL;
    }

    if (pthread_rwlock_rdlock(&db_rw_lock) != 0) {
        err_last = err_rwlock;
        return NULL;
    }

    if (!is_open) {
        pthread_rwlock_unlock(&db_rw_lock);

        err_last = err_not_open;
        return NULL;
    }

    pthread_rwlock_unlock(&db_rw_lock);

    if (sem_wait(&db_sem) != 0) {
        err_last = err_semaphore_wait;
        return NULL;
    }

    if (clock_gettime(CLOCK_REALTIME, &tmo_timespec) != 0) {
        err_last = err_clock_gettime;
        if (sem_post(&db_sem) != 0) {
            /* ignore the error, we are already in erroneous state */
        }
        return NULL;
    }

    tmo_timespec.tv_sec += DEFAULT_MUTEX_TIMEOUT_SEC;
    if (pthread_mutex_timedlock(&db_mutex, &tmo_timespec) != 0) {
        err_last = err_mutex;
        if (sem_post(&db_sem) != 0) {
            /* ignore the error, we are already in erroneous state */
        }
        return NULL;
    }

    if (pthread_rwlock_rdlock(&db_rw_lock) != 0) {
        err_last = err_rwlock;
        return NULL;
    }

    /*
     * checking another time because it's possible that db_close was
     * working while we were waiting on the mutex or a connection was
     * closed for another reason
     */
    if (!is_open) {
        pthread_rwlock_unlock(&db_rw_lock);
        pthread_mutex_unlock(&db_mutex);

        err_last = err_not_open;
        if (sem_post(&db_sem) != 0) {
            /* ignore the error, we are already in erroneous state */
        }
        return NULL;
    }

    pthread_rwlock_unlock(&db_rw_lock);

    for (i = 0U; i < DB_POOL_CONN_COUNT; i++) {
        if (!mysql_conns_busy[i]) {
            mysql_conns_busy[i] = 1;
            mysql_conn = mysql_conns[i];
            break;
        }
    }

    if (!mysql_conn) {
        pthread_mutex_unlock(&db_mutex);
        err_last = err_no_free_slot_bug;
        if (sem_post(&db_sem) != 0) {
            /* ignore the error, we are already in erroneous state */
        }
        return NULL;
    }

    pthread_mutex_unlock(&db_mutex);


    return mysql_conn;
}

int db_post_conn(MYSQL *mysql_conn)
{
    size_t i;
    int found;

    if (!is_inited) {
        err_last = err_init;
        return -1;
    }

    if (!mysql_conn) {
        err_last = err_input;
        return -1;
    }

    if (pthread_mutex_lock(&db_mutex) != 0) {
        err_last = err_mutex;
        return -1;
    }

    for (i = 0U, found = 0; i < DB_POOL_CONN_COUNT; i++) {
        if (mysql_conns_busy[i]) {
            mysql_conns[i] = mysql_conn;
            mysql_conns_busy[i] = 0;
            found = 1;
            break;
        }
    }

    if (!found) {
        pthread_mutex_unlock(&db_mutex);

        err_last = err_no_busy_slot_bug;
        return -1;
    }

    pthread_mutex_unlock(&db_mutex);

    if (sem_post(&db_sem) != 0) {
        err_last = err_semaphore_post;
        return -1;
    }

    return 0;
}

int db_ping(MYSQL *mysql_conn)
{
    if (!is_inited) {
        err_last = err_init;
        return -1;
    }

    if (!mysql_conn) {
        err_last = err_input;
        return -1;
    }

    if (mysql_ping(mysql_conn) != 0) {
        err_last = err_ping;
        return -1;
    }

    return 0;
}
