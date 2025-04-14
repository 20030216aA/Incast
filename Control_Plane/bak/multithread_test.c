#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include <bf_rt/bf_rt.h>

#include <bfsys/bf_sal/bf_sys_intf.h>

#include "common.h"

/******************************************************************************
 * This sample c application code is based on the P4 program
 * multithread_test.p4
 * Please refer to the P4 program and the generated bf-rt.json for information
 * on the tables contained in the P4 program, and the associated key and data
 * fields.
 *****************************************************************************/

// Structure definition to represent the key of the ipRoute table
typedef struct ipRoute_key_s {
  uint32_t ip_dst_addr;
  uint16_t vrf;
} ipRoute_key_t;

// Structure definition to represent the data of the ipRoute table for action
// "route"
typedef struct ipRoute_route_data_s {
  uint64_t src_mac;
  uint64_t dst_mac;
  uint16_t dst_port;
} ipRoute_route_data_t;

// Structure definition to represent the data of the ipRoute table for action
// "nat"
typedef struct ipRoute_nat_data_s {
  uint32_t src_addr;
  uint32_t dst_addr;
  uint16_t dst_port;
} ipRoute_nat_data_t;

// Structure definition tp represent the data of the ipRoute table
typedef struct ipRoute_data_s {
  // Based on the action_id, contents of the enum are interpreted
  bf_rt_id_t action_id;
  union {
    ipRoute_route_data_t route_data;
    ipRoute_nat_data_t nat_data;
  } data;
} ipRoute_data_t;

// Structure definition to represent the key of the forward table
typedef struct forward_key_s { uint64_t dst_mac; } forward_key_t;

typedef union table_key_e {
  ipRoute_key_t ipRoute_key;
  forward_key_t forward_key;
} table_key_t;

// Structure definition to represent the data of the forward table for action
// "hit"
typedef struct forward_hit_data_s { uint16_t port; } forward_hit_data_t;

// Structure definition to represent the data of the forward table for action
// "miss"
typedef struct forward_miss_data_s { uint8_t drop; } forward_miss_data_t;

// Structure definition tp represent the data of the forward table
typedef struct forward_data_s {
  // Based on the action_id, contents of the enum are interpreted
  bf_rt_id_t action_id;
  union {
    forward_hit_data_t hit_data;
    forward_miss_data_t miss_data;
  } data;
} forward_data_t;

// Key field ids, table data field ids, action ids, Table hdl required for
// interacting with the table
const bf_rt_info_hdl *bfrt_info = NULL;
const bf_rt_table_hdl *ipRoute_table = NULL;
bf_rt_table_attributes_hdl *attr = NULL;
const bf_rt_table_hdl *forward_table = NULL;
bool is_test_finished = false;

// Key field ids
bf_rt_id_t ipRoute_ip_dst_field_id = 0;
bf_rt_id_t ipRoute_vrf_field_id = 0;

bf_rt_id_t forward_eth_dst_field_id = 0;

// Action Ids
bf_rt_id_t ipRoute_route_action_id = 0;
bf_rt_id_t ipRoute_nat_action_id = 0;

bf_rt_id_t forward_hit_action_id = 0;
bf_rt_id_t forward_miss_action_id = 0;

// Data field Ids for route action
bf_rt_id_t ipRoute_route_action_src_mac_field_id = 0;
bf_rt_id_t ipRoute_route_action_dst_mac_field_id = 0;
bf_rt_id_t ipRoute_route_action_port_field_id = 0;

// Data field ids for nat action
bf_rt_id_t ipRoute_nat_action_ip_src_field_id = 0;
bf_rt_id_t ipRoute_nat_action_ip_dst_field_id = 0;
bf_rt_id_t ipRoute_nat_action_port_field_id = 0;

// Data field id for hit action
bf_rt_id_t forward_hit_action_port_field_id = 0;

// Data field id for miss action
bf_rt_id_t forward_miss_action_drop_field_id = 0;

#define ALL_PIPES 0xffff
bf_rt_target_t dev_tgt;

bool interactive = true;

#define NO_OF_ENTRIES 125000

// Structure to contain all single thread related data.
typedef struct thread_key_data_s {
  bf_rt_table_key_hdl *bfrt_table_key;
  bf_rt_table_data_hdl *bfrt_table_data;
  bf_rt_session_hdl *session;
} thread_key_data_t;

typedef enum thread_function_e {
  FUNC_ADD,
  FUNC_DELETE,
  FUNC_MODIFY,
  FUNC_GET,
} thread_function_t;

typedef enum thread_table_type_e {
  TABLE_IPROUTE,
  TABLE_FORWARD,
} thread_table_type_t;

typedef struct thread_args_s {
  union {
    ipRoute_key_t ipRoute_key;
    forward_key_t forward_key;
  } data;
  thread_table_type_t table_type;
  thread_key_data_t key_data;
  thread_function_t func;
} thread_args_t;

// This function does the initial set_up of getting bfrt_info object associated
// with the P4 program from which all other required objects are obtained
void set_up(void) {
  dev_tgt.dev_id = 0;
  dev_tgt.pipe_id = ALL_PIPES;

  // Get bfrt_info object from dev_id and p4 program name
  bf_status_t bf_status =
      bf_rt_info_get(dev_tgt.dev_id, "multithread_test", &bfrt_info);
  // Check for status
  bf_sys_assert(bf_status == BF_SUCCESS);
}

/**********************************************************************
 * CALLBACK funciton that gets invoked upon a entry aging event. One per entry
 *  1. target : Device target from which the entry is aging out
 *  2. key : Pointer to the key object representing the entry which has aged out
 *  3. cookie : Pointer to the cookie which was given at the time of the
 *callback registration
 *
 *********************************************************************/
bf_status_t idletime_callback(bf_rt_target_t *target,
                              bf_rt_table_key_hdl *key,
                              void *cookie) {
  bf_rt_session_hdl *session = (bf_rt_session_hdl *)cookie;
#ifdef BFRT_GENERIC_FLAGS
  bf_status_t status =
      bf_rt_table_entry_del(ipRoute_table, session, target, 0, key);
#else
  bf_status_t status =
      bf_rt_table_entry_del(ipRoute_table, session, target, key);
#endif
  bf_sys_assert(status == BF_SUCCESS);
  bf_rt_session_complete_operations(session);

  status = bf_rt_table_key_deallocate(key);
  bf_sys_assert(status == BF_SUCCESS);

  return BF_SUCCESS;
}

