#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#include <bf_rt/bf_rt.h>

#include <bfsys/bf_sal/bf_sys_intf.h>

#include "common.h"

/******************************************************************************
 * This sample c application is based on the P4 program large_counter_meter.p4
 * It demonstrates how the BFRT table sync operations (REGISTER_SYNC,
 * COUNTER_SYNC) might be executed by multiple threads.
 *
 * The main process starts several threads to request BFRT table operations
 * in different ways: asynchronously with callback, or synchronously;
 * on the same table, or on different tables; from different sessions.
 * The callback cookie object tracks how many times each task (a thread
 * with cookie) is executed. If a table is currently busy with the same
 * operation, then the task should wait and repeat if it has enough attempts
 * left. Please read the source code and comments below for more details.
 *
 * Please refer to the P4 program and the generated bf-rt.json for information
 * on the tables contained in the P4 program, and the associated key and data
 * fields.
 *****************************************************************************/

#define LOG(...)                                                \
  {                                                             \
    char time_buf_[64] = {0};                                   \
    struct tm curr_tm_ = {0};                                   \
    struct timespec c_t_ = {0};                                 \
    clock_gettime(CLOCK_REALTIME, &c_t_);                       \
    gmtime_r(&c_t_.tv_sec, &curr_tm_);                          \
    strftime(time_buf_, sizeof(time_buf_), "%F %T", &curr_tm_); \
    printf("\n%s.%06lu ", time_buf_, c_t_.tv_nsec / 1000);      \
    printf(__VA_ARGS__);                                        \
  }

#define LOG_COOKIE(cookie_, ...)                                \
  {                                                             \
    char time_buf_[64] = {0};                                   \
    struct tm curr_tm_ = {0};                                   \
    struct timespec c_t_ = {0};                                 \
    clock_gettime(CLOCK_REALTIME, &c_t_);                       \
    gmtime_r(&c_t_.tv_sec, &curr_tm_);                          \
    strftime(time_buf_, sizeof(time_buf_), "%F %T", &curr_tm_); \
    printf("\n%s.%06lu ", time_buf_, c_t_.tv_nsec / 1000);      \
    printf(" %scookie %d (%d:%d/%d/%d) op:%d on %s :: ",        \
           ((cookie_)->is_active) ? "active " : "",             \
           (cookie_)->thread_nr,                                \
           (cookie_)->credit,                                   \
           (cookie_)->cnt_req,                                  \
           (cookie_)->cnt_cbk,                                  \
           (cookie_)->cnt_try,                                  \
           (cookie_)->op_mode,                                  \
           (cookie_)->table_name);                              \
    printf(__VA_ARGS__);                                        \
  }

#define ALL_PIPES 0xffff
#define COOKIE_SIGN 0xC0CE

typedef struct cb_cookie {
  int signature;   // magic value to check is a valid cookie pointer passed to
                   // callback
  bool is_active;  // the task is either waiting for callback, or sleeping
  pthread_mutex_t lock;            // atomic cookie read/write
  bf_rt_counter_sync_cb callback;  // callback function to execute
  int thread_nr;                   // requested thread (task#)
  unsigned int credit;  // how many times to execute (0 - until fail on retries)
  unsigned int exec_delay_sec;  // make pause before executing
  unsigned int
      retry_delay_sec;  // delay if the table is busy with the same operation
  unsigned int
      retry_attempts;  // attempts to repeat on the busy table if credit allows

  // BFRT settings
  bf_rt_table_operations_mode_t op_mode;
  bf_rt_session_hdl *session;
  const bf_rt_table_hdl *table_hdl;
  const char *table_name;

  // The task statistics
  unsigned int cnt_req;  // how many times this cookie was requested
  unsigned int cnt_try;  // how many times this cookie was retried
  unsigned int cnt_cbk;  // how many times this cookie was called back
} cb_cookie_t;

const int num_threads = 7;
int run_threads = 0;

pthread_t *threads = NULL;
cb_cookie_t *cookies = NULL;  // each thread has its own cookie

bf_rt_session_hdl *session_1 = NULL;
bf_rt_session_hdl *session_2 = NULL;
const bf_rt_info_hdl *bfrt_info = NULL;

bf_rt_target_t dev_tgt = {0, ALL_PIPES};

static const char p4_program_name[] = "large_counter_meter";

const char *counter_table_name = "SwitchIngress.counter";
const bf_rt_table_hdl *counter_table = NULL;

const char *register_table_name = "SwitchIngress.register_table";
const bf_rt_table_hdl *register_table = NULL;

// Forward declarations
static bf_status_t req_table_operation(cb_cookie_t *cookie);

