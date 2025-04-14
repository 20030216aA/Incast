#include "common.h"

#include <bfsys/bf_sal/bf_sys_intf.h>
#include <bf_rt/bf_rt.h>

/***********************************************************************************
 * This sample c application code is based on the P4 program
 *tna_exact_match.p4
 * Please refer to the P4 program and the generated bf-rt.json for information
 *on
 * the tables contained in the P4 program, and the associated key and data
 *fields.
 **********************************************************************************/

// Structure definition to represent the key of the ipRoute table
typedef struct IpRouteKey {
  uint32_t ipDstAddr;
  uint16_t vrf;
} IpRouteKey;

// Structure definition to represent the data of the ipRoute table for action
// "route"
typedef struct IpRoute_routeData {
  uint64_t srcMac;
  uint64_t dstMac;
  uint16_t dst_port;
} IpRoute_routeData;

// Structure definition to represent the data of the ipRoute table for action
// "nat"
typedef struct IpRoute_natData {
  uint32_t srcAddr;
  uint32_t dstAddr;
  uint16_t dst_port;
} IpRoute_natData;

// Structure definition tp represent the data of the ipRoute table
typedef struct IpRouteData {
  // Based on the action_id, contents of the enum are interpreted
  bf_rt_id_t action_id;
  union {
    IpRoute_routeData route_data;
    IpRoute_natData nat_data;
  } data;
} IpRouteData;

// Key field ids, table data field ids, action ids, Table hdl required for
// interacting with the table
const bf_rt_info_hdl *bfrtInfo = NULL;
const bf_rt_table_hdl *ipRouteTable = NULL;
bf_rt_session_hdl *session = NULL;

bf_rt_table_key_hdl *bfrtTableKey;
bf_rt_table_data_hdl *bfrtTableData;

// Key field ids
bf_rt_id_t ipRoute_ip_dst_field_id = 0;
bf_rt_id_t ipRoute_vrf_field_id = 0;

// Action Ids
bf_rt_id_t ipRoute_route_action_id = 0;
bf_rt_id_t ipRoute_nat_action_id = 0;

// Data field Ids for route action
bf_rt_id_t ipRoute_route_action_src_mac_field_id = 0;
bf_rt_id_t ipRoute_route_action_dst_mac_field_id = 0;
bf_rt_id_t ipRoute_route_action_port_field_id = 0;

// Data field ids for nat action
bf_rt_id_t ipRoute_nat_action_ip_src_field_id = 0;
bf_rt_id_t ipRoute_nat_action_ip_dst_field_id = 0;
bf_rt_id_t ipRoute_nat_action_port_field_id = 0;

#define ALL_PIPES 0xffff
bf_rt_target_t dev_tgt;

bool interactive = true;

// This function does the initial setUp of getting bfrtInfo object associated
// with the P4 program from which all other required objects are obtained
void setUp() {
  dev_tgt.dev_id = 0;
  dev_tgt.pipe_id = ALL_PIPES;

  // Get bfrtInfo object from dev_id and p4 program name
  bf_status_t bf_status =
      bf_rt_info_get(dev_tgt.dev_id, "tna_exact_match", &bfrtInfo);
  // Check for status
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Create a session object
  bf_status = bf_rt_session_create(&session);
  // Check for status
  bf_sys_assert(bf_status == BF_SUCCESS);
}

