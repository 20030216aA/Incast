extern "C" {
#include "common.h"
}

#include <bf_rt/bf_rt.hpp>

/***********************************************************************************
 * This sample cpp application code is based on the P4 program tna_digest.p4
 * Please refer to the P4 program and the generated bf-rt.json for information
 *on
 * the tables contained in the P4 program, and the associated key and data
 *fields.
 **********************************************************************************/

namespace bfrt {
namespace examples {
namespace tna_digest {

// Structure definition to represent the key of the smac table
struct smacKey {
  uint64_t srcMac;
};

// Structure definition to represent the data of the ipRoute table for action
// "route"
struct smac_hitData {
  uint16_t port;
};

namespace {
// Key field ids, table data field ids, action ids, Table object required for
// interacting with the table
const bfrt::BfRtInfo *bfrtInfo = nullptr;
const bfrt::BfRtLearn *learn_obj = nullptr;
const bfrt::BfRtTable *smacTable = nullptr;
std::shared_ptr<bfrt::BfRtSession> session;

std::unique_ptr<bfrt::BfRtTableKey> bfrtTableKey;
std::unique_ptr<bfrt::BfRtTableData> bfrtTableData;

// Key field ids
bf_rt_id_t smac_srcMac_key_field_id = 0;

// Action Ids
bf_rt_id_t smac_hit_action_id = 0;
bf_rt_id_t smac_miss_action_id = 0;

// Data field Ids for smac hit action
bf_rt_id_t smac_hit_port_field_id = 0;

// Learn field-ids
bf_rt_id_t learn_dst_mac_addr_field_id = 0;
bf_rt_id_t learn_port_field_id = 0;
bf_rt_id_t learn_src_mac_addr_field_id = 0;

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
  auto bf_status = devMgr.bfRtInfoGet(dev_tgt.dev_id, "tna_exact_match_example", &bfrtInfo);
  // Check for status
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Create a session object
  session = bfrt::BfRtSession::sessionCreate();
}

/**********************************************************************
 * CALLBACK funciton that gets invoked upon a learn event
 *  1. session : Session object that was used to register the callback. This is
 *               the session that has to be used to manipulate the table in
 *response to a learn
 *               event. Its always advisable to use a single session to
 *manipulate a single
 *               table.
 *  2. vec : Vector of learnData objects
 *  3. learn_msg_hdl : Pointer to the underlying learn message object, on which
 *                     an ack needs to be done in order for the hardware
 *resource to be freed up.
 *                     This is to be done once all the processing on the learn
 *update is done.
 *
 *********************************************************************/
bf_status_t learn_callback(const bf_rt_target_t &bf_rt_tgt,
                           const std::shared_ptr<BfRtSession> session,
                           std::vector<std::unique_ptr<BfRtLearnData>> vec,
                           bf_rt_learn_msg_hdl *const learn_msg_hdl,
                           const void *cookie) {
  /***********************************************************
   * INSERT CALLBACK IMPLEMENTATION HERE
   **********************************************************/

  // Extract learn data fields from Learn Data object and use it as needed.

  /*********************************************************
  * WHEN DONE, ACK THE learn_msg_hdl
  ********************************************************/
  (void)bf_rt_tgt;
  (void)vec;
  (void)cookie;
  printf("Learn callback invoked\n");
  auto bf_status = learn_obj->bfRtLearnNotifyAck(session, learn_msg_hdl);
  bf_sys_assert(bf_status == BF_SUCCESS);
  return BF_SUCCESS;
}

// This function does the initial set up of getting key field-ids, action-ids
// and data field ids associated with the smac table. This is done once
// during init time.
void tableSetUp() {
  // Get table object from name
  auto bf_status =
      bfrtInfo->bfrtTableFromNameGet("SwitchIngress.smac", &smacTable);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get action Ids for hit and miss actions
  bf_status =
      smacTable->actionIdGet("SwitchIngress.smac_hit", &smac_hit_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      smacTable->actionIdGet("SwitchIngress.smac_miss", &smac_miss_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get field-ids for key field and data fields
  bf_status = smacTable->keyFieldIdGet("hdr.ethernet.src_addr",
                                       &smac_srcMac_key_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
  * DATA FIELD ID GET FOR "smac_hit" ACTION
  **********************************************************************/
  bf_status = smacTable->dataFieldIdGet(
      "port", smac_hit_action_id, &smac_hit_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Allocate key and data once, and use reset across different uses
  bf_status = smacTable->keyAllocate(&bfrtTableKey);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = smacTable->dataAllocate(&bfrtTableData);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * LEARN OBJECT GET FOR "digest" extern
   **********************************************************************/
  bf_status = bfrtInfo->bfrtLearnFromNameGet("SwitchIngressDeparser.digest_a",
                                             &learn_obj);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * LEARN FIELD ID GET FROM LEARN OBJECT
   **********************************************************************/
  bf_status =
      learn_obj->learnFieldIdGet("dst_addr", &learn_dst_mac_addr_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = learn_obj->learnFieldIdGet("port", &learn_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      learn_obj->learnFieldIdGet("src_addr", &learn_src_mac_addr_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * LEARN callback registration
   **********************************************************************/
  bf_status = learn_obj->bfRtLearnCallbackRegister(
      session, dev_tgt, learn_callback, nullptr);
  bf_sys_assert(bf_status == BF_SUCCESS);
}

// This function sets the passed in ip_dst and vrf value into the key object
// passed using the setValue methods on the key object
void smac_key_setup(const smacKey &smac_key, bfrt::BfRtTableKey *table_key) {
  // Set value into the key object. Key type is "EXACT"
  auto bf_status =
      table_key->setValue(smac_srcMac_key_field_id, smac_key.srcMac);
  bf_sys_assert(bf_status == BF_SUCCESS);
  return;
}

// This function sets the passed in "hit" action data  into the
// data object associated with the smac table
void smac_data_setup_for_hit(const smac_hitData &smac_data,
                             bfrt::BfRtTableData *table_data) {
  // Set value into the data object
  auto bf_status = table_data->setValue(smac_hit_port_field_id,
                                        static_cast<uint64_t>(smac_data.port));
  bf_sys_assert(bf_status == BF_SUCCESS);
  return;
}

// This function deletes an entry specified by the smac_key
void smac_entry_delete(const smacKey &smac_key) {
  // Reset key before use
  smacTable->keyReset(bfrtTableKey.get());

  smac_key_setup(smac_key, bfrtTableKey.get());

  uint64_t flags = 0;
  auto status =
      smacTable->tableEntryDel(*session, dev_tgt, flags, *bfrtTableKey);
  bf_sys_assert(status == BF_SUCCESS);
  session->sessionCompleteOperations();
  return;
}
}  // tna_digest
}  // examples
}  // bfrt

int main(int argc, char **argv) {
  parse_opts_and_switchd_init(argc, argv);

  // Do initial set up
  bfrt::examples::tna_digest::setUp();
  // Do table level set up
  bfrt::examples::tna_digest::tableSetUp();

  run_cli_or_cleanup();
  return 0;
}