//---
void connect_bfrt_and_tables(void) {
  bf_status_t bf_status = BF_UNEXPECTED;

  LOG("--- Create BFRT session #1");
  bf_status = bf_rt_session_create(&session_1);
  bf_sys_assert(bf_status == BF_SUCCESS);

  LOG("--- Create BFRT session #2");
  bf_status = bf_rt_session_create(&session_2);
  bf_sys_assert(bf_status == BF_SUCCESS);

  LOG("--- Get BFRT info for %s", p4_program_name);
  bf_status = bf_rt_info_get(dev_tgt.dev_id, p4_program_name, &bfrt_info);
  bf_sys_assert(bf_status == BF_SUCCESS);

  LOG("--- Connect table %s", counter_table_name);
  bf_status =
      bf_rt_table_from_name_get(bfrt_info, counter_table_name, &counter_table);
  bf_sys_assert(bf_status == BF_SUCCESS);

  LOG("--- Connect table %s", register_table_name);
  bf_status = bf_rt_table_from_name_get(
      bfrt_info, register_table_name, &register_table);
  bf_sys_assert(bf_status == BF_SUCCESS);
}

//---
void tear_down_bfrt_and_tables() {
  bf_status_t bf_status = BF_UNEXPECTED;
  if (session_1) {
    LOG("--- Tear down BFRT session #1");
    bf_status = bf_rt_session_destroy(session_1);
    if (bf_status != BF_SUCCESS) {
      LOG("=!= ERROR on BFRT session #1 destroy rc=%d", bf_status);
    }
  }
  if (session_2) {
    LOG("--- Tear down BFRT session #2");
    bf_status = bf_rt_session_destroy(session_2);
    if (bf_status != BF_SUCCESS) {
      LOG("=!= ERROR on BFRT session #2 destroy rc=%d", bf_status);
    }
  }
}

//---
void fn_table_operation(cb_cookie_t *cookie, bool from_callback) {
  if (cookie == NULL || cookie->signature != COOKIE_SIGN) {
    LOG("---!!!--- Invalid cookie, ignore.");
  }

  int status = pthread_mutex_lock(&(cookie->lock));
  if (status != 0) {
    LOG_COOKIE(cookie, "-!- ERROR locking, rc=%d", status);
    return;
  }

  if (from_callback) {
    LOG_COOKIE(cookie, "->- Got");
    cookie->cnt_cbk++;
  } else {
    LOG_COOKIE(cookie, "vvv Begin");
    cookie->is_active = true;
  }

  if (cookie->credit && cookie->exec_delay_sec) {
    // Note: Better to pass the cookie to some dedicated scheduling thread.
    // It is not a good idea to suspend the callback thread with waiting.

    // Remember the cookie counters
    cb_cookie_t saved;
    saved = *cookie;

    status = pthread_mutex_unlock(&(cookie->lock));
    if (status != 0) {
      LOG_COOKIE(cookie, "-!- ERROR unlocking for delay, rc=%d", status);
      return;
    }
    LOG_COOKIE(
        cookie, "... Delay execution for %d sec.", cookie->exec_delay_sec);
    sleep(cookie->exec_delay_sec);

    status = pthread_mutex_lock(&(cookie->lock));
    if (status != 0) {
      LOG_COOKIE(cookie, "-!- ERROR locking after delay, rc=%d", status);
      return;
    }

    // After delay the cookie credit might already gone.
    if (cookie->cnt_req != saved.cnt_req || cookie->cnt_try != saved.cnt_try ||
        cookie->cnt_cbk != saved.cnt_cbk) {
      LOG_COOKIE(cookie, "-!- Is changed after delay.");
    }
  }

  bool is_retry = false;
  do {
    if (cookie->credit) {
      cookie->credit--;
      LOG_COOKIE(cookie,
                 "--- %s %s table operation",
                 (is_retry) ? "Retry" : "Call",
                 (from_callback) ? "next" : "first");
      bf_status_t bf_status = req_table_operation(cookie);
      if (bf_status == BF_SUCCESS) {
        cookie->cnt_req++;
        break;
      } else if (bf_status != BF_EAGAIN) {
        LOG_COOKIE(
            cookie, "-!- ERROR BFRT table operation request, rc=%d", bf_status);
        break;
      } else if (cookie->retry_attempts) {
        cookie->retry_attempts--;
        if (cookie->retry_delay_sec) {
          // TODO: pass the cookie to some dedicated scheduling thread.
          // It is not a good idea to suspend the callback thread with waiting.

          // Remember the cookie counters
          cb_cookie_t saved;
          saved = *cookie;

          status = pthread_mutex_unlock(&(cookie->lock));
          if (status != 0) {
            LOG_COOKIE(cookie, "-!- ERROR unlocking for retry, rc=%d", status);
            return;
          }
          LOG_COOKIE(
              cookie, "... Delay retry for %d sec.", cookie->retry_delay_sec);
          sleep(cookie->retry_delay_sec);
          status = pthread_mutex_lock(&(cookie->lock));
          if (status != 0) {
            LOG_COOKIE(cookie, "-!- ERROR locking for retry, rc=%d", status);
            return;
          }
          // After delay the cookie credit might already gone.
          if (cookie->cnt_req != saved.cnt_req ||
              cookie->cnt_try != saved.cnt_try ||
              cookie->cnt_cbk != saved.cnt_cbk) {
            LOG_COOKIE(cookie, "-!- Is changed before retry.");
          }
        }
        cookie->cnt_try++;
        LOG_COOKIE(cookie, "--- Retry #%d", cookie->cnt_try);
        is_retry = true;
      } else {
        LOG_COOKIE(cookie, "-!- Done, no more retry attempts.");
        cookie->is_active = false;
        break;
      }
    } else {
      LOG_COOKIE(cookie, "---- Done, no more credit to call.");
      cookie->is_active = false;
      break;
    }
  } while (is_retry);

  if (cookie->callback == NULL) {
    // synchronous task is done
    cookie->is_active = false;
  }

  status = pthread_mutex_unlock(&(cookie->lock));
  if (status != 0) {
    LOG_COOKIE(cookie, "-!- ERROR unlocking, rc=%d", status);
    return;
  }

  return;
}

