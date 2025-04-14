extern "C" {
#include "common.h"
}

#include <bf_rt/bf_rt.hpp>

/***********************************************************************************
 * This sample cpp application code is based on the P4 program
 *tna_idletimeout.p4
 * Please refer to the P4 program and the generated bf-rt.json for information
 *on
 * the tables contained in the P4 program, and the associated key and data
 *fields.
 **********************************************************************************/

/************************************************************************************
 * This example demonstarates the following for a P4 table with idle_timeout set
 *to True
 * 1. Allocate an attribute object and enable idle time out in NOTIFY_MODE with
 *required parameters.
 * 2. Using the attirbute object to call the attribute set API on table object.
 * 3. A hook for idle time out callback implementation.
 ********************************************************************/

namespace bfrt {
namespace examples {
namespace tna_idletimeout {

namespace {
// Key field ids, table data field ids, action ids, Table object required for
// interacting with the table
const bfrt::BfRtInfo *bfrtInfo = nullptr;
// const bfrt::BfRtLearn *learn_obj = nullptr;
const bfrt::BfRtTable *dmacTable = nullptr;
std::shared_ptr<bfrt::BfRtSession> session;

std::unique_ptr<bfrt::BfRtTableKey> bfrtTableKey;
std::unique_ptr<bfrt::BfRtTableData> bfrtTableData;
std::unique_ptr<BfRtTableAttributes> attr;

// Key field ids
bf_rt_id_t dmac_dstMac_key_field_id = 0;
bf_rt_id_t dmac_srcMac_key_field_id = 0;
bf_rt_id_t dmac_ipDst_key_field_id = 0;

// Action Ids
bf_rt_id_t dmac_hit_action_id = 0;

// Data field Ids for smac hit action
bf_rt_id_t dmac_hit_port_field_id = 0;

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
      devMgr.bfRtInfoGet(dev_tgt.dev_id, "tna_idletimeout", &bfrtInfo);
  // Check for status
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Create a session object
  session = bfrt::BfRtSession::sessionCreate();
}

/**********************************************************************
 * CALLBACK funciton that gets invoked upon a entry agin event. One per entry
 *  1. target : Device target from which the entry is aging out
 *  2. key : Pointer to the key object representing the entry which has aged out
 *  3. cookie : Pointer to the cookie which was given at the time of the
 *callback registration
 *
 *********************************************************************/
bf_status_t idletime_callback(const bf_rt_target_t &target,
                              const BfRtTableKey *key,
                              const void *cookie) {
  /***********************************************************
   * INSERT CALLBACK IMPLEMENTATION HERE
   **********************************************************/

  (void)target;
  (void)key;
  (void)cookie;
  return BF_SUCCESS;
}

// This function does the initial set up of getting key field-ids, action-ids
// and data field ids associated with the smac table. This is done once
// during init time.
void tableSetUp() {
  // Get table object from name
  auto bf_status =
      bfrtInfo->bfrtTableFromNameGet("SwitchIngress.dmac", &dmacTable);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get action Ids for hit and miss actions
  bf_status = dmacTable->actionIdGet("SwitchIngress.hit", &dmac_hit_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get field-ids for key field and data fields
  bf_status = dmacTable->keyFieldIdGet("hdr.ethernet.dst_addr",
                                       &dmac_dstMac_key_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);
  bf_status = dmacTable->keyFieldIdGet("hdr.ethernet.src_addr",
                                       &dmac_srcMac_key_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);
  bf_status =
      dmacTable->keyFieldIdGet("hdr.ipv4.dst_addr", &dmac_ipDst_key_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
  * DATA FIELD ID GET FOR "dmac_hit" ACTION
  **********************************************************************/
  bf_status = dmacTable->dataFieldIdGet(
      "port", dmac_hit_action_id, &dmac_hit_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Allocate key and data once, and use reset across different uses
  bf_status = dmacTable->keyAllocate(&bfrtTableKey);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = dmacTable->dataAllocate(&bfrtTableData);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * ALLOCATE TABLE ATTRIBUTE FOR ENABLING IDLE TIMEOUT AND REGISTER A CALLBACK
   **********************************************************************/
  bf_status =
      dmacTable->attributeAllocate(TableAttributesType::IDLE_TABLE_RUNTIME,
                                   TableAttributesIdleTableMode::NOTIFY_MODE,
                                   &attr);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Set min_ttl to 50 ms, max_ttl to 5000 ms and ttl_query intervale to 50 ms
  uint32_t min_ttl = 50;
  uint32_t max_ttl = 5000;
  uint32_t ttl_query_interval = 50;
  void *cookie = nullptr;

  bf_status = attr->idleTableNotifyModeSet(
      true, idletime_callback, ttl_query_interval, max_ttl, min_ttl, cookie);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * CALL ATTRIBUTE SET API ON TABLE OBJECT
   **********************************************************************/
  uint32_t flags = 0;
  bf_status =
      dmacTable->tableAttributesSet(*session, dev_tgt, flags, *attr.get());
  bf_sys_assert(bf_status == BF_SUCCESS);
}
}  // tna_idletimeout
}  // examples
}  // bfrt

int main(int argc, char **argv) {
  parse_opts_and_switchd_init(argc, argv);

  // Do initial set up
  bfrt::examples::tna_idletimeout::setUp();
  // Do table level set up
  bfrt::examples::tna_idletimeout::tableSetUp();

  run_cli_or_cleanup();
  return 0;
}
