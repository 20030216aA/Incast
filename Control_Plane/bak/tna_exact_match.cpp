extern "C" {
#include "common.h"
}

#include <bf_rt/bf_rt.hpp>

/***********************************************************************************
 * This sample cpp application code is based on the P4 program
 *tna_exact_match.p4
 * Please refer to the P4 program and the generated bf-rt.json for information
 *on
 * the tables contained in the P4 program, and the associated key and data
 *fields.
 **********************************************************************************/

namespace bfrt {
namespace examples {
namespace tna_exact_match {

// Structure definition to represent the key of the ipRoute table
struct IpRouteKey {
  uint32_t ipDstAddr;
  uint16_t vrf;
};

// Structure definition to represent the data of the ipRoute table for action
// "route"
struct IpRoute_routeData {
  uint64_t srcMac;
  uint64_t dstMac;
  uint16_t dst_port;
};

// Structure definition to represent the data of the ipRoute table for action
// "nat"
struct IpRoute_natData {
  uint32_t srcAddr;
  uint32_t dstAddr;
  uint16_t dst_port;
};

// Structure definition tp represent the data of the ipRoute table
struct IpRouteData {
  union {
    IpRoute_routeData route_data;
    IpRoute_natData nat_data;
  } data;
  // Based on the action_id, contents of the enum are interpreted
  bf_rt_id_t action_id;
};

namespace {
// Key field ids, table data field ids, action ids, Table object required for
// interacting with the table
const bfrt::BfRtInfo *bfrtInfo = nullptr;
const bfrt::BfRtTable *ipRouteTable = nullptr;
std::shared_ptr<bfrt::BfRtSession> session;

std::unique_ptr<bfrt::BfRtTableKey> bfrtTableKey;
std::unique_ptr<bfrt::BfRtTableData> bfrtTableData;

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
}  // anonymous namespace

// This function does the initial setUp of getting bfrtInfo object associated
// with the P4 program from which all other required objects are obtained
void setUp() {
  dev_tgt.dev_id = 0;
  dev_tgt.pipe_id = ALL_PIPES;
  // Get devMgr singleton instance
  auto &devMgr = bfrt::BfRtDevMgr::getInstance();

  // Get bfrtInfo object from dev_id and p4 program name
  auto bf_status =
      devMgr.bfRtInfoGet(dev_tgt.dev_id, "tna_exact_match", &bfrtInfo);
  // Check for status
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Create a session object
  session = bfrt::BfRtSession::sessionCreate();
}

// This function does the initial set up of getting key field-ids, action-ids
// and data field ids associated with the ipRoute table. This is done once
// during init time.
void tableSetUp() {
  // Get table object from name
  auto bf_status =
      bfrtInfo->bfrtTableFromNameGet("SwitchIngress.ipRoute", &ipRouteTable);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get action Ids for route and nat actions
  bf_status = ipRouteTable->actionIdGet("SwitchIngress.route",
                                        &ipRoute_route_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      ipRouteTable->actionIdGet("SwitchIngress.nat", &ipRoute_nat_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get field-ids for key field and data fields
  bf_status = ipRouteTable->keyFieldIdGet("hdr.ipv4.dst_addr",
                                          &ipRoute_ip_dst_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = ipRouteTable->keyFieldIdGet("vrf", &ipRoute_vrf_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * DATA FIELD ID GET FOR "route" ACTION
   **********************************************************************/
  bf_status =
      ipRouteTable->dataFieldIdGet("srcMac",
                                   ipRoute_route_action_id,
                                   &ipRoute_route_action_src_mac_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      ipRouteTable->dataFieldIdGet("dstMac",
                                   ipRoute_route_action_id,
                                   &ipRoute_route_action_dst_mac_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = ipRouteTable->dataFieldIdGet(
      "dst_port", ipRoute_route_action_id, &ipRoute_route_action_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * DATA FIELD ID GET FOR "nat" ACTION
   **********************************************************************/
  bf_status = ipRouteTable->dataFieldIdGet(
      "srcAddr", ipRoute_nat_action_id, &ipRoute_nat_action_ip_src_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = ipRouteTable->dataFieldIdGet(
      "dstAddr", ipRoute_nat_action_id, &ipRoute_nat_action_ip_dst_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = ipRouteTable->dataFieldIdGet(
      "dst_port", ipRoute_nat_action_id, &ipRoute_nat_action_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Allocate key and data once, and use reset across different uses
  bf_status = ipRouteTable->keyAllocate(&bfrtTableKey);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = ipRouteTable->dataAllocate(&bfrtTableData);
  bf_sys_assert(bf_status == BF_SUCCESS);
}

/*******************************************************************************
 * Utility functions associated with "ipRoute" table in the P4 program.
 ******************************************************************************/

// This function sets the passed in ip_dst and vrf value into the key object
// passed using the setValue methods on the key object
void ipRoute_key_setup(const IpRouteKey &ipRoute_key,
                       bfrt::BfRtTableKey *table_key) {
  // Set value into the key object. Key type is "EXACT"
  auto bf_status = table_key->setValue(
      ipRoute_ip_dst_field_id, static_cast<uint64_t>(ipRoute_key.ipDstAddr));
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = table_key->setValue(ipRoute_vrf_field_id,
                                  static_cast<uint64_t>(ipRoute_key.vrf));
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This function sets the passed in "route" action data  into the
// data object associated with the ipRoute table
void ipRoute_data_setup_for_route(const IpRoute_routeData &ipRoute_data,
                                  bfrt::BfRtTableData *table_data) {
  // Set value into the data object
  auto bf_status = table_data->setValue(ipRoute_route_action_src_mac_field_id,
                                        ipRoute_data.srcMac);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = table_data->setValue(ipRoute_route_action_dst_mac_field_id,
                                   ipRoute_data.dstMac);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      table_data->setValue(ipRoute_route_action_port_field_id,
                           static_cast<uint64_t>(ipRoute_data.dst_port));
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This functiona sets the passed in "nat" acton data into the
// data object associated with the ipRoute table and "nat" action within the
// ipRoute table
void ipRoute_data_setup_for_nat(const IpRoute_natData &ipRoute_data,
                                bfrt::BfRtTableData *table_data) {
  // Set value into the data object
  auto bf_status =
      table_data->setValue(ipRoute_nat_action_ip_src_field_id,
                           static_cast<uint64_t>(ipRoute_data.srcAddr));
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = table_data->setValue(ipRoute_nat_action_ip_dst_field_id,
                                   static_cast<uint64_t>(ipRoute_data.dstAddr));
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      table_data->setValue(ipRoute_nat_action_port_field_id,
                           static_cast<uint64_t>(ipRoute_data.dst_port));
  bf_sys_assert(bf_status == BF_SUCCESS);

  return;
}

// This function adds or modifies an entry in the ipRoute table with "route"
// action. The workflow is similar for either table entry add or modify
void ipRoute_entry_add_modify_with_route(const IpRouteKey &ipRoute_key,
                                         const IpRoute_routeData &ipRoute_data,
                                         const bool &add) {
  // Adding a match entry with below mac Addr to be forwarded to the below port
  // Reset key and data before use
  ipRouteTable->keyReset(bfrtTableKey.get());
  ipRouteTable->dataReset(ipRoute_route_action_id, bfrtTableData.get());

  // Fill in the Key and Data object
  ipRoute_key_setup(ipRoute_key, bfrtTableKey.get());
  ipRoute_data_setup_for_route(ipRoute_data, bfrtTableData.get());

  // Call table entry add API, if the request is for an add, else call modify
  bf_status_t status = BF_SUCCESS;
  uint64_t flags = 0;
  if (add) {
    status = ipRouteTable->tableEntryAdd(
        *session, dev_tgt, flags, *bfrtTableKey, *bfrtTableData);
  } else {
    status = ipRouteTable->tableEntryMod(
        *session, dev_tgt, flags, *bfrtTableKey, *bfrtTableData);
  }
  bf_sys_assert(status == BF_SUCCESS);
  session->sessionCompleteOperations();
}

// This function adds or modifies an entry in the ipRoute table with "nat"
// action. The workflow is similar for either table entry add or modify
void ipRoute_entry_add_modify_with_nat(const IpRouteKey &ipRoute_key,
                                       const IpRoute_natData &ipRoute_data,
                                       const bool &add) {
  // Reset key and data before use
  ipRouteTable->keyReset(bfrtTableKey.get());
  ipRouteTable->dataReset(ipRoute_nat_action_id, bfrtTableData.get());

  ipRoute_key_setup(ipRoute_key, bfrtTableKey.get());
  ipRoute_data_setup_for_nat(ipRoute_data, bfrtTableData.get());

  // Call table entry add API, if the request is for an add, else call modify
  bf_status_t status = BF_SUCCESS;
  uint64_t flags = 0;
  if (add) {
    status = ipRouteTable->tableEntryAdd(
        *session, dev_tgt, flags, *bfrtTableKey, *bfrtTableData);
  } else {
    status = ipRouteTable->tableEntryMod(
        *session, dev_tgt, flags, *bfrtTableKey, *bfrtTableData);
  }
  bf_sys_assert(status == BF_SUCCESS);
  session->sessionCompleteOperations();
  return;
}

// This function process the entry obtained by a get call for a "route" action
// and populates the IpRoute_routeData structure
void ipRoute_process_route_entry_get(const bfrt::BfRtTableData &data,
                                     IpRoute_routeData *route_data) {
  bf_status_t status = BF_SUCCESS;

  status =
      data.getValue(ipRoute_route_action_src_mac_field_id, &route_data->srcMac);
  bf_sys_assert(status == BF_SUCCESS);

  status =
      data.getValue(ipRoute_route_action_dst_mac_field_id, &route_data->dstMac);
  bf_sys_assert(status == BF_SUCCESS);

  uint64_t port;
  status = data.getValue(ipRoute_route_action_port_field_id, &port);
  route_data->dst_port = static_cast<uint16_t>(port);
  bf_sys_assert(status == BF_SUCCESS);

  return;
}

// This function process the entry obtained by a get call for a "nat" action
// and populates the IpRoute_natData structure
void ipRoute_process_nat_entry_get(const bfrt::BfRtTableData &data,
                                   IpRoute_natData *nat_data) {
  bf_status_t status = BF_SUCCESS;

  uint64_t srcAddr;
  status = data.getValue(ipRoute_nat_action_ip_src_field_id, &srcAddr);
  bf_sys_assert(status == BF_SUCCESS);
  nat_data->srcAddr = static_cast<uint32_t>(srcAddr);

  uint64_t dstAddr;
  status = data.getValue(ipRoute_nat_action_ip_dst_field_id, &dstAddr);
  bf_sys_assert(status == BF_SUCCESS);
  nat_data->dstAddr = static_cast<uint32_t>(dstAddr);

  uint64_t dst_port;
  status = data.getValue(ipRoute_nat_action_port_field_id, &dst_port);
  bf_sys_assert(status == BF_SUCCESS);
  nat_data->dst_port = static_cast<uint16_t>(dst_port);

  return;
}

// This function processes the entry obtained by a get call. Based on the action
// id the data object is intepreted.
void ipRoute_process_entry_get(const bfrt::BfRtTableData &data,
                               IpRouteData *ipRoute_data) {
  // First get actionId, then based on that, fill in appropriate fields
  bf_status_t bf_status;
  bf_rt_id_t action_id;

  bf_status = data.actionIdGet(&action_id);
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
void ipRoute_entry_get(const IpRouteKey &ipRoute_key, IpRouteData *data) {
  // Reset key and data before use
  ipRouteTable->keyReset(bfrtTableKey.get());
  // Data reset is done without action-id, since the action-id is filled in by
  // the get function
  ipRouteTable->dataReset(bfrtTableData.get());

  ipRoute_key_setup(ipRoute_key, bfrtTableKey.get());

  bf_status_t status = BF_SUCCESS;
  // Entry get from hardware with the flag set to read from hardware
  uint64_t flags = 0;
  BF_RT_FLAG_SET(flags, BF_RT_FROM_HW);
  status = ipRouteTable->tableEntryGet(
      *session, dev_tgt, flags, *bfrtTableKey, bfrtTableData.get());
  bf_sys_assert(status == BF_SUCCESS);
  session->sessionCompleteOperations();

  ipRoute_process_entry_get(*bfrtTableData, data);

  return;
}

// This function deletes an entry specified by the ipRoute_key
void ipRoute_entry_delete(const IpRouteKey &ipRoute_key) {
  // Reset key before use
  ipRouteTable->keyReset(bfrtTableKey.get());

  ipRoute_key_setup(ipRoute_key, bfrtTableKey.get());

  uint64_t flags;
  BF_RT_FLAG_INIT(flags);
  auto status =
      ipRouteTable->tableEntryDel(*session, dev_tgt, flags, *bfrtTableKey);
  bf_sys_assert(status == BF_SUCCESS);
  session->sessionCompleteOperations();
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
  std::unique_ptr<BfRtTableKey> first_key;
  std::unique_ptr<BfRtTableData> first_data;

  auto bf_status = ipRouteTable->keyAllocate(&first_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = ipRouteTable->dataAllocate(&first_data);
  bf_sys_assert(bf_status == BF_SUCCESS);

  uint64_t flags;
  BF_RT_FLAG_INIT(flags);
  BF_RT_FLAG_SET(flags, BF_RT_FROM_HW);

  bf_status = ipRouteTable->tableEntryGetFirst(
      *session, dev_tgt, flags, first_key.get(), first_data.get());
  bf_sys_assert(bf_status == BF_SUCCESS);
  session->sessionCompleteOperations();

  // Process the first entry
  IpRouteData route_data;
  ipRoute_process_entry_get(*first_data, &route_data);

  // Get the usage of table
  uint32_t entry_count = 0;
  bf_status =
      ipRouteTable->tableUsageGet(*session, dev_tgt, flags, &entry_count);
  bf_sys_assert(bf_status == BF_SUCCESS);

  if (entry_count == 1) {
    return;
  }

  BfRtTable::keyDataPairs key_data_pairs;
  std::vector<std::unique_ptr<BfRtTableKey>> keys(entry_count - 1);
  std::vector<std::unique_ptr<BfRtTableData>> data(entry_count - 1);

  for (unsigned i = 0; i < entry_count - 1; ++i) {
    bf_status = ipRouteTable->keyAllocate(&keys[i]);
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = ipRouteTable->dataAllocate(&data[i]);
    bf_sys_assert(bf_status == BF_SUCCESS);

    key_data_pairs.push_back(std::make_pair(keys[i].get(), data[i].get()));
  }

  // Get next N
  uint32_t num_returned = 0;
  bf_status = ipRouteTable->tableEntryGetNext_n(*session,
                                                dev_tgt,
                                                flags,
                                                *first_key.get(),
                                                entry_count - 1,
                                                &key_data_pairs,
                                                &num_returned);
  bf_sys_assert(bf_status == BF_SUCCESS);
  bf_sys_assert(num_returned == entry_count - 1);
  session->sessionCompleteOperations();

  // Process the rest of the entries
  for (unsigned i = 0; i < entry_count - 1; ++i) {
    ipRoute_process_entry_get(*data[i], &route_data);
    // Do any required processing with the obtained data and key
  }
  return;
}
}  // tna_exact_match
}  // examples
}  // bfrt

int main(int argc, char **argv) {
  parse_opts_and_switchd_init(argc, argv);

  // Do initial set up
  bfrt::examples::tna_exact_match::setUp();
  // Do table level set up
  bfrt::examples::tna_exact_match::tableSetUp();

  run_cli_or_cleanup();
  return 0;
}