// Callback function from BFRT on table operation execute.
/*
  The application should validate the cookie object pointer received.

  BFRT keeps cookie pointer and callback function pointer received
  in its internal state proxy object while it waits for low-level
  driver function asynchronous call.
  The internal state is kept on per-table and per-operation basis.
  While a table is busy with an operation, the same requests
  to that table are rejected (BF_EAGAIN) to make sure the callback
  will be sent to correct requester, but if the callback invocation
  was delayed from HW for longer than BFRT grace time (3 min.)
  due to some unusual HW or a driver issue, then the internal state
  might be considered as outdated, and it becomes replaced by another
  application cookie reference from the next same request.
  The outdated and replaced callback will not be called.
*/
void cb_bfrt_table_sync(bf_rt_target_t *tgt, void *arg) {
  (void)tgt;
  cb_cookie_t *cookie = (cb_cookie_t *)arg;

  LOG("vvv---vvv Got callback");
  fn_table_operation(cookie, true);
  LOG("^^^---^^^ End callback");
}

//---
static bf_status_t req_table_operation(cb_cookie_t *cookie) {
  if (cookie == NULL || cookie->signature != COOKIE_SIGN) {
    return BF_INVALID_ARG;
  }
  if (cookie->session == NULL || cookie->table_hdl == NULL) {
    return BF_INVALID_ARG;
  }

  LOG_COOKIE(cookie, "--- Call operation");

  bf_status_t bf_status = BF_UNEXPECTED;
  bf_status_t bf_status_2 = BF_UNEXPECTED;

  bf_rt_table_operations_hdl *operation_hdl;
  bf_status = bf_rt_table_operations_allocate(
      cookie->table_hdl, cookie->op_mode, &operation_hdl);
  if (bf_status != BF_SUCCESS) {
    LOG_COOKIE(cookie, "-!- ERROR table operation allocate, rc=%d", bf_status);
    return bf_status;
  }

  switch (cookie->op_mode) {
    case BFRT_COUNTER_SYNC:
      bf_status = bf_rt_operations_counter_sync_set(operation_hdl,
                                                    cookie->session,
                                                    &dev_tgt,
                                                    cookie->callback,
                                                    (void *)cookie);
      break;
    case BFRT_REGISTER_SYNC:
      bf_status = bf_rt_operations_register_sync_set(operation_hdl,
                                                     cookie->session,
                                                     &dev_tgt,
                                                     cookie->callback,
                                                     (void *)cookie);
      break;
    default:
      bf_status = BF_NOT_IMPLEMENTED;
      break;
  }
  if (bf_status != BF_SUCCESS) {
    LOG_COOKIE(cookie, "-!- ERROR table operation set, rc=%d", bf_status);
  } else {
    bf_status =
        bf_rt_table_operations_execute(cookie->table_hdl, operation_hdl);
    LOG_COOKIE(cookie,
               "%s Executed rc=%d",
               (bf_status) ? "-!- ERROR " : "---",
               bf_status);
  }

  // deallocate operation in any case
  bf_status_2 = bf_rt_table_operations_deallocate(operation_hdl);
  if (bf_status_2 != BF_SUCCESS) {
    LOG_COOKIE(cookie, "-!- ERROR table operation dealloc, rc=%d", bf_status_2);
  }

  return bf_status;
}

