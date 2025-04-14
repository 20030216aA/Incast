#include "common.h"
#include <bfsys/bf_sal/bf_sys_intf.h>
#include <bf_rt/bf_rt.h>

/***********************************************************************************
 * This sample c application code is based on the P4 program
 * tna_counter.p4
 * Please refer to the P4 program and the generated bf-rt.json for information
 *on
 * the tables contained in the P4 program, and the associated key and data
 *fields.
 **********************************************************************************/

// Structure definition to represent the key of the forward_dst table
typedef struct forward_dst_key_ { uint64_t mac_dst_addr; } forward_dst_key_t;

// Structure definition to represent the data of the forward_dst table for
// action
// "hit_dst"
typedef struct forward_dst_hit_dst_data_ {
  uint16_t port;
} forward_dst_hit_dst_data_t;

// Structure definition tp represent the data of the forward_dst table
typedef struct forward_dst_data_ {
  union {
    forward_dst_hit_dst_data_t hit_dst_data;
  } data;
  // Based on the action_id, contents of the enum are interpreted
  bf_rt_id_t action_id;
} forward_dst_data_t;

typedef struct counter_data_ {
  uint32_t pkts;
  uint32_t bytes;
} counter_data_t;

// Key field ids, table data field ids, action ids, Table hdl required for
// interacting with the table
const bf_rt_info_hdl *bfrt_info = NULL;
const bf_rt_table_hdl *forward_dst_table = NULL;
const bf_rt_table_hdl *counter_table = NULL;
bf_rt_session_hdl *session = NULL;

bf_rt_table_key_hdl *fwd_table_key;
bf_rt_table_data_hdl *fwd_table_data;

bf_rt_table_key_hdl *counter_table_key;
bf_rt_table_data_hdl *counter_table_data;

// Key field ids
bf_rt_id_t mac_dst_field_id = 0;
bf_rt_id_t counter_index_field_id = 0;

// Action Ids
bf_rt_id_t hit_dst_action_id = 0;

// Data field Ids for hit_dst action
bf_rt_id_t hit_dst_port_field_id = 0;

// Data field Ids for counter fields
bf_rt_id_t counter_bytes_field_id = 0;
bf_rt_id_t counter_pkts_field_id = 0;

#define ALL_PIPES 0xffff
bf_rt_target_t dev_tgt;

bool interactive = true;

// This function does the initial set_up of getting bfrt_info object associated
// with the P4 program from which all other required objects are obtained
void set_up(void) {
  dev_tgt.dev_id = 0;
  dev_tgt.pipe_id = ALL_PIPES;

  // Get bfrt_info object from dev_id and p4 program name
  bf_status_t bf_status =
      bf_rt_info_get(dev_tgt.dev_id, "tna_counter", &bfrt_info);
  // Check for status
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Create a session object
  bf_status = bf_rt_session_create(&session);
  // Check for status
  bf_sys_assert(bf_status == BF_SUCCESS);
}

