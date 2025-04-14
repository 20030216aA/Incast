extern "C" {
#include "common.h"
}

#include <iostream>
#include <bf_rt/bf_rt.hpp>

/***********************************************************************************
 * This sample cpp application code is based on the P4 program
 *tna_action_selector.p4
 * Please refer to the P4 program and the generated bf-rt.json for information
 *on
 * the tables contained in the P4 program, and the associated key and data
 *fields.
 **********************************************************************************/

namespace bfrt {
namespace examples {
namespace tna_action_selector {

// Structure definition to represent the key of the ipRoute table
struct ForwardKey {
  uint16_t port;
};

// Structure definition to represent the data of the ipRoute table for action
// "route"
struct Forward_HitData {
  uint16_t port;
};

namespace {
// Key field ids, table data field ids, action ids, Table object required for
// interacting with the table
const bfrt::BfRtInfo *bfrtInfo = nullptr;
const bfrt::BfRtTable *forwardTable = nullptr;
const bfrt::BfRtTable *actProfTable = nullptr;
const bfrt::BfRtTable *selTable = nullptr;
std::shared_ptr<bfrt::BfRtSession> session;

// KEY DECLEARATIONS
std::unique_ptr<bfrt::BfRtTableKey> match_tbl_key;
std::unique_ptr<bfrt::BfRtTableKey> act_prof_key;
std::unique_ptr<bfrt::BfRtTableKey> sel_table_key;

// DATA DECLARATIONS
std::unique_ptr<bfrt::BfRtTableData> match_tbl_data;
std::unique_ptr<bfrt::BfRtTableData> act_prof_data;
std::unique_ptr<bfrt::BfRtTableData> sel_table_data;

// Table ATTRIBUTE declaration
std::unique_ptr<bfrt::BfRtTableAttributes> table_attribute;

// Key field ids
bf_rt_id_t forward_port_field_id = 0;
bf_rt_id_t action_mbr_field_id = 0;
bf_rt_id_t sel_grp_field_id = 0;

// Action Ids
bf_rt_id_t hit_action_id = 0;

// Data field Ids for hit action
bf_rt_id_t hit_action_port_field_id = 0;

// Data field Ids for forward table
bf_rt_id_t forward_act_mbr_data_field_id = 0;
bf_rt_id_t forward_sel_grp_data_field_id = 0;

// Data field Ids for selector table
bf_rt_id_t sel_table_act_mbr_data_field_id = 0;
bf_rt_id_t sel_table_act_mbr_status_data_field_id = 0;
bf_rt_id_t sel_table_max_grp_size_data_field_id = 0;

#define ALL_PIPES 0xffff
bf_rt_target_t dev_tgt;
}  // anonymous namespace

void sel_update_callback(const std::shared_ptr<BfRtSession> session,
                         const bf_rt_target_t &bf_rt_tgt,
                         const void *cookie,
                         const bf_rt_id_t sel_grp_id,
                         const bf_rt_id_t act_mbr_id,
                         const int &logical_entry_index,
                         const bool &is_add) {
  (void)session;
  (void)bf_rt_tgt;
  (void)cookie;
  (void)sel_grp_id;
  (void)act_mbr_id;
  (void)logical_entry_index;
  (void)is_add;
  std::cout << "Received callback for selector table" << std::endl;
  std::cout << "	Selector Group Id : " << sel_grp_id << std::endl;
  std::cout << "	Action member Id : " << act_mbr_id << std::endl;
  std::cout << " 	Logical entry Index : " << logical_entry_index
            << std::endl;
  std::cout << "        Entry Add : " << is_add << std::endl;
}

// This function does the initial setUp of getting bfrtInfo object associated
// with the P4 program from which all other required objects are obtained
void setUp() {
  dev_tgt.dev_id = 0;
  dev_tgt.pipe_id = ALL_PIPES;
  // Get devMgr singleton instance
  auto &devMgr = bfrt::BfRtDevMgr::getInstance();

  // Get bfrtInfo object from dev_id and p4 program name
  auto bf_status =
      devMgr.bfRtInfoGet(dev_tgt.dev_id, "tna_action_selector", &bfrtInfo);
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
  auto bf_status = bfrtInfo->bfrtTableFromNameGet("pipe.SwitchIngress.forward",
                                                  &forwardTable);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bfrtInfo->bfrtTableFromNameGet(
      "pipe.SwitchIngress.example_action_selector_ap", &actProfTable);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = bfrtInfo->bfrtTableFromNameGet(
      "pipe.SwitchIngress.example_action_selector", &selTable);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get action Id for hit actions
  bf_status = actProfTable->actionIdGet("SwitchIngress.hit", &hit_action_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Get field-ids for key field and data fields
  bf_status = forwardTable->keyFieldIdGet("ig_intr_md.ingress_port",
                                          &forward_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status =
      actProfTable->keyFieldIdGet("$ACTION_MEMBER_ID", &action_mbr_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = selTable->keyFieldIdGet("$SELECTOR_GROUP_ID", &sel_grp_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // DATA FIELD ID GET FOR FORWARD TABLE
  bf_status = forwardTable->dataFieldIdGet("$ACTION_MEMBER_ID",
                                           &forward_act_mbr_data_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = forwardTable->dataFieldIdGet("$SELECTOR_GROUP_ID",
                                           &forward_sel_grp_data_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  /***********************************************************************
   * DATA FIELD ID GET FOR "hit" ACTION
   **********************************************************************/
  bf_status = actProfTable->dataFieldIdGet(
      "port", hit_action_id, &hit_action_port_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // DATA FIELD ID GET FOR SELECTOR TABLE
  bf_status = selTable->dataFieldIdGet("$ACTION_MEMBER_ID",
                                       &sel_table_act_mbr_data_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = selTable->dataFieldIdGet("$ACTION_MEMBER_STATUS",
                                       &sel_table_act_mbr_status_data_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = selTable->dataFieldIdGet("$MAX_GROUP_SIZE",
                                       &sel_table_max_grp_size_data_field_id);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // KEY ALLOCATES
  bf_status = forwardTable->keyAllocate(&match_tbl_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = actProfTable->keyAllocate(&act_prof_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = selTable->keyAllocate(&sel_table_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // DATA ALLOCATES
  bf_status = forwardTable->dataAllocate(&match_tbl_data);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = actProfTable->dataAllocate(&act_prof_data);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = selTable->dataAllocate(&sel_table_data);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // TABLE ATTRIBUTE ALLOCATE
  bf_status = selTable->attributeAllocate(
      bfrt::TableAttributesType::SELECTOR_UPDATE_CALLBACK, &table_attribute);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // REGISTER SELECTOR UPDATE CALLBACK
  bf_status = table_attribute->selectorUpdateCbSet(
      true, session, sel_update_callback, nullptr);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // KEY ALLOCATES
  bf_status = forwardTable->keyAllocate(&match_tbl_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = actProfTable->keyAllocate(&act_prof_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = selTable->keyAllocate(&sel_table_key);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // DATA ALLOCATES
  bf_status = forwardTable->dataAllocate(&match_tbl_data);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = actProfTable->dataAllocate(&act_prof_data);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = selTable->dataAllocate(&sel_table_data);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // TABLE ATTRIBUTE ALLOCATE
  bf_status = selTable->attributeAllocate(
      bfrt::TableAttributesType::SELECTOR_UPDATE_CALLBACK, &table_attribute);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // REGISTER SELECTOR UPDATE CALLBACK
  std::cout << "Registering selector update callback for the selector table"
            << std::endl;
  bf_status = table_attribute->selectorUpdateCbSet(
      true, session, sel_update_callback, nullptr);
  bf_sys_assert(bf_status == BF_SUCCESS);

  uint64_t flags = 0;
  bf_status =
      selTable->tableAttributesSet(*session, dev_tgt, flags, *table_attribute);
  bf_sys_assert(bf_status == BF_SUCCESS);

  // Read back the table attribute and ensure it's set to the right value
  std::unique_ptr<bfrt::BfRtTableAttributes> read_table_attribute;
  bf_status = selTable->attributeAllocate(
      bfrt::TableAttributesType::SELECTOR_UPDATE_CALLBACK,
      &read_table_attribute);
  bf_sys_assert(bf_status == BF_SUCCESS);

  bf_status = selTable->tableAttributesGet(
      *session, dev_tgt, flags, read_table_attribute.get());
  bf_sys_assert(bf_status == BF_SUCCESS);

  bool read_enable;
  bfrt::BfRtSession *read_session;
  // std::shared_ptr<bfrt::BfRtSession> read_session;
  selUpdateCb read_sel_update_callback;
  void *read_cookie;
  bf_status = read_table_attribute->selectorUpdateCbGet(
      &read_enable, &read_session, &read_sel_update_callback, &read_cookie);
  auto read_sel_update_callback_raw_ptr =
      *read_sel_update_callback
           .target<void (*)(const std::shared_ptr<BfRtSession> session,
                            const bf_rt_target_t &bf_rt_tgt,
                            const void *cookie,
                            const bf_rt_id_t sel_grp_id,
                            const bf_rt_id_t act_mbr_id,
                            const int &logical_table_index,
                            const bool &is_add)>();
  bf_sys_assert(bf_status == BF_SUCCESS);
  bf_sys_assert(read_enable == true);
  bf_sys_assert(read_session == session.get());
  bf_sys_assert(read_sel_update_callback_raw_ptr == sel_update_callback);
  bf_sys_assert(read_cookie == nullptr);
}

void tableEntryAdd() {
  uint64_t flags = 0;
  unsigned int num_members = 40;
  bf_status_t bf_status = BF_SUCCESS;

  std::cout << "Installing " << num_members << " Action profile members"
            << std::endl;
  // FIRST ADD ACTION MEMBERS IN THE ACTION PROFILE TABLE
  for (unsigned int i = 0; i < num_members; ++i) {
    bf_status = actProfTable->keyReset(act_prof_key.get());
    bf_sys_assert(bf_status == BF_SUCCESS);

    std::cout << " 	Action Member Id : " << i + 1 << std::endl;
    bf_status = act_prof_key->setValue(action_mbr_field_id,
                                       static_cast<uint64_t>(i + 1));
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = actProfTable->dataReset(hit_action_id, act_prof_data.get());
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = act_prof_data->setValue(hit_action_port_field_id,
                                        static_cast<uint64_t>(i + 3));
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = actProfTable->tableEntryAdd(
        *session, dev_tgt, flags, *act_prof_key, *act_prof_data);
    bf_sys_assert(bf_status == BF_SUCCESS);
    session->sessionCompleteOperations();
  }

  unsigned int num_groups = 10;
  unsigned int num_members_per_group = 4;

  std::cout << "Installing " << num_groups << " with " << num_members_per_group
            << " members per group" << std::endl;
  // NEXT CREATE GROUPS WITH MEMBERS
  for (unsigned int i = 0; i < num_groups; ++i) {
    bf_status = selTable->keyReset(sel_table_key.get());
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = selTable->dataReset(sel_table_data.get());
    bf_sys_assert(bf_status == BF_SUCCESS);

    std::cout << "	Selector Member Id : " << i + 1 << std::endl;
    bf_status =
        sel_table_key->setValue(sel_grp_field_id, static_cast<uint64_t>(i + 1));
    bf_sys_assert(bf_status == BF_SUCCESS);

    std::vector<bf_rt_id_t> members;
    std::vector<bool> member_status(num_members_per_group, true);
    for (unsigned int j = 0; j < num_members_per_group; ++j) {
      std::cout << "		Action Member Id : "
                << ((i + j) % num_members + 1) << std::endl;
      members.push_back(((i + j) % num_members) + 1);
    }
    bf_status =
        sel_table_data->setValue(sel_table_act_mbr_data_field_id, members);
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = sel_table_data->setValue(sel_table_act_mbr_status_data_field_id,
                                         member_status);
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status =
        sel_table_data->setValue(sel_table_max_grp_size_data_field_id,
                                 static_cast<uint64_t>(num_members_per_group));
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = selTable->tableEntryAdd(
        *session, dev_tgt, flags, *sel_table_key, *sel_table_data);
    bf_sys_assert(bf_status == BF_SUCCESS);
    session->sessionCompleteOperations();
  }

  unsigned int num_entries = 10;

  std::cout << "Installing " << num_entries << " entries into forward table"
            << std::endl;
  // NEXT ADD ENTRIES TO FORWARD TABLE REFERENCING THE GROUPS
  for (unsigned int i = 0; i < num_entries; ++i) {
    bf_status = forwardTable->keyReset(match_tbl_key.get());
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = forwardTable->dataReset(match_tbl_data.get());
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = match_tbl_key->setValue(forward_port_field_id,
                                        static_cast<uint64_t>(i + 1));
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = match_tbl_data->setValue(forward_sel_grp_data_field_id,
                                         static_cast<uint64_t>(i + 1));
    bf_sys_assert(bf_status == BF_SUCCESS);

    bf_status = forwardTable->tableEntryAdd(
        *session, dev_tgt, flags, *match_tbl_key, *match_tbl_data);

    bf_sys_assert(bf_status == BF_SUCCESS);
    session->sessionCompleteOperations();
  }
}
}  // tna_action_selector
}  // examples
}  // bfrt

int main(int argc, char **argv) {
  parse_opts_and_switchd_init(argc, argv);

  // Do initial set up
  bfrt::examples::tna_action_selector::setUp();
  // Do table level set up
  bfrt::examples::tna_action_selector::tableSetUp();
  // Do table Entry Add
  bfrt::examples::tna_action_selector::tableEntryAdd();

  run_cli_or_cleanup();
  return 0;
}