// Thread function to run a cookie.
//
static void *fn_table_sync_req(void *arg) {
  cb_cookie_t *cookie = arg;

  LOG("vvvv Start sync thread");
  fn_table_operation(cookie, false);
  LOG("^^^^ End sync thread");

  return NULL;
}

//---
int setup_cookies(cb_cookie_t *cookies_, int n_cookies) {
  int status = 0;
  bf_sys_assert(cookies_ != NULL);

  // Setup default cookie parameters
  int i = 0;
  for (i = 0; i < n_cookies; i++) {
    status = pthread_mutex_init(&(cookies_[i].lock), NULL);
    if (status) break;

    cookies_[i].is_active = false;

    cookies_[i].thread_nr = i;
    cookies_[i].callback = cb_bfrt_table_sync;
    cookies_[i].exec_delay_sec = 0;
    cookies_[i].credit = 0;
    cookies_[i].retry_delay_sec = 1;
    cookies_[i].retry_attempts = 3;
    cookies_[i].session = session_1;
    cookies_[i].table_hdl = NULL;
    cookies_[i].table_name = NULL;
    cookies_[i].op_mode = BFRT_COUNTER_SYNC;
    cookies_[i].cnt_req = 0;
    cookies_[i].cnt_cbk = 0;
    cookies_[i].cnt_try = 0;
    cookies_[i].signature = COOKIE_SIGN;
  }
  if (status) {
    for (int j = 0; j < i; j++) {
      pthread_mutex_destroy(&(cookies_[j].lock));
    }
    return status;
  }

  // Task-specific settings.
  //
  // The task schedule example is for the large_counter_meter.p4 tables
  // where each counter sync operation executes in approx. 3 seconds.
  //
  // Task current state:
  //   Exec, Sleep, Blocked
  // Task final state:
  //   (C:E/B/T) - initial Credit, Executions, callBacks, reTries.
  //
  // +----- session#
  // | +--- executes callback
  // | | +-- task# (a thread with cookie).
  // | | | +- seconds from the treads' init
  // | | | 0---------1---------2---------3---------4---------5---------6----
  // | | | |123456789|123456789|123456789|123456789|123456789|123456789|1234
  // V V v |
  // 1 c 0: EeeEeeEeeEee|                (4:4/4/0)
  // 1 c 1: sBb|                         (2:0/0/2)
  // 2 c 2: sBbB|                        (3:0/0/3)
  // 1 c 3: ssssssssssssBbEee|           (2:1/1/1)
  // 1 c 4: EeeEeeEeeEee|                (4:4/4/0)
  // 1   5: sBbbbbbbbbbbbbbbbbbbbbbbEee| (2:1/0/1)
  // 2   6: ssssssssssssssssssssEee|     (1:1/0/0)
  //
  if (n_cookies) {
    // #0 - the main thread should run almost always.
    cookies_[0].table_hdl = counter_table;
    cookies_[0].table_name = counter_table_name;
    cookies_[0].credit = 4;  // UINT_MAX;
    cookies_[0].exec_delay_sec = 0;
    cookies_[0].retry_delay_sec = 0;
    cookies_[0].retry_attempts = UINT_MAX;
  }
  if (n_cookies > 1) {
    // #1 - makes a jam at start, never done.
    cookies_[1].table_hdl = counter_table;
    cookies_[1].table_name = counter_table_name;
    cookies_[1].credit = 2;
    cookies_[1].exec_delay_sec = 1;
    cookies_[1].retry_delay_sec = 1;
    cookies_[1].retry_attempts = UINT_MAX;
  }
  if (n_cookies > 2) {
    // #2 - another session, makes longer jam at start, never done.
    cookies_[2].session = session_2;
    cookies_[2].table_hdl = counter_table;
    cookies_[2].table_name = counter_table_name;
    cookies_[2].credit = 3;
    cookies_[2].exec_delay_sec = 1;
    cookies_[2].retry_delay_sec = 1;
    cookies_[2].retry_attempts = UINT_MAX;
  }
  if (n_cookies > 3) {
    // #3 - waits the jam ends, retry once and run once.
    cookies_[3].table_hdl = counter_table;
    cookies_[3].table_name = counter_table_name;
    cookies_[3].credit = 2;
    cookies_[3].exec_delay_sec = 13;
    cookies_[3].retry_delay_sec = 3;
    cookies_[3].retry_attempts = UINT_MAX;
  }
  if (n_cookies > 4) {
    // #4 - runs smoothly on another table.
    cookies_[4].table_hdl = register_table;
    cookies_[4].table_name = register_table_name;
    cookies_[4].op_mode = BFRT_REGISTER_SYNC;
    cookies_[4].credit = 4;
    cookies_[4].exec_delay_sec = 0;
    cookies_[4].retry_delay_sec = 0;
    cookies_[4].retry_attempts = UINT_MAX;
  }
  if (n_cookies > 5) {
    // #5 - starts in jam, retries once, and runs once synchronously.
    cookies_[5].callback = NULL;
    cookies_[5].table_hdl = counter_table;
    cookies_[5].table_name = counter_table_name;
    cookies_[5].credit = 2;
    cookies_[5].exec_delay_sec = 1;
    cookies_[5].retry_delay_sec = 25;
    cookies_[5].retry_attempts = UINT_MAX;
  }
  if (n_cookies > 6) {
    // #6 - runs once synchronously after a pause.
    cookies_[6].session = session_2;
    cookies_[6].callback = NULL;
    cookies_[6].table_hdl = counter_table;
    cookies_[6].table_name = counter_table_name;
    cookies_[6].credit = 1;
    cookies_[6].exec_delay_sec = 20;
    cookies_[6].retry_delay_sec = 0;
    cookies_[6].retry_attempts = UINT_MAX;
  }

  //----- Summary
  for (i = 0; i < n_cookies; i++) {
    LOG_COOKIE(&cookies_[i], "<== initial state");
  }

  return status;
}