// This function does the initial set up of getting key field-ids, action-ids
// and data field ids associated with the forward_dst table. This is done once
// during init time.
void table_set_up(void) {
  // Get table object from name
  bf_status_t bf_status = bf_rt_table_from_name_get(
      bfrt_info, "SwitchIngress.forward_dst", &forward_dst_table);
  bf_sys_assert(bf_status == BF_SUCCESS);
  bf_status =
      bf_rt_table_from_name_get(bfrt_info, "indirect_counter", &counter_table);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get action Ids for hit_dst action
  bf_status = bf_rt_action_name_to_id(
      forward_dst_table, "SwitchIngress.hit_dst", &hit_dst_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get field-ids for key field and data fields
  bf_status = bf_rt_key_field_id_get(
      forward_dst_table, "hdr.ethernet.dst_addr", &mac_dst_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_key_field_id_get(
      counter_table, "$COUNTER_INDEX", &counter_index_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * DATA FIELD ID GET FOR "hit_dst" ACTION
   **********************************************************************/
  bf_status = bf_rt_data_field_id_with_action_get(
      forward_dst_table, "port", hit_dst_action_id, &hit_dst_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_data_field_id_get(
      counter_table, "$COUNTER_SPEC_BYTES", &counter_bytes_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);
  bf_status = bf_rt_data_field_id_get(
      counter_table, "$COUNTER_SPEC_PKTS", &counter_pkts_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Allocate key and data once, and use reset across different uses
  bf_status = bf_rt_table_key_allocate(forward_dst_table, &fwd_table_key);
  bf_sys_assert(bf_status == BF_SUCCESS);
  bf_status = bf_rt_table_key_allocate(counter_table, &counter_table_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_data_allocate(forward_dst_table, &fwd_table_data);
  bf_sys_assert(bf_status == BF_SUCCESS);
  bf_status = bf_rt_table_data_allocate(counter_table, &counter_table_data);
  bf_sys_assert(bf_status == BF_SUCCESS);
}

// This function clears up any allocated memory during table_set_up()
void table_tear_down(void) {
  bf_status_t bf_status;
  // Deallocate key and data
  bf_status = bf_rt_table_key_deallocate(fwd_table_key);
  bf_sys_assert(bf_status == BF_SUCCESS);
  bf_status = bf_rt_table_key_deallocate(counter_table_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_data_deallocate(fwd_table_data);
  bf_sys_assert(bf_status == BF_SUCCESS);
  bf_status = bf_rt_table_data_deallocate(counter_table_data);
  bf_sys_assert(bf_status == BF_SUCCESS);
}
// This function clears up any allocated mem during set_up()
void tear_down(void) {
  bf_status_t bf_status;
  bf_status = bf_rt_session_destroy(session);
  // Check for status
  bf_sys_assert(bf_status == BF_SUCCESS);
}

/*******************************************************************************
 * Utility functions associated with "forward_dst" table in the P4 program.
 ******************************************************************************/

// This function sets the passed in ip_dst and vrf value into the key object
// passed using the setValue methods on the key object
void forward_dst_key_setup(const forward_dst_key_t *forward_dst_key,
                           bf_rt_table_key_hdl *table_key) {
  // Set value into the key object. Key type is "EXACT"
  bf_status_t bf_status = bf_rt_key_field_set_value(
      table_key, mac_dst_field_id, forward_dst_key->mac_dst_addr);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This function sets the passed in "dst_hit" action data  into the
// data object associated with the forward_dst table
void forward_dst_data_setup(const forward_dst_hit_dst_data_t *forward_dst_data,
                            bf_rt_table_data_hdl *table_data) {
  // Set value into the data object
  bf_status_t bf_status = bf_rt_data_field_set_value(
      table_data, hit_dst_port_field_id, forward_dst_data->port);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// sync counter table
void counter_table_sync(void) {
  bf_status_t bf_status = BF_SUCCESS;
  bf_rt_table_operations_hdl *operation_hdl;
  bf_status = bf_rt_table_operations_allocate(
      counter_table, BFRT_COUNTER_SYNC, &operation_hdl);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_operations_counter_sync_set(
      operation_hdl, session, &dev_tgt, NULL, NULL);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_operations_execute(counter_table, operation_hdl);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// Add to counter table
void counter_table_add(const uint64_t *counter_key,
                       const counter_data_t *counter_data) {
  // Reset key and data before use
  bf_rt_table_key_reset(counter_table, &counter_table_key);
  bf_rt_table_data_reset(counter_table, &counter_table_data);

  // Fill in the Key and Data object
  bf_status_t bf_status = bf_rt_key_field_set_value(
      counter_table_key, counter_index_field_id, *counter_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_data_field_set_value(
      counter_table_data, counter_bytes_field_id, counter_data->bytes);
  bf_sys_assert(bf_status == BF_SUCCESS);
  bf_status = bf_rt_data_field_set_value(
      counter_table_data, counter_pkts_field_id, counter_data->pkts);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Call table entry add API, if the request is for an add, else call modify
  bf_status_t status = BF_SUCCESS;
#ifdef BFRT_GENERIC_FLAGS
  status = bf_rt_table_entry_add(counter_table,
                                 session,
                                 &dev_tgt,
                                 0,
                                 counter_table_key,
                                 counter_table_data);
#else
  status = bf_rt_table_entry_add(
      counter_table, session, &dev_tgt, counter_table_key, counter_table_data);
#endif
  bf_sys_assert(status == BF_SUCCESS);
  bf_rt_session_complete_operations(session);
}

// This function adds or modifies an entry in the forward_dst table with
// "dst_hit"
// action. The workflow is similar for either table entry add or modify
void forward_dst_entry_add_modify(
    const forward_dst_key_t *forward_dst_key,
    const forward_dst_hit_dst_data_t *forward_dst_hit_dst_data,
    const bool add) {
  // Adding a match entry with below mac Addr to be forwarded to the below port
  // Reset key and data before use
  bf_rt_table_key_reset(forward_dst_table, &fwd_table_key);
  bf_rt_table_action_data_reset(
      forward_dst_table, hit_dst_action_id, &fwd_table_data);

  // Fill in the Key and Data object
  forward_dst_key_setup(forward_dst_key, fwd_table_key);
  forward_dst_data_setup(forward_dst_hit_dst_data, fwd_table_data);

  // Call table entry add API, if the request is for an add, else call modify
  bf_status_t status = BF_SUCCESS;
  if (add) {
#ifdef BFRT_GENERIC_FLAGS
    status = bf_rt_table_entry_add(
        forward_dst_table, session, &dev_tgt, 0, fwd_table_key, fwd_table_data);
#else
    status = bf_rt_table_entry_add(
        forward_dst_table, session, &dev_tgt, fwd_table_key, fwd_table_data);
#endif
  } else {
#ifdef BFRT_GENERIC_FLAGS
    status = bf_rt_table_entry_mod(
        forward_dst_table, session, &dev_tgt, 0, fwd_table_key, fwd_table_data);
#else
    status = bf_rt_table_entry_mod(
        forward_dst_table, session, &dev_tgt, fwd_table_key, fwd_table_data);
#endif
  }
  bf_sys_assert(status == BF_SUCCESS);
  bf_rt_session_complete_operations(session);
}

// This function process the entry obtained by a get call for a "dst_hit" action
// and populates the forward_dst_hit_dst_data_t structure
void forward_dst_process_hit_dst_entry_get(
    const bf_rt_table_data_hdl *data,
    forward_dst_hit_dst_data_t *hit_dst_data) {
  bf_status_t status = BF_SUCCESS;

  uint64_t port;
  status = bf_rt_data_field_get_value(data, hit_dst_port_field_id, &port);
  hit_dst_data->port = (uint16_t)port;
  bf_sys_assert(status == BF_SUCCESS);

  return;
}

// This function processes the entry obtained by a get call. Based on the action
// id the data object is intepreted.
void forward_dst_process_entry_get(const bf_rt_table_data_hdl *data,
                                   forward_dst_data_t *forward_dst_data) {
  // First get actionId, then based on that, fill in appropriate fields
  bf_status_t bf_status;
  bf_rt_id_t action_id;

  bf_status = bf_rt_data_action_id_get(data, &action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  if (action_id == hit_dst_action_id) {
    forward_dst_process_hit_dst_entry_get(data,
                                          &forward_dst_data->data.hit_dst_data);
  }
  return;
}

// This function reads an entry specified by the forward_dst_key, and fills in
// the
// passedin IpRoute object
void forward_dst_entry_get(const forward_dst_key_t *forward_dst_key,
                           forward_dst_data_t *data) {
  // Reset key and data before use
  bf_rt_table_key_reset(forward_dst_table, &fwd_table_key);
  // Data reset is done without action-id, since the action-id is filled in by
  // the get function
  bf_rt_table_data_reset(forward_dst_table, &fwd_table_data);

  forward_dst_key_setup(forward_dst_key, fwd_table_key);

  bf_status_t status = BF_SUCCESS;
// Entry get from hardware with the flag set to read from hardware
#ifdef BFRT_GENERIC_FLAGS
  uint64_t flags = 0;
  BF_RT_FLAG_SET(flags, BF_RT_FROM_HW);
  status = bf_rt_table_entry_get(forward_dst_table,
                                 session,
                                 &dev_tgt,
                                 flags,
                                 fwd_table_key,
                                 fwd_table_data);
#else
  bf_rt_entry_read_flag_e flag = ENTRY_READ_FROM_HW;
  status = bf_rt_table_entry_get(forward_dst_table,
                                 session,
                                 &dev_tgt,
                                 fwd_table_key,
                                 fwd_table_data,
                                 flag);
#endif
  bf_sys_assert(status == BF_SUCCESS);

  forward_dst_process_entry_get(fwd_table_data, data);

  return;
}

// This function deletes an entry specified by the forward_dst_key
void forward_dst_entry_delete(const forward_dst_key_t *forward_dst_key) {
  // Reset key before use
  bf_rt_table_key_reset(forward_dst_table, &fwd_table_key);

  forward_dst_key_setup(forward_dst_key, fwd_table_key);

#ifdef BFRT_GENERIC_FLAGS
  bf_status_t status = bf_rt_table_entry_del(
      forward_dst_table, session, &dev_tgt, 0, fwd_table_key);
#else
  bf_status_t status = bf_rt_table_entry_del(
      forward_dst_table, session, &dev_tgt, fwd_table_key);
#endif
  bf_sys_assert(status == BF_SUCCESS);
  bf_rt_session_complete_operations(session);
  return;
}

// Function to iterate over all the entries in the table
void table_iterate(void) {
  // Table iteration involves the following
  //    1. Use the getFirst API to get the first entry
  //    2. Use the tableUsageGet API to get the number of entries currently in
  //    the table.
  //    3. Use the number of entries returned in step 2 and pass it as a
  //    parameter to getNext_n (as n) to get all the remaining entries
  bf_rt_table_key_hdl *first_key;
  bf_rt_table_data_hdl *first_data;

  bf_status_t bf_status =
      bf_rt_table_key_allocate(forward_dst_table, &first_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_data_allocate(forward_dst_table, &first_data);
  bf_sys_assert(bf_status == BF_SUCCESS);

#ifdef BFRT_GENERIC_FLAGS
  uint64_t flags = 0;
  BF_RT_FLAG_SET(flags, BF_RT_FROM_HW);
  bf_status = bf_rt_table_entry_get_first(
      forward_dst_table, session, &dev_tgt, flags, first_key, first_data);
#else
  bf_rt_entry_read_flag_e flag = ENTRY_READ_FROM_HW;
  bf_status = bf_rt_table_entry_get_first(
      forward_dst_table, session, &dev_tgt, first_key, first_data, flag);
#endif
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Process the first entry
  forward_dst_data_t dst_hit_data;
  forward_dst_process_entry_get(first_data, &dst_hit_data);

  // Get the usage of table
  uint32_t entry_count = 0;
#ifdef BFRT_GENERIC_FLAGS
  bf_status = bf_rt_table_usage_get(
      forward_dst_table, session, &dev_tgt, flags, &entry_count);
#else
  bf_status = bf_rt_table_usage_get(
      forward_dst_table, session, &dev_tgt, &entry_count, flag);
#endif
  bf_sys_assert(bf_status == BF_SUCCESS);

  if (entry_count == 1) {
    goto clean_first;
  }

  // allocate 2 pointer arrays for bf_rt_table_key_hdl* and
  // bf_rt_table_data_hdl*
  bf_rt_table_key_hdl **keys =
      bf_sys_malloc(sizeof(bf_rt_table_key_hdl *) * (entry_count - 1));
  bf_rt_table_data_hdl **datas =
      bf_sys_malloc(sizeof(bf_rt_table_data_hdl *) * (entry_count - 1));

  for (unsigned i = 0; i < entry_count - 1; ++i) {
    bf_status = bf_rt_table_key_allocate(forward_dst_table, &keys[i]);
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = bf_rt_table_data_allocate(forward_dst_table, &datas[i]);
    bf_sys_assert(bf_status == BF_SUCCESS);
  }

  // Get next N
  uint32_t num_returned = 0;
#ifdef BFRT_GENERIC_FLAGS
  bf_status = bf_rt_table_entry_get_next_n(forward_dst_table,
                                           session,
                                           &dev_tgt,
                                           flags,
                                           first_key,
                                           keys,
                                           datas,
                                           entry_count - 1,
                                           &num_returned);
#else
  bf_status = bf_rt_table_entry_get_next_n(forward_dst_table,
                                           session,
                                           &dev_tgt,
                                           first_key,
                                           keys,
                                           datas,
                                           entry_count - 1,
                                           &num_returned,
                                           flag);
#endif
  bf_sys_assert(bf_status == BF_SUCCESS);
  bf_sys_assert(num_returned == entry_count - 1);

  // Process the rest of the entries
  for (unsigned i = 0; i < entry_count - 1; ++i) {
    forward_dst_process_entry_get(datas[i], &dst_hit_data);
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
  bf_sys_free(keys);
  bf_sys_free(datas);
clean_first:
  // Deallocate the key, data used for entry_first_get
  bf_status = bf_rt_table_key_deallocate(first_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_data_deallocate(first_data);
  bf_sys_assert(bf_status == BF_SUCCESS);
  return;
}

void perform_driver_func(void) {
  // Do initial set up
  set_up();
  // Do table level set up
  table_set_up();
  // Add a table entry
  forward_dst_key_t forward_dst_key1 = {0x0A0B0C0D0E01};
  forward_dst_hit_dst_data_t forward_dst_hit_dst_data = {12};
  forward_dst_entry_add_modify(
      &forward_dst_key1, &forward_dst_hit_dst_data, true);

  // Add a few more entries
  forward_dst_key_t forward_dst_key2 = {0x0B0B0C0D0E02};
  forward_dst_hit_dst_data_t forward_dst_hit_dst_data2 = {2};
  forward_dst_entry_add_modify(
      &forward_dst_key2, &forward_dst_hit_dst_data2, true);

  forward_dst_key_t forward_dst_key3 = {0x0B0B0C0D0E03};
  forward_dst_hit_dst_data_t forward_dst_hit_dst_data3 = {4};
  forward_dst_entry_add_modify(
      &forward_dst_key3, &forward_dst_hit_dst_data3, true);

  forward_dst_key_t forward_dst_key4 = {0x0B0B0C0E0D04};
  forward_dst_hit_dst_data_t forward_dst_hit_dst_data4 = {10};
  forward_dst_entry_add_modify(
      &forward_dst_key4, &forward_dst_hit_dst_data4, true);

  // Add a few entries to the counter table
  counter_data_t cntr_data1 = {500, 600};
  uint64_t cntr_key1 = 12;
  counter_table_add(&cntr_key1, &cntr_data1);

  counter_data_t cntr_data2 = {106723, 843};
  uint64_t cntr_key2 = 2;
  counter_table_add(&cntr_key2, &cntr_data2);

  counter_data_t cntr_data3 = {0, 600};
  uint64_t cntr_key3 = 4;
  counter_table_add(&cntr_key3, &cntr_data3);

  counter_data_t cntr_data4 = {500, 900};
  uint64_t cntr_key4 = 10;
  counter_table_add(&cntr_key4, &cntr_data4);

  // perform sync operation on the counter table
  counter_table_sync();
  // Iterate over the forward table
  table_iterate();

  // Delete the table entries
  forward_dst_entry_delete(&forward_dst_key1);
  forward_dst_entry_delete(&forward_dst_key2);
  forward_dst_entry_delete(&forward_dst_key3);
  forward_dst_entry_delete(&forward_dst_key4);

  // Table tear down
  table_tear_down();
  // Tear Down
  tear_down();
  return;
}

int main(int argc, char **argv) {
  parse_opts_and_switchd_init(argc, argv);

  perform_driver_func();

  run_cli_or_cleanup();
  return 0;
}