// This function does the initial set up of getting key field-ids, action-ids
// and data field ids associated with the ipRoute table. This is done once
// during init time.
void tableSetUp() {
  // Get table object from name
  bf_status_t bf_status = bf_rt_table_from_name_get(
      bfrtInfo, "SwitchIngress.ipRoute", &ipRouteTable);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get action Ids for route and nat actions
  bf_status = bf_rt_action_name_to_id(
      ipRouteTable, "SwitchIngress.route", &ipRoute_route_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_action_name_to_id(
      ipRouteTable, "SwitchIngress.nat", &ipRoute_nat_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get field-ids for key field and data fields
  bf_status = bf_rt_key_field_id_get(
      ipRouteTable, "hdr.ipv4.dst_addr", &ipRoute_ip_dst_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      bf_rt_key_field_id_get(ipRouteTable, "vrf", &ipRoute_vrf_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * DATA FIELD ID GET FOR "route" ACTION
   **********************************************************************/
  bf_status = bf_rt_data_field_id_with_action_get(
      ipRouteTable,
      "srcMac",
      ipRoute_route_action_id,
      &ipRoute_route_action_src_mac_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_data_field_id_with_action_get(
      ipRouteTable,
      "dstMac",
      ipRoute_route_action_id,
      &ipRoute_route_action_dst_mac_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      bf_rt_data_field_id_with_action_get(ipRouteTable,
                                          "dst_port",
                                          ipRoute_route_action_id,
                                          &ipRoute_route_action_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * DATA FIELD ID GET FOR "nat" ACTION
   **********************************************************************/
  bf_status =
      bf_rt_data_field_id_with_action_get(ipRouteTable,
                                          "srcAddr",
                                          ipRoute_nat_action_id,
                                          &ipRoute_nat_action_ip_src_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      bf_rt_data_field_id_with_action_get(ipRouteTable,
                                          "dstAddr",
                                          ipRoute_nat_action_id,
                                          &ipRoute_nat_action_ip_dst_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      bf_rt_data_field_id_with_action_get(ipRouteTable,
                                          "dst_port",
                                          ipRoute_nat_action_id,
                                          &ipRoute_nat_action_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Allocate key and data once, and use reset across different uses
  bf_status = bf_rt_table_key_allocate(ipRouteTable, &bfrtTableKey);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_data_allocate(ipRouteTable, &bfrtTableData);
  bf_sys_assert(bf_status == BF_SUCCESS);
}

// This function clears up any allocated memory during tableSetUp()
void tableTearDown() {
  bf_status_t bf_status;
  // Deallocate key and data
  bf_status = bf_rt_table_key_deallocate(bfrtTableKey);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_data_deallocate(bfrtTableData);
  bf_sys_assert(bf_status == BF_SUCCESS);
}
// This function clears up any allocated mem during setUp()
void tearDown() {
  bf_status_t bf_status;
  bf_status = bf_rt_session_destroy(session);
  // Check for status
  bf_sys_assert(bf_status == BF_SUCCESS);
}

/*******************************************************************************
 * Utility functions associated with "ipRoute" table in the P4 program.
 ******************************************************************************/

// This function sets the passed in ip_dst and vrf value into the key object
// passed using the setValue methods on the key object
void ipRoute_key_setup(const IpRouteKey *ipRoute_key,
                       bf_rt_table_key_hdl *table_key) {
  // Set value into the key object. Key type is "EXACT"
  bf_status_t bf_status = bf_rt_key_field_set_value(
      table_key, ipRoute_ip_dst_field_id, ipRoute_key->ipDstAddr);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_key_field_set_value(
      table_key, ipRoute_vrf_field_id, ipRoute_key->vrf);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This function sets the passed in "route" action data  into the
// data object associated with the ipRoute table
void ipRoute_data_setup_for_route(const IpRoute_routeData *ipRoute_data,
                                  bf_rt_table_data_hdl *table_data) {
  // Set value into the data object
  bf_status_t bf_status = bf_rt_data_field_set_value(
      table_data, ipRoute_route_action_src_mac_field_id, ipRoute_data->srcMac);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_data_field_set_value(
      table_data, ipRoute_route_action_dst_mac_field_id, ipRoute_data->dstMac);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_data_field_set_value(
      table_data, ipRoute_route_action_port_field_id, ipRoute_data->dst_port);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This functiona sets the passed in "nat" acton data into the
// data object associated with the ipRoute table and "nat" action within the
// ipRoute table
void ipRoute_data_setup_for_nat(const IpRoute_natData *ipRoute_data,
                                bf_rt_table_data_hdl *table_data) {
  // Set value into the data object
  bf_status_t bf_status = bf_rt_data_field_set_value(
      table_data, ipRoute_nat_action_ip_src_field_id, ipRoute_data->srcAddr);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_data_field_set_value(
      table_data, ipRoute_nat_action_ip_dst_field_id, ipRoute_data->dstAddr);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_data_field_set_value(
      table_data, ipRoute_nat_action_port_field_id, ipRoute_data->dst_port);
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This function adds or modifies an entry in the ipRoute table with "route"
// action. The workflow is similar for either table entry add or modify
void ipRoute_entry_add_modify_with_route(const IpRouteKey *ipRoute_key,
                                         const IpRoute_routeData *ipRoute_data,
                                         const bool add) {
  // Adding a match entry with below mac Addr to be forwarded to the below port
  // Reset key and data before use
  bf_rt_table_key_reset(ipRouteTable, &bfrtTableKey);
  bf_rt_table_action_data_reset(
      ipRouteTable, ipRoute_route_action_id, &bfrtTableData);

  // Fill in the Key and Data object
  ipRoute_key_setup(ipRoute_key, bfrtTableKey);
  ipRoute_data_setup_for_route(ipRoute_data, bfrtTableData);

  // Call table entry add API, if the request is for an add, else call modify
  bf_status_t status = BF_SUCCESS;
  if (add) {
#ifdef BFRT_GENERIC_FLAGS
    status = bf_rt_table_entry_add(
        ipRouteTable, session, &dev_tgt, 0, bfrtTableKey, bfrtTableData);
#else
    status = bf_rt_table_entry_add(
        ipRouteTable, session, &dev_tgt, bfrtTableKey, bfrtTableData);
#endif
  } else {
#ifdef BFRT_GENERIC_FLAGS
    status = bf_rt_table_entry_mod(
        ipRouteTable, session, &dev_tgt, 0, bfrtTableKey, bfrtTableData);
#else
    status = bf_rt_table_entry_mod(
        ipRouteTable, session, &dev_tgt, bfrtTableKey, bfrtTableData);
#endif
  }
  bf_sys_assert(status == BF_SUCCESS);
  bf_rt_session_complete_operations(session);
}

// This function adds or modifies an entry in the ipRoute table with "nat"
// action. The workflow is similar for either table entry add or modify
void ipRoute_entry_add_modify_with_nat(const IpRouteKey *ipRoute_key,
                                       const IpRoute_natData *ipRoute_data,
                                       const bool add) {
// We can choose to reset key and data before use. It should work even if
// we don't
#if 0
  bf_rt_table_key_reset(ipRouteTable, &bfrtTableKey);
  bf_rt_table_action_data_reset(
      ipRouteTable, ipRoute_nat_action_id, &bfrtTableData);
#endif

  ipRoute_key_setup(ipRoute_key, bfrtTableKey);
  ipRoute_data_setup_for_nat(ipRoute_data, bfrtTableData);

  // Call table entry add API, if the request is for an add, else call modify
  bf_status_t status = BF_SUCCESS;
  if (add) {
#ifdef BFRT_GENERIC_FLAGS
    status = bf_rt_table_entry_add(
        ipRouteTable, session, &dev_tgt, 0, bfrtTableKey, bfrtTableData);
#else
    status = bf_rt_table_entry_add(
        ipRouteTable, session, &dev_tgt, bfrtTableKey, bfrtTableData);
#endif
  } else {
#ifdef BFRT_GENERIC_FLAGS
    status = bf_rt_table_entry_mod(
        ipRouteTable, session, &dev_tgt, 0, bfrtTableKey, bfrtTableData);
#else
    status = bf_rt_table_entry_mod(
        ipRouteTable, session, &dev_tgt, bfrtTableKey, bfrtTableData);
#endif
  }
  bf_sys_assert(status == BF_SUCCESS);
  bf_rt_session_complete_operations(session);
  return;
}

// This function process the entry obtained by a get call for a "route" action
// and populates the IpRoute_routeData structure
void ipRoute_process_route_entry_get(const bf_rt_table_data_hdl *data,
                                     IpRoute_routeData *route_data) {
  bf_status_t status = BF_SUCCESS;

  status = bf_rt_data_field_get_value(
      data, ipRoute_route_action_src_mac_field_id, &route_data->srcMac);
  bf_sys_assert(status == BF_SUCCESS);

  status = bf_rt_data_field_get_value(
      data, ipRoute_route_action_dst_mac_field_id, &route_data->dstMac);
  bf_sys_assert(status == BF_SUCCESS);

  uint64_t port;
  status = bf_rt_data_field_get_value(
      data, ipRoute_route_action_port_field_id, &port);
  route_data->dst_port = (uint16_t)port;
  bf_sys_assert(status == BF_SUCCESS);

  return;
}

// This function process the entry obtained by a get call for a "nat" action
// and populates the IpRoute_natData structure
void ipRoute_process_nat_entry_get(const bf_rt_table_data_hdl *data,
                                   IpRoute_natData *nat_data) {
  bf_status_t status = BF_SUCCESS;

  uint64_t srcAddr;
  status = bf_rt_data_field_get_value(
      data, ipRoute_nat_action_ip_src_field_id, &srcAddr);
  bf_sys_assert(status == BF_SUCCESS);
  nat_data->srcAddr = (uint32_t)srcAddr;

  uint64_t dstAddr;
  status = bf_rt_data_field_get_value(
      data, ipRoute_nat_action_ip_dst_field_id, &dstAddr);
  bf_sys_assert(status == BF_SUCCESS);
  nat_data->dstAddr = (uint32_t)dstAddr;

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
                               IpRouteData *ipRoute_data) {
  // First get actionId, then based on that, fill in appropriate fields
  bf_status_t bf_status;
  bf_rt_id_t action_id;

  bf_status = bf_rt_data_action_id_get(data, &action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  if (action_id == ipRoute_route_action_id) {
    ipRoute_process_route_entry_get(data, &ipRoute_data->data.route_data);
    ipRoute_data->action_id = action_id;
  } else if (action_id == ipRoute_nat_action_id) {
    ipRoute_process_nat_entry_get(data, &ipRoute_data->data.nat_data);
    ipRoute_data->action_id = action_id;
  }
  return;
}

// This function reads an entry specified by the ipRoute_key, and fills in the
// passedin IpRoute object
void ipRoute_entry_get(const IpRouteKey *ipRoute_key, IpRouteData *data) {
  // Reset key and data before use
  bf_rt_table_key_reset(ipRouteTable, &bfrtTableKey);
  // Data reset is done without action-id, since the action-id is filled in by
  // the get function
  bf_rt_table_data_reset(ipRouteTable, &bfrtTableData);

  ipRoute_key_setup(ipRoute_key, bfrtTableKey);

  bf_status_t status = BF_SUCCESS;
// Entry get from hardware with the flag set to read from hardware
#ifdef BFRT_GENERIC_FLAGS
  uint64_t flags = 0;
  BF_RT_FLAG_SET(flags, BF_RT_FROM_HW);
  status = bf_rt_table_entry_get(
      ipRouteTable, session, &dev_tgt, flags, bfrtTableKey, bfrtTableData);
#else
  bf_rt_entry_read_flag_e flag = ENTRY_READ_FROM_HW;
  status = bf_rt_table_entry_get(
      ipRouteTable, session, &dev_tgt, bfrtTableKey, bfrtTableData, flag);
#endif
  bf_sys_assert(status == BF_SUCCESS);

  ipRoute_process_entry_get(bfrtTableData, data);

  return;
}

// This function deletes an entry specified by the ipRoute_key
void ipRoute_entry_delete(const IpRouteKey *ipRoute_key) {
  // Reset key before use
  bf_rt_table_key_reset(ipRouteTable, &bfrtTableKey);

  ipRoute_key_setup(ipRoute_key, bfrtTableKey);

#ifdef BFRT_GENERIC_FLAGS
  bf_status_t status =
      bf_rt_table_entry_del(ipRouteTable, session, &dev_tgt, 0, bfrtTableKey);
#else
  bf_status_t status =
      bf_rt_table_entry_del(ipRouteTable, session, &dev_tgt, bfrtTableKey);
#endif
  bf_sys_assert(status == BF_SUCCESS);
  bf_rt_session_complete_operations(session);
  return;
}

// Function to iterate over all the entries in the table
void table_iterate() {
  // Table iteration involves the following
  //    1. Use the getFirst API to get the first entry
  //    2. Use the tableUsageGet API to get the number of entries currently in
  //    the table.
  //    3. Use the number of entries returned in step 2 and pass it as a
  //    parameter to getNext_n (as n) to get all the remaining entries
  bf_rt_table_key_hdl *first_key;
  bf_rt_table_data_hdl *first_data;

  bf_status_t bf_status = bf_rt_table_key_allocate(ipRouteTable, &first_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bf_rt_table_data_allocate(ipRouteTable, &first_data);
  bf_sys_assert(bf_status == BF_SUCCESS);

#ifdef BFRT_GENERIC_FLAGS
  uint64_t flags = 0;
  BF_RT_FLAG_SET(flags, BF_RT_FROM_HW);
  bf_status = bf_rt_table_entry_get_first(
      ipRouteTable, session, &dev_tgt, flags, first_key, first_data);
#else
  bf_rt_entry_read_flag_e flag = ENTRY_READ_FROM_HW;
  bf_status = bf_rt_table_entry_get_first(
      ipRouteTable, session, &dev_tgt, first_key, first_data, flag);
#endif
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Process the first entry
  IpRouteData route_data;
  ipRoute_process_entry_get(first_data, &route_data);

  // Get the usage of table
  uint32_t entry_count = 0;
#ifdef BFRT_GENERIC_FLAGS
  bf_status = bf_rt_table_usage_get(
      ipRouteTable, session, &dev_tgt, flags, &entry_count);
#else
  bf_status = bf_rt_table_usage_get(
      ipRouteTable, session, &dev_tgt, &entry_count, flag);
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
    bf_status = bf_rt_table_key_allocate(ipRouteTable, &keys[i]);
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = bf_rt_table_data_allocate(ipRouteTable, &datas[i]);
    bf_sys_assert(bf_status == BF_SUCCESS);
  }

  // Get next N
  uint32_t num_returned = 0;
#ifdef BFRT_GENERIC_FLAGS
  bf_status = bf_rt_table_entry_get_next_n(ipRouteTable,
                                           session,
                                           &dev_tgt,
                                           flags,
                                           first_key,
                                           keys,
                                           datas,
                                           entry_count - 1,
                                           &num_returned);
#else
  bf_status = bf_rt_table_entry_get_next_n(ipRouteTable,
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

void perform_driver_func() {
  // Do initial set up
  setUp();
  // Do table level set up
  tableSetUp();
  // Add a table entry
  IpRouteKey ipRoute_key1 = {0x0A0B0C01, 9};
  IpRouteData ipRoute_data_get = {0};
  IpRoute_routeData ipRoute_data = {0xaabbccddeeff, 0xffeeddccbbaa, 2};
  ipRoute_entry_add_modify_with_route(&ipRoute_key1, &ipRoute_data, true);

  // Modify the table entry
  IpRoute_natData ipNat_data = {0x264DCC42, 0xC0A80102, 7};
  ipRoute_entry_add_modify_with_nat(&ipRoute_key1, &ipNat_data, false);

  // Add a few more entries
  const IpRouteKey ipRoute_key2 = {0x0B0B0C02, 9};
  ipRoute_entry_add_modify_with_route(&ipRoute_key2, &ipRoute_data, true);
  IpRouteKey ipRoute_key3 = {0x0B0B0C03, 10};
  ipRoute_entry_add_modify_with_route(&ipRoute_key3, &ipRoute_data, true);
  IpRouteKey ipRoute_key4 = {0x0B0B0C04, 11};
  ipRoute_entry_add_modify_with_route(&ipRoute_key4, &ipRoute_data, true);

  ipRoute_entry_get(&ipRoute_key2, &ipRoute_data_get);
  bf_sys_assert(ipRoute_data_get.action_id == ipRoute_route_action_id);
  ipRoute_entry_add_modify_with_nat(&ipRoute_key2, &ipNat_data, false);

  // Iterate over the table
  table_iterate();

  // Delete the table entries

  ipRoute_entry_delete(&ipRoute_key1);
  ipRoute_entry_delete(&ipRoute_key2);
  ipRoute_entry_delete(&ipRoute_key3);
  ipRoute_entry_delete(&ipRoute_key4);

  // Table tear down
  tableTearDown();
  // Tear Down
  tearDown();
  return;
}

int main(int argc, char **argv) {
  parse_opts_and_switchd_init(argc, argv);

  perform_driver_func();

  run_cli_or_cleanup();
  return 0;
}