// Check all tasks completed
int active_cookies_count() {
  int curr_active = 0;

  if (!cookies) return 0;

  for (int i = 0; i < run_threads; i++) {
    if (cookies[i].signature == COOKIE_SIGN && cookies[i].is_active) {
      curr_active++;
    }
  }
  return curr_active;
}

//-----
int launch_threads(void) {
  int status = -1;

  if (run_threads) return 0;

  threads = malloc(sizeof(*threads) * num_threads);
  if (!threads) {
    return -ENOMEM;
  }
  cookies = malloc(sizeof(*cookies) * num_threads);
  if (!cookies) {
    free(threads);
    threads = NULL;
    return -ENOMEM;
  }

  status = setup_cookies(cookies, num_threads);
  if (status) {
    free(cookies);
    cookies = NULL;
    free(threads);
    threads = NULL;
    return status;
  }

  for (run_threads = 0; run_threads < num_threads; run_threads++) {
    LOG("->- Start thread %i", run_threads);
    status = pthread_create(
        &threads[run_threads], NULL, fn_table_sync_req, &cookies[run_threads]);
    if (status) {
      LOG("-!- ERROR thread %d creation, rc=%d", run_threads, status);
      return status;
    }
  }

  return status;
}

//---
void tear_down_threads(void) {
  int status = -1;

  if (!run_threads || !threads) return;

  LOG("==== Wait %d running threads", run_threads);

  for (int i = 0; i < run_threads; i++) {
    LOG("--- wait thread %d", i);
    status = pthread_join(threads[i], NULL);
    if (status) {
      LOG("-!- ERROR join thread %d, rc=%d", i, status);
      continue;
    }
    LOG("-- thread %d end", i);

    if (cookies) {
      pthread_mutex_destroy(&(cookies[i].lock));
      LOG_COOKIE((&cookies[i]), "<<< final state");
    }
  }

  free(threads);
  if (cookies) free(cookies);

  return;
}

//---
int main(int argc, char **argv) {
  parse_opts_and_switchd_init(argc, argv);
  connect_bfrt_and_tables();

  if (launch_threads() == 0) {
    run_cli_or_cleanup();
  }

  // For this simple app lets take that all the jobs have to be completed
  // max. in 3 minutes since the example schedule is initiated.
  int max_run_sec = 3 * 60;
  int is_alive_sec = 30;  // 'is alive' interval to check for completion.

  int a_cnt = 0;
  do {
    a_cnt = active_cookies_count();
    if (a_cnt) {
      LOG("............................. wait for %d active cookies", a_cnt);
      sleep(is_alive_sec);
      max_run_sec -= is_alive_sec;
    }
  } while (a_cnt && max_run_sec >= is_alive_sec);

  if (a_cnt) {
    LOG(".!!!!. Incomplete tasks: %d of %d", a_cnt, num_threads);
  }

  tear_down_threads();
  tear_down_bfrt_and_tables();

  return 0;
}