// This function does the initial set up of getting key field-ids, action-ids
// and data field ids associated with the ipRoute table. This is done once
// during init time.
void ipRoute_table_set_up(bf_rt_session_hdl *session) {
  // Get table object from name
  bf_status_t bf_status = bf_rt_table_from_name_get(
      bfrt_info, "SwitchIngress.ipRoute", &ipRoute_table);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get action Ids for route and nat actions
  bf_status = bf_rt_action_name_to_id(
      ipRoute_table, "SwitchIngress.route", &ipRoute_route_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_action_name_to_id(
      ipRoute_table, "SwitchIngress.nat", &ipRoute_nat_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get field-ids for key field and data fields
  bf_status = bf_rt_key_field_id_get(
      ipRoute_table, "hdr.ipv4.dst_addr", &ipRoute_ip_dst_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      bf_rt_key_field_id_get(ipRoute_table, "vrf", &ipRoute_vrf_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * DATA FIELD ID GET FOR "route" ACTION
   **********************************************************************/
  bf_status = bf_rt_data_field_id_with_action_get(
      ipRoute_table,
      "srcMac",
      ipRoute_route_action_id,
      &ipRoute_route_action_src_mac_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_data_field_id_with_action_get(
      ipRoute_table,
      "dstMac",
      ipRoute_route_action_id,
      &ipRoute_route_action_dst_mac_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      bf_rt_data_field_id_with_action_get(ipRoute_table,
                                          "dst_port",
                                          ipRoute_route_action_id,
                                          &ipRoute_route_action_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * DATA FIELD ID GET FOR "nat" ACTION
   **********************************************************************/
  bf_status =
      bf_rt_data_field_id_with_action_get(ipRoute_table,
                                          "srcAddr",
                                          ipRoute_nat_action_id,
                                          &ipRoute_nat_action_ip_src_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      bf_rt_data_field_id_with_action_get(ipRoute_table,
                                          "dstAddr",
                                          ipRoute_nat_action_id,
                                          &ipRoute_nat_action_ip_dst_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      bf_rt_data_field_id_with_action_get(ipRoute_table,
                                          "dst_port",
                                          ipRoute_nat_action_id,
                                          &ipRoute_nat_action_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * ALLOCATE TABLE ATTRIBUTE FOR ENABLING IDLE TIMEOUT AND REGISTER A CALLBACK
   **********************************************************************/
  bf_status = bf_rt_table_idle_table_attributes_allocate(
      ipRoute_table, BFRT_NOTIFY_MODE, &attr);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Set min_ttl to 50 ms, max_ttl to 5000 ms and ttl_query intervale to 50 ms
  uint32_t min_ttl = 50;
  uint32_t max_ttl = 5000;
  uint32_t ttl_query_interval = 50;
  void *cookie = (void *)session;

  bf_status = bf_rt_attributes_idle_table_notify_mode_set(attr,
                                                          true,
                                                          idletime_callback,
                                                          ttl_query_interval,
                                                          max_ttl,
                                                          min_ttl,
                                                          cookie);
  bf_sys_assert(bf_status == BF_SUCCESS);
#ifdef BFRT_GENERIC_FLAGS
  bf_status =
      bf_rt_table_attributes_set(ipRoute_table, session, &dev_tgt, 0, attr);
#else
  bf_status =
      bf_rt_table_attributes_set(ipRoute_table, session, &dev_tgt, attr);
#endif
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

void forward_table_set_up(void) {
  // Get table object from name
  bf_status_t bf_status = bf_rt_table_from_name_get(
      bfrt_info, "SwitchIngress.forward", &forward_table);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get action Ids for hit and miss actions
  bf_status = bf_rt_action_name_to_id(
      forward_table, "SwitchIngress.hit", &forward_hit_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_action_name_to_id(
      forward_table, "SwitchIngress.miss", &forward_miss_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get field-ids for key field and data fields
  bf_status = bf_rt_key_field_id_get(
      forward_table, "hdr.ethernet.dst_addr", &forward_eth_dst_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * DATA FIELD ID GET FOR "hit" ACTION
   **********************************************************************/
  bf_status =
      bf_rt_data_field_id_with_action_get(forward_table,
                                          "port",
                                          forward_hit_action_id,
                                          &forward_hit_action_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * DATA FIELD ID GET FOR "miss" ACTION
   **********************************************************************/
  bf_status =
      bf_rt_data_field_id_with_action_get(forward_table,
                                          "drop",
                                          forward_miss_action_id,
                                          &forward_miss_action_drop_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

void table_set_up(bf_rt_session_hdl *session) {
  ipRoute_table_set_up(session);
  forward_table_set_up();
}

// This function allocates the objects which need to be separate across threads
void thread_table_set_up(thread_table_type_t which_table,
                         thread_key_data_t *t) {
  const bf_rt_table_hdl *table = ipRoute_table;

  if (which_table == TABLE_FORWARD) table = forward_table;

  // Allocate key and data once, and use reset across different uses
  bf_status_t bf_status = bf_rt_table_key_allocate(table, &t->bfrt_table_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_data_allocate(table, &t->bfrt_table_data);
  bf_sys_assert(bf_status == BF_SUCCESS);
}

// This function clears up any allocated memory during table_set_up()
void thread_table_tear_down(thread_key_data_t *t) {
  bf_status_t bf_status;
  // Deallocate key and data
  bf_status = bf_rt_table_key_deallocate(t->bfrt_table_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_data_deallocate(t->bfrt_table_data);
  bf_sys_assert(bf_status == BF_SUCCESS);
}

/*******************************************************************************
 * Utility functions associated with "ipRoute" table in the P4 program.
 ******************************************************************************/

// This function sets the passed in ip_dst and vrf value into the key object
// passed using the setValue methods on the key object
void ipRoute_key_setup(const ipRoute_key_t *ipRoute_key,
                       bf_rt_table_key_hdl *table_key) {
  // Set value into the key object. Key type is "EXACT"
  bf_status_t bf_status = bf_rt_key_field_set_value(
      table_key, ipRoute_ip_dst_field_id, ipRoute_key->ip_dst_addr);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_key_field_set_value(
      table_key, ipRoute_vrf_field_id, ipRoute_key->vrf);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This function sets the passed in eth dst value into the key object
// passed using the setValue methods on the key object
void forward_key_setup(const forward_key_t *forward_key,
                       bf_rt_table_key_hdl *table_key) {
  // Set value into the key object.
  bf_status_t bf_status = bf_rt_key_field_set_value(
      table_key, forward_eth_dst_field_id, forward_key->dst_mac);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This function sets the passed in "hit" action data into the
// data object associated with the forward table
void forward_data_setup_for_hit(const forward_hit_data_t *forward_data,
                                bf_rt_table_data_hdl *table_data) {
  // Set value into the data object
  bf_status_t bf_status = bf_rt_data_field_set_value(
      table_data, forward_hit_action_port_field_id, forward_data->port);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This function sets the passed in "miss" action data into the
// data object associated with the forward table
void forward_data_setup_for_miss(const forward_miss_data_t *forward_data,
                                 bf_rt_table_data_hdl *table_data) {
  // Set value into the data object
  bf_status_t bf_status = bf_rt_data_field_set_value(
      table_data, forward_miss_action_drop_field_id, forward_data->drop);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This function sets the passed in "route" action data into the
// data object associated with the ipRoute table
void ipRoute_data_setup_for_route(const ipRoute_route_data_t *ipRoute_data,
                                  bf_rt_table_data_hdl *table_data) {
  // Set value into the data object
  bf_status_t bf_status = bf_rt_data_field_set_value(
      table_data, ipRoute_route_action_src_mac_field_id, ipRoute_data->src_mac);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_data_field_set_value(
      table_data, ipRoute_route_action_dst_mac_field_id, ipRoute_data->dst_mac);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_data_field_set_value(
      table_data, ipRoute_route_action_port_field_id, ipRoute_data->dst_port);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This functiona sets the passed in "nat" acton data into the
// data object associated with the ipRoute table and "nat" action within the
// ipRoute table
void ipRoute_data_setup_for_nat(const ipRoute_nat_data_t *ipRoute_data,
                                bf_rt_table_data_hdl *table_data) {
  // Set value into the data object
  bf_status_t bf_status = bf_rt_data_field_set_value(
      table_data, ipRoute_nat_action_ip_src_field_id, ipRoute_data->src_addr);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_data_field_set_value(
      table_data, ipRoute_nat_action_ip_dst_field_id, ipRoute_data->dst_addr);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_data_field_set_value(
      table_data, ipRoute_nat_action_port_field_id, ipRoute_data->dst_port);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This function adds or modifies an entry in the ipRoute table with "route"
// action. The workflow is similar for either table entry add or modify
static bf_status_t ipRoute_entry_add_modify_with_route(
    const ipRoute_key_t *ipRoute_key,
    const ipRoute_route_data_t *ipRoute_data,
    thread_key_data_t *t,
    const bool add) {
  // Adding a match entry with below mac Addr to be forwarded to the below port
  // Reset key and data before use
  bf_rt_table_key_reset(ipRoute_table, &t->bfrt_table_key);
  bf_rt_table_action_data_reset(
      ipRoute_table, ipRoute_route_action_id, &t->bfrt_table_data);

  // Fill in the Key and Data object
  ipRoute_key_setup(ipRoute_key, t->bfrt_table_key);
  ipRoute_data_setup_for_route(ipRoute_data, t->bfrt_table_data);

  // Call table entry add API, if the request is for an add, else call modify
  bf_status_t status = BF_SUCCESS;
  if (add) {
    status = bf_rt_table_entry_add(ipRoute_table,
                                   t->session,
                                   &dev_tgt,
#ifdef BFRT_GENERIC_FLAGS
                                   0,
#endif
                                   t->bfrt_table_key,
                                   t->bfrt_table_data);
  } else {
    status = bf_rt_table_entry_mod(ipRoute_table,
                                   t->session,
                                   &dev_tgt,
#ifdef BFRT_GENERIC_FLAGS
                                   0,
#endif
                                   t->bfrt_table_key,
                                   t->bfrt_table_data);
  }
  if (status == BF_SUCCESS) bf_rt_session_complete_operations(t->session);

  return status;
}

// This function adds or modifies an entry in the forward table with "hit"
// action. The workflow is similar for either table entry add or modify
static bf_status_t forward_entry_add_modify_with_hit(
    const forward_key_t *forward_key,
    const forward_hit_data_t *forward_data,
    thread_key_data_t *t,
    const bool add) {
  // Adding a match entry with below mac Addr to be forwarded to the below port
  // Reset key and data before use
  bf_rt_table_key_reset(forward_table, &t->bfrt_table_key);
  bf_rt_table_action_data_reset(
      forward_table, forward_hit_action_id, &t->bfrt_table_data);

  // Fill in the Key and Data object
  forward_key_setup(forward_key, t->bfrt_table_key);
  forward_data_setup_for_hit(forward_data, t->bfrt_table_data);

  // Call table entry add API, if the request is for an add, else call modify
  bf_status_t status = BF_SUCCESS;
  if (add) {
    status = bf_rt_table_entry_add(forward_table,
                                   t->session,
                                   &dev_tgt,
#ifdef BFRT_GENERIC_FLAGS
                                   0,
#endif
                                   t->bfrt_table_key,
                                   t->bfrt_table_data);
  } else {
    status = bf_rt_table_entry_mod(forward_table,
                                   t->session,
                                   &dev_tgt,
#ifdef BFRT_GENERIC_FLAGS
                                   0,
#endif
                                   t->bfrt_table_key,
                                   t->bfrt_table_data);
  }
  if (status == BF_SUCCESS) bf_rt_session_complete_operations(t->session);

  return status;
}

// This function adds or modifies an entry in the forward table with "miss"
// action. The workflow is similar for either table entry add or modify
static bf_status_t forward_entry_add_modify_with_miss(
    const forward_key_t *forward_key,
    const forward_miss_data_t *forward_data,
    thread_key_data_t *t,
    const bool add) {
  // Adding a match entry with below mac Addr to be forwarded to the below port
  // Reset key and data before use
  bf_rt_table_key_reset(forward_table, &t->bfrt_table_key);
  bf_rt_table_action_data_reset(
      forward_table, forward_miss_action_id, &t->bfrt_table_data);

  // Fill in the Key and Data object
  forward_key_setup(forward_key, t->bfrt_table_key);
  forward_data_setup_for_miss(forward_data, t->bfrt_table_data);

  // Call table entry add API, if the request is for an add, else call modify
  bf_status_t status = BF_SUCCESS;
  if (add) {
    status = bf_rt_table_entry_add(forward_table,
                                   t->session,
                                   &dev_tgt,
#ifdef BFRT_GENERIC_FLAGS
                                   0,
#endif
                                   t->bfrt_table_key,
                                   t->bfrt_table_data);
  } else {
    status = bf_rt_table_entry_mod(forward_table,
                                   t->session,
                                   &dev_tgt,
#ifdef BFRT_GENERIC_FLAGS
                                   0,
#endif
                                   t->bfrt_table_key,
                                   t->bfrt_table_data);
  }
  if (status == BF_SUCCESS) bf_rt_session_complete_operations(t->session);

  return status;
}

// This function adds or modifies an entry in the ipRoute table with "nat"
// action. The workflow is similar for either table entry add or modify
static bf_status_t ipRoute_entry_add_modify_with_nat(
    const ipRoute_key_t *ipRoute_key,
    const ipRoute_nat_data_t *ipRoute_data,
    thread_key_data_t *t,
    const bool add) {
  // Reset key and data before use
  bf_rt_table_key_reset(ipRoute_table, &t->bfrt_table_key);
  bf_rt_table_action_data_reset(
      ipRoute_table, ipRoute_nat_action_id, &t->bfrt_table_data);

  ipRoute_key_setup(ipRoute_key, t->bfrt_table_key);
  ipRoute_data_setup_for_nat(ipRoute_data, t->bfrt_table_data);

  // Call table entry add API, if the request is for an add, else call modify
  bf_status_t status = BF_SUCCESS;
  if (add) {
    status = bf_rt_table_entry_add(ipRoute_table,
                                   t->session,
                                   &dev_tgt,
#ifdef BFRT_GENERIC_FLAGS
                                   0,
#endif
                                   t->bfrt_table_key,
                                   t->bfrt_table_data);
  } else {
    status = bf_rt_table_entry_mod(ipRoute_table,
                                   t->session,
                                   &dev_tgt,
#ifdef BFRT_GENERIC_FLAGS
                                   0,
#endif
                                   t->bfrt_table_key,
                                   t->bfrt_table_data);
  }
  if (status == BF_SUCCESS) bf_rt_session_complete_operations(t->session);

  return status;
}

// This function process the entry obtained by a get call for a "route" action
// and populates the ipRoute_route_data_t structure
void ipRoute_process_route_entry_get(const bf_rt_table_data_hdl *data,
                                     ipRoute_route_data_t *route_data) {
  bf_status_t status = BF_SUCCESS;

  status = bf_rt_data_field_get_value(
      data, ipRoute_route_action_src_mac_field_id, &route_data->src_mac);
  bf_sys_assert(status == BF_SUCCESS);

  status = bf_rt_data_field_get_value(
      data, ipRoute_route_action_dst_mac_field_id, &route_data->dst_mac);
  bf_sys_assert(status == BF_SUCCESS);

  uint64_t port;
  status = bf_rt_data_field_get_value(
      data, ipRoute_route_action_port_field_id, &port);
  route_data->dst_port = (uint16_t)port;
  bf_sys_assert(status == BF_SUCCESS);

  return;
}

// This function process the entry obtained by a get call for a "nat" action
// and populates the ipRoute_nat_data_t structure
void ipRoute_process_nat_entry_get(const bf_rt_table_data_hdl *data,
                                   ipRoute_nat_data_t *nat_data) {
  bf_status_t status = BF_SUCCESS;

  uint64_t src_addr;
  status = bf_rt_data_field_get_value(
      data, ipRoute_nat_action_ip_src_field_id, &src_addr);
  bf_sys_assert(status == BF_SUCCESS);
  nat_data->src_addr = (uint32_t)src_addr;

  uint64_t dst_addr;
  status = bf_rt_data_field_get_value(
      data, ipRoute_nat_action_ip_dst_field_id, &dst_addr);
  bf_sys_assert(status == BF_SUCCESS);
  nat_data->dst_addr = (uint32_t)dst_addr;

  uint64_t dst_port;
  status = bf_rt_data_field_get_value(
      data, ipRoute_nat_action_port_field_id, &dst_port);
  bf_sys_assert(status == BF_SUCCESS);
  nat_data->dst_port = (uint16_t)dst_port;

  return;
}

// This function processes the entry obtained by a get call. Based on the action
// id the data object is intepreted.
void ipRoute_process_entry_get(const bf_rt_table_data_hdl *data,
                               ipRoute_data_t *ipRoute_data) {
  // First get actionId, then based on that, fill in appropriate fields
  bf_status_t bf_status;
  bf_rt_id_t action_id;

  bf_status = bf_rt_data_action_id_get(data, &action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  if (action_id == ipRoute_route_action_id) {
    ipRoute_process_route_entry_get(data, &ipRoute_data->data.route_data);
  } else if (action_id == ipRoute_nat_action_id) {
    ipRoute_process_nat_entry_get(data, &ipRoute_data->data.nat_data);
  }
  return;
}

// This function reads an entry specified by the ipRoute_key, and fills in the
// passedin IpRoute object
void ipRoute_entry_get(const ipRoute_key_t *ipRoute_key,
                       thread_key_data_t *t,
                       ipRoute_data_t *data) {
  // Reset key and data before use
  bf_rt_table_key_reset(ipRoute_table, &t->bfrt_table_key);
  // Data reset is done without action-id, since the action-id is filled in by
  // the get function
  bf_rt_table_data_reset(ipRoute_table, &t->bfrt_table_data);

  ipRoute_key_setup(ipRoute_key, t->bfrt_table_key);

  bf_status_t status = BF_SUCCESS;

#ifdef BFRT_GENERIC_FLAGS
  uint64_t flags = 0;
  BF_RT_FLAG_CLEAR(flags, BF_RT_FROM_HW);
  status = bf_rt_table_entry_get(ipRoute_table,
                                 t->session,
                                 &dev_tgt,
                                 flags,
                                 t->bfrt_table_key,
                                 t->bfrt_table_data);
#else
  bf_rt_entry_read_flag_e flag = ENTRY_READ_FROM_SW;
  status = bf_rt_table_entry_get(ipRoute_table,
                                 t->session,
                                 &dev_tgt,
                                 t->bfrt_table_key,
                                 t->bfrt_table_data,
                                 flag);
#endif
  bf_sys_assert(status == BF_SUCCESS);

  ipRoute_process_entry_get(t->bfrt_table_data, data);

  return;
}

// This function deletes an entry specified by the ipRoute_key
static bf_status_t ipRoute_entry_delete(const ipRoute_key_t *ipRoute_key,
                                        thread_key_data_t *t) {
  // Reset key before use
  bf_rt_table_key_reset(ipRoute_table, &t->bfrt_table_key);

  ipRoute_key_setup(ipRoute_key, t->bfrt_table_key);

#ifdef BFRT_GENERIC_FLAGS
  bf_status_t status = bf_rt_table_entry_del(
      ipRoute_table, t->session, &dev_tgt, 0, t->bfrt_table_key);
#else
  bf_status_t status = bf_rt_table_entry_del(
      ipRoute_table, t->session, &dev_tgt, t->bfrt_table_key);
#endif
  if (status == BF_SUCCESS) bf_rt_session_complete_operations(t->session);

  return status;
}

// This function deletes an entry specified by the forward_key
static bf_status_t forward_entry_delete(const forward_key_t *forward_key,
                                        thread_key_data_t *t) {
  // Reset key before use
  bf_rt_table_key_reset(forward_table, &t->bfrt_table_key);

  forward_key_setup(forward_key, t->bfrt_table_key);
#ifdef BFRT_GENERIC_FLAGS
  bf_status_t status = bf_rt_table_entry_del(
      forward_table, t->session, &dev_tgt, 0, t->bfrt_table_key);
#else
  bf_status_t status = bf_rt_table_entry_del(
      forward_table, t->session, &dev_tgt, t->bfrt_table_key);
#endif
  if (status == BF_SUCCESS) bf_rt_session_complete_operations(t->session);

  return status;
}

static void ipRoute_table_sync(bf_rt_session_hdl *session);

void ipRoute_table_sync_sync_cb(bf_rt_target_t *tgt, void *arg) {
  tgt = tgt;

  if (is_test_finished == false) ipRoute_table_sync((bf_rt_session_hdl *)arg);
}

// sync counter table
static void ipRoute_table_sync(bf_rt_session_hdl *session) {
  bf_status_t bf_status = BF_SUCCESS;
  bf_rt_table_operations_hdl *operation_hdl;
  bf_status = bf_rt_table_operations_allocate(
      ipRoute_table, BFRT_COUNTER_SYNC, &operation_hdl);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_operations_counter_sync_set(operation_hdl,
                                                session,
                                                &dev_tgt,
                                                ipRoute_table_sync_sync_cb,
                                                (void *)session);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_operations_execute(ipRoute_table, operation_hdl);
  bf_sys_assert(bf_status == BF_SUCCESS);
}

// Function to iterate over all the entries in the table
static void table_iterate(thread_key_data_t *t) {
  // Table iteration involves the following
  //    1. Use the getFirst API to get the first entry
  //    2. Use the tableUsageGet API to get the number of entries currently in
  //    the table.
  //    3. Use the number of entries returned in step 2 and pass it as a
  //    parameter to getNext_n (as n) to get all the remaining entries
  bf_rt_table_key_hdl *first_key;
  bf_rt_table_data_hdl *first_data;

  bf_status_t bf_status = bf_rt_table_key_allocate(ipRoute_table, &first_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_data_allocate(ipRoute_table, &first_data);
  bf_sys_assert(bf_status == BF_SUCCESS);

#ifdef BFRT_GENERIC_FLAGS
  uint64_t flags = 0;
  BF_RT_FLAG_CLEAR(flags, BF_RT_FROM_HW);
  bf_status = bf_rt_table_entry_get_first(
      ipRoute_table, t->session, &dev_tgt, flags, first_key, first_data);
#else
  bf_rt_entry_read_flag_e flag = ENTRY_READ_FROM_SW;
  bf_status = bf_rt_table_entry_get_first(
      ipRoute_table, t->session, &dev_tgt, first_key, first_data, flag);
#endif
  if (bf_status == BF_OBJECT_NOT_FOUND) goto clean_first;
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Process the first entry
  ipRoute_data_t route_data;
  ipRoute_process_entry_get(first_data, &route_data);

  // Get the usage of table
  uint32_t entry_count = 0;
#ifdef BFRT_GENERIC_FLAGS
  bf_status = bf_rt_table_usage_get(
      ipRoute_table, t->session, &dev_tgt, flags, &entry_count);
#else
  bf_status = bf_rt_table_usage_get(
      ipRoute_table, t->session, &dev_tgt, &entry_count, flag);
#endif
  bf_sys_assert(bf_status == BF_SUCCESS);

  if (entry_count == 1) {
    goto clean_first;
  }

  // allocate 2 pointer arrays for bf_rt_table_key_hdl* and
  // bf_rt_table_data_hdl*
  bf_rt_table_key_hdl **keys =
      malloc(sizeof(bf_rt_table_key_hdl *) * (entry_count - 1));
  bf_rt_table_data_hdl **datas =
      malloc(sizeof(bf_rt_table_data_hdl *) * (entry_count - 1));

  bf_sys_assert(keys != NULL);
  bf_sys_assert(datas != NULL);

  for (unsigned i = 0; i < entry_count - 1; ++i) {
    bf_status = bf_rt_table_key_allocate(ipRoute_table, &keys[i]);
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = bf_rt_table_data_allocate(ipRoute_table, &datas[i]);
    bf_sys_assert(bf_status == BF_SUCCESS);
  }

  // Get next N
  uint32_t num_returned = 0;
#ifdef BFRT_GENERIC_FLAGS
  bf_status = bf_rt_table_entry_get_next_n(ipRoute_table,
                                           t->session,
                                           &dev_tgt,
                                           flags,
                                           first_key,
                                           keys,
                                           datas,
                                           entry_count - 1,
                                           &num_returned);
#else
  bf_status = bf_rt_table_entry_get_next_n(ipRoute_table,
                                           t->session,
                                           &dev_tgt,
                                           first_key,
                                           keys,
                                           datas,
                                           entry_count - 1,
                                           &num_returned,
                                           flag);

#endif
  if (num_returned != entry_count - 1)
    printf("Entry count does not match, might happen during delete\n");

  // Process the rest of the entries
  for (unsigned i = 0; i < entry_count - 1; ++i) {
    ipRoute_process_entry_get(datas[i], &route_data);
    // Do any required processing with the obtained data and key
  }

  // Deallocate the key and data objects
  for (unsigned i = 0; i < entry_count - 1; ++i) {
    bf_status = bf_rt_table_key_deallocate(keys[i]);
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = bf_rt_table_data_deallocate(datas[i]);
    bf_sys_assert(bf_status == BF_SUCCESS);
  }
  // Deallocate the pointer arrays
  free(keys);
  free(datas);
clean_first:
  // Deallocate the key, data used for entry_first_get
  bf_status = bf_rt_table_key_deallocate(first_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_data_deallocate(first_data);
  bf_sys_assert(bf_status == BF_SUCCESS);
  return;
}

static void delete_n_next_entries(thread_table_type_t table_type,
                                  table_key_t *table_key1,
                                  thread_key_data_t *t,
                                  int n) {
  pthread_t tid = pthread_self();
  bf_status_t status = BF_SUCCESS;
  int i = 0;

  if (table_type == TABLE_IPROUTE) {
    ipRoute_key_t *ipRoute_key1 = (ipRoute_key_t *)table_key1;
    ipRoute_key_t *ipRoute_key = malloc(sizeof(ipRoute_key_t) * n);
    bf_sys_assert(ipRoute_key != NULL);

    for (i = 0; i < n; i++) {
      printf("\r %d", i);

      ipRoute_key[i].ip_dst_addr = ipRoute_key1->ip_dst_addr + i;
      ipRoute_key[i].vrf = ipRoute_key1->vrf + i;

      status = ipRoute_entry_delete(&ipRoute_key[i], t);
      if (status != BF_SUCCESS) break;
    }

    free(ipRoute_key);
  } else if (table_type == TABLE_FORWARD) {
    forward_key_t *forward_key1 = (forward_key_t *)table_key1;
    forward_key_t *forward_key = malloc(sizeof(forward_key_t) * n);
    bf_sys_assert(forward_key != NULL);

    for (i = 0; i < n; i++) {
      printf("\r %d", i);

      forward_key[i].dst_mac = forward_key1->dst_mac + i;

      status = forward_entry_delete(&forward_key[i], t);
      if (status != BF_SUCCESS) break;
    }

    free(forward_key);
  }
  printf("\nTID [%lu]:---Deleted %d entries session ptr [%p]\n",
         tid,
         i,
         (void *)t->session);
  bf_sys_assert(status == BF_SUCCESS);
}

static void add_n_next_entries(thread_table_type_t table_type,
                               table_key_t *table_key1,
                               thread_key_data_t *t,
                               int n) {
  pthread_t tid = pthread_self();
  bf_status_t status = BF_SUCCESS;
  int i = 0;

  if (table_type == TABLE_IPROUTE) {
    ipRoute_key_t *ipRoute_key1 = (ipRoute_key_t *)table_key1;
    ipRoute_key_t *ipRoute_key = malloc(sizeof(ipRoute_key_t) * n);
    bf_sys_assert(ipRoute_key != NULL);

    ipRoute_route_data_t ipRoute_data = {0xaabbccddeeff, 0xffeeddccbbaa, 2};

    for (i = 0; i < n; i++) {
      printf("\r %d", i);

      ipRoute_key[i].ip_dst_addr = ipRoute_key1->ip_dst_addr + i;
      ipRoute_key[i].vrf = ipRoute_key1->vrf + i;

      status = ipRoute_entry_add_modify_with_route(
          &ipRoute_key[i], &ipRoute_data, t, true);
      if (status != BF_SUCCESS) break;
    }

    free(ipRoute_key);
  } else if (table_type == TABLE_FORWARD) {
    forward_key_t *forward_key1 = (forward_key_t *)table_key1;
    forward_key_t *forward_key = malloc(sizeof(forward_key_t) * n);
    bf_sys_assert(forward_key != NULL);

    forward_hit_data_t forward_data = {0};

    for (i = 0; i < n; i++) {
      printf("\r %d", i);

      forward_key[i].dst_mac = forward_key1->dst_mac + i;

      status = forward_entry_add_modify_with_hit(
          &forward_key[i], &forward_data, t, true);
      if (status != BF_SUCCESS) break;
    }

    free(forward_key);
  }
  printf("\nTID [%lu]:---Added %d entries session ptr [%p]\n",
         tid,
         i,
         (void *)t->session);
  bf_sys_assert(status == BF_SUCCESS);
}

static void modify_n_next_entries(thread_table_type_t table_type,
                                  table_key_t *table_key1,
                                  thread_key_data_t *t,
                                  int n) {
  pthread_t tid = pthread_self();
  bf_status_t status = BF_SUCCESS;
  int i = 0;

  if (table_type == TABLE_IPROUTE) {
    ipRoute_key_t *ipRoute_key1 = (ipRoute_key_t *)table_key1;
    ipRoute_key_t *ipRoute_key = malloc(sizeof(ipRoute_key_t) * n);
    bf_sys_assert(ipRoute_key != NULL);

    ipRoute_nat_data_t ipNat_data = {0x264DCC42, 0xC0A80102, 7};

    for (i = 0; i < n; i++) {
      printf("\r %d", i);
      ipRoute_key[i].ip_dst_addr = ipRoute_key1->ip_dst_addr + i;
      ipRoute_key[i].vrf = ipRoute_key1->vrf + i;

      status = ipRoute_entry_add_modify_with_nat(
          &ipRoute_key[i], &ipNat_data, t, false);
    }

    free(ipRoute_key);
  } else if (table_type == TABLE_FORWARD) {
    forward_key_t *forward_key1 = (forward_key_t *)table_key1;
    forward_key_t *forward_key = malloc(sizeof(forward_key_t) * n);
    bf_sys_assert(forward_key != NULL);

    forward_miss_data_t forward_data = {0};

    for (i = 0; i < n; i++) {
      printf("\r %d", i);

      forward_key[i].dst_mac = forward_key1->dst_mac + i;

      status = forward_entry_add_modify_with_miss(
          &forward_key[i], &forward_data, t, true);
    }

    free(forward_key);
  }

  printf("\nTID [%lu]:---Modified %d entries session ptr [%p]\n",
         tid,
         i,
         (void *)t->session);
  bf_sys_assert(status == BF_SUCCESS);
}

static void *thread_func1(void *arg) {
  thread_args_t *args = arg;
  table_key_t *table_key = (table_key_t *)&args->data;
  bool session_created = false;

  // Handle session creation if the none was passed
  if (args->key_data.session == NULL) {
    bf_status_t bf_status = bf_rt_session_create(&args->key_data.session);
    bf_sys_assert(bf_status == BF_SUCCESS);
    session_created = true;
  }

  thread_table_set_up(args->table_type, &args->key_data);

  switch (args->func) {
    case FUNC_MODIFY:
      modify_n_next_entries(
          args->table_type, table_key, &args->key_data, NO_OF_ENTRIES);
      break;
    case FUNC_ADD:
      add_n_next_entries(
          args->table_type, table_key, &args->key_data, NO_OF_ENTRIES);
      break;
    case FUNC_DELETE:
      delete_n_next_entries(
          args->table_type, table_key, &args->key_data, NO_OF_ENTRIES);
      break;
    case FUNC_GET:
      table_iterate(&args->key_data);
      break;
    default:
      break;
  }

  thread_table_tear_down(&args->key_data);

  if (session_created) {
    // Tear Down
    bf_status_t bf_status = bf_rt_session_destroy((args->key_data.session));
    bf_sys_assert(bf_status == BF_SUCCESS);
  }

  return NULL;
}

int perform_driver_func_threads(void) {
  forward_key_t forward_key1 = {0x000102030405};
  ipRoute_key_t ipRoute_key1 = {0x0A0B0C01, 9};
  bf_rt_session_hdl *session;
  pthread_t *thread_w;
  thread_args_t *args;
  int status = 0;

  thread_w = malloc(sizeof(*thread_w) * 2);
  if (!thread_w) return -ENOMEM;
  args = malloc(sizeof(*args) * 2);
  if (!args) {
    free(thread_w);
    return -ENOMEM;
  }

  printf("Tables are set up.\n");

  // Create a session object
  bf_status_t bf_status = bf_rt_session_create(&session);
  // Check for status
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Do initial set ups
  set_up();
  // Do table level set up
  table_set_up(session);

  ipRoute_table_sync(session);

  printf("6.1?Single client with two threads\n");
  printf("Test a) One thread adds, another gets entries\n");
  args[0].func = FUNC_ADD;
  args[0].table_type = TABLE_IPROUTE;
  args[0].data.ipRoute_key.ip_dst_addr = ipRoute_key1.ip_dst_addr;
  args[0].data.ipRoute_key.vrf = ipRoute_key1.vrf;
  args[0].key_data.session = session;
  status = pthread_create(&thread_w[0], NULL, thread_func1, &args[0]);
  if (status) goto error;

  args[1].func = FUNC_GET;
  args[1].table_type = TABLE_IPROUTE;
  args[1].key_data.session = session;
  status = pthread_create(&thread_w[1], NULL, thread_func1, &args[1]);
  if (status) goto error;

  for (int i = 0; i < 2; i++) {
    status = pthread_join(thread_w[i], NULL);
    if (status) goto error;
  }

  printf("Test b) One thread modifies, another gets entries\n");
  args[0].func = FUNC_MODIFY;
  args[0].data.ipRoute_key.ip_dst_addr = ipRoute_key1.ip_dst_addr;
  args[0].data.ipRoute_key.vrf = ipRoute_key1.vrf;
  args[0].key_data.session = session;
  status = pthread_create(&thread_w[0], NULL, thread_func1, &args[0]);
  if (status) goto error;

  args[1].func = FUNC_GET;
  args[1].table_type = TABLE_IPROUTE;
  args[1].key_data.session = session;
  status = pthread_create(&thread_w[1], NULL, thread_func1, &args[1]);
  if (status) goto error;

  for (int i = 0; i < 2; i++) {
    status = pthread_join(thread_w[i], NULL);
    if (status) goto error;
  }

#if 0
  printf("Test c): One thread deletes, another gets entries\n");
  args[0].func = FUNC_DELETE;
  args[0].data.ipRoute_key.ip_dst_addr = ipRoute_key1.ip_dst_addr;
  args[0].data.ipRoute_key.vrf = ipRoute_key1.vrf;
  args[0].key_data.session = session;
  status = pthread_create(&thread_w[0], NULL, thread_func1, &args[0]);
  if (status) goto error;

  args[1].func = FUNC_GET;
  args[1].table_type = TABLE_IPROUTE;
  args[1].key_data.session = session;
  status = pthread_create(&thread_w[1], NULL, thread_func1, &args[1]);
  if (status) goto error;

  for (int i = 0; i < 2; i++) {
    status = pthread_join(thread_w[i], NULL);
    if (status) goto error;
  }
#endif

  printf(
      "6.2 Multiple clients (each thread creates a session), each single "
      "thread\n");
  printf("Test a) One thread adds, another gets entries\n");
  args[0].func = FUNC_ADD;
  args[0].table_type = TABLE_IPROUTE;
  args[0].data.ipRoute_key.ip_dst_addr = 2 * ipRoute_key1.ip_dst_addr;
  args[0].data.ipRoute_key.vrf = ipRoute_key1.vrf;
  args[0].key_data.session = NULL;
  status = pthread_create(&thread_w[0], NULL, thread_func1, &args[0]);
  if (status) goto error;

  args[1].func = FUNC_GET;
  args[1].table_type = TABLE_IPROUTE;
  args[1].key_data.session = NULL;
  status = pthread_create(&thread_w[1], NULL, thread_func1, &args[1]);
  if (status) goto error;

  for (int i = 0; i < 2; i++) {
    status = pthread_join(thread_w[i], NULL);
    if (status) goto error;
  }

  printf("Test b) One thread modifies, another gets entries\n");
  args[0].func = FUNC_MODIFY;
  args[0].table_type = TABLE_IPROUTE;
  args[0].data.ipRoute_key.ip_dst_addr = 2 * ipRoute_key1.ip_dst_addr;
  args[0].data.ipRoute_key.vrf = ipRoute_key1.vrf;
  args[0].key_data.session = NULL;
  status = pthread_create(&thread_w[0], NULL, thread_func1, &args[0]);
  if (status) goto error;

  args[1].func = FUNC_GET;
  args[1].table_type = TABLE_IPROUTE;
  args[1].key_data.session = NULL;
  status = pthread_create(&thread_w[1], NULL, thread_func1, &args[1]);
  if (status) goto error;

  for (int i = 0; i < 2; i++) {
    status = pthread_join(thread_w[i], NULL);
    if (status) goto error;
  }

#if 0
  printf("Test c): One thread deletes, another gets entries\n");
  args[0].func = FUNC_DELETE;
  args[0].data.ipRoute_key.ip_dst_addr = ipRoute_key1.ip_dst_addr;
  args[0].data.ipRoute_key.vrf = ipRoute_key1.vrf;
  args[0].key_data.session = NULL;
  status = pthread_create(&thread_w[0], NULL, thread_func1, &args[0]);
  if (status) goto error;

  args[1].func = FUNC_GET;
  args[1].table_type = TABLE_IPROUTE;
  args[1].key_data.session = NULL;
  status = pthread_create(&thread_w[1], NULL, thread_func1, &args[1]);
  if (status) goto error;

  for (int i = 0; i < 2; i++) {
    status = pthread_join(thread_w[i], NULL);
    if (status) goto error;
  }
#endif

  printf("6.3?Single client with two threads and two tables\n");
  printf("Test a) Both thread add entries to different tables\n");
  args[0].func = FUNC_ADD;
  args[0].table_type = TABLE_IPROUTE;
  args[0].data.ipRoute_key.ip_dst_addr = 3 * ipRoute_key1.ip_dst_addr;
  args[0].data.ipRoute_key.vrf = ipRoute_key1.vrf;
  args[0].key_data.session = NULL;

  status = pthread_create(&thread_w[0], NULL, thread_func1, &args[0]);
  if (status) goto error;

  args[1].func = FUNC_ADD;
  args[1].table_type = TABLE_FORWARD;
  args[1].data.forward_key.dst_mac = forward_key1.dst_mac;
  args[1].key_data.session = NULL;

  status = pthread_create(&thread_w[1], NULL, thread_func1, &args[1]);
  if (status) goto error;

  for (int i = 0; i < 2; i++) {
    status = pthread_join(thread_w[i], NULL);
    if (status) goto error;
  }

  printf(
      "Test b) One thread modifies ipRoute table entries, another gets forward "
      "table entries\n");
  args[0].func = FUNC_MODIFY;
  args[0].table_type = TABLE_IPROUTE;
  args[0].data.ipRoute_key.ip_dst_addr = 3 * ipRoute_key1.ip_dst_addr;
  args[0].data.ipRoute_key.vrf = ipRoute_key1.vrf;
  args[0].key_data.session = NULL;

  status = pthread_create(&thread_w[0], NULL, thread_func1, &args[0]);
  if (status) goto error;

  args[1].func = FUNC_GET;
  args[1].table_type = TABLE_FORWARD;
  args[1].key_data.session = NULL;
  status = pthread_create(&thread_w[1], NULL, thread_func1, &args[1]);
  if (status) goto error;

  for (int i = 0; i < 2; i++) {
    status = pthread_join(thread_w[i], NULL);
    if (status) goto error;
  }

#if 0
  printf(
      "Test c): One thread deletes ipRoute table entries, another deletes "
      "forward "
      "table entries\n");
  args[0].func = FUNC_DELETE;
  args[0].table_type = TABLE_IPROUTE;
  args[0].data.ipRoute_key.ip_dst_addr = ipRoute_key1.ip_dst_addr;
  args[0].data.ipRoute_key.vrf = ipRoute_key1.vrf;
  args[0].key_data.session = NULL;

  status = pthread_create(&thread_w[0], NULL, thread_func1, &args[0]);
  if (status) goto error;

  args[1].func = FUNC_DELETE;
  args[1].table_type = TABLE_FORWARD;
  args[1].data.forward_key.dst_mac = forward_key1.dst_mac;
  args[1].key_data.session = NULL;

  status = pthread_create(&thread_w[1], NULL, thread_func1, &args[1]);
  if (status) goto error;

  for (int i = 0; i < 2; i++) {
    status = pthread_join(thread_w[i], NULL);
    if (status) goto error;
  }
#endif

  printf("Done! Test completed.\n");

error:
  is_test_finished = true;
  // Tear Down
  bf_status = bf_rt_session_destroy(session);
  // Check for status
  bf_sys_assert(bf_status == BF_SUCCESS);

  free(args);
  free(thread_w);

  return status;
}

int main(int argc, char **argv) {
  parse_opts_and_switchd_init(argc, argv);

  perform_driver_func_threads();

  run_cli_or_cleanup();
  return 0;
}
