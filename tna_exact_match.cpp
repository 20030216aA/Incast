extern "C" {
    #include "common.h"
    #include <tofino/bf_pal/bf_pal_port_intf.h>
    
}
#include <iostream>
#include <unordered_set>
#include <bf_rt/bf_rt.h>
#include <bf_rt/bf_rt.hpp>
#include <chrono>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <cinttypes>
#include <tofino/pdfixed/pd_conn_mgr.h>
#include <pipe_mgr/pktgen_intf.h>
#include <bfsys/bf_sal/bf_sys_intf.h>
#include <unistd.h>
#include <bf_types/bf_types.h>
#include <bfsys/bf_sal/bf_sys_assert.h>
namespace bfrt{
    namespace incast_detector {

namespace {
#define REGISTER_SIZE 65536
#define MAX_PORTS 256
#define ALL_PIPES 0xffff
std::unique_ptr<bfrt::BfRtTableKey> bfrtTableKey;
std::unique_ptr<bfrt::BfRtTableData> bfrtTableData;
std::shared_ptr<BfRtSession> session;
std::mutex reg_mutex;
std::atomic<bool> running{true};
bf_rt_target_t dev_tgt;
//第二版本
const BfRtInfo  *bfrtInfo = nullptr;
const BfRtLearn *learn_obj = nullptr;
const BfRtTable *Pkt_Register = nullptr;
const BfRtTable *Pkt_Register_01 = nullptr;
const BfRtTable *Port_Register = nullptr;
const BfRtTable *HASH1_reg = nullptr;
const BfRtTable *HASH2_reg = nullptr;
const BfRtTable *HASH3_reg = nullptr;
const BfRtTable *PACKET_THRESHOLD_th = nullptr;
const BfRtTable *PORT_THRESHOLD_th = nullptr;
static const bf_dev_port_t OUT_PORT0    = 152;
static const bf_dev_port_t OUT_PORT1    = 153;
static const bf_dev_port_t OUT_PORT2    = 154;
static const bf_dev_port_t OUT_PORT3    = 155;
// 字段ID缓存
bf_rt_id_t Pkt_Register_index_fid;
bf_rt_id_t Pkt_Register_value_fid;
bf_rt_id_t Pkt_Register_index_fid_01;
bf_rt_id_t Pkt_Register_value_fid_01;
bf_rt_id_t Port_Register_index_fid;
bf_rt_id_t Port_Register_value_fid;
bf_rt_id_t Digest_Port_fid;
bf_rt_id_t Digest_Time_fid;
bf_rt_id_t PACKET_THRESHOLD_index_fid;
bf_rt_id_t PORT_THRESHOLD_value_fid;
bf_rt_id_t PACKET_THRESHOLD_value_fid;
bf_rt_id_t PORT_THRESHOLD_index_fid;
bf_rt_id_t HASH1_reg_index_fid;
bf_rt_id_t HASH2_reg_index_fid;
bf_rt_id_t HASH3_reg_index_fid;
bf_rt_id_t HASH1_reg_value_fid;
bf_rt_id_t HASH2_reg_value_fid;
bf_rt_id_t HASH3_reg_value_fid;
/////////////////////////////////////////

} 

/* 初始化BfRT环境 */
void setUp() {
    dev_tgt.dev_id = 0;
    dev_tgt.pipe_id = ALL_PIPES;
    auto &devMgr = BfRtDevMgr::getInstance();
    auto status = devMgr.bfRtInfoGet(dev_tgt.dev_id, "incast_detect", &bfrtInfo);
    bf_sys_assert(status == BF_SUCCESS);
    session = BfRtSession::sessionCreate();
}
using namespace std;
/* 设置表和字段ID */
void tableSetUp() {

    auto status = bfrtInfo->bfrtTableFromNameGet("pipe.SwitchIngress.Pkt_Register", &Pkt_Register);
    bf_sys_assert(status == BF_SUCCESS);
    
    status = bfrtInfo->bfrtTableFromNameGet("pipe.SwitchIngress.Pkt_Register_01", &Pkt_Register_01);
    bf_sys_assert(status == BF_SUCCESS);

    status = bfrtInfo->bfrtTableFromNameGet("pipe.SwitchIngress.Port_Register", &Port_Register);
    bf_sys_assert(status == BF_SUCCESS);

 
    status = bfrtInfo->bfrtTableFromNameGet("pipe.SwitchIngress.PACKET_THRESHOLD", &PACKET_THRESHOLD_th);
    bf_sys_assert(status == BF_SUCCESS);
    
    status = bfrtInfo->bfrtTableFromNameGet("pipe.SwitchIngress.PORT_THRESHOLD", &PORT_THRESHOLD_th);
    bf_sys_assert(status == BF_SUCCESS);


    status = Pkt_Register->keyFieldIdGet("$REGISTER_INDEX", &Pkt_Register_index_fid);
    bf_sys_assert(status == BF_SUCCESS);
    
    status = Pkt_Register->dataFieldIdGet("SwitchIngress.Pkt_Register.f1", &Pkt_Register_value_fid);
    bf_sys_assert(status == BF_SUCCESS);

    status = Pkt_Register_01->dataFieldIdGet("SwitchIngress.Pkt_Register_01.f1", &Pkt_Register_value_fid_01);
    bf_sys_assert(status == BF_SUCCESS);

    status = Pkt_Register_01->keyFieldIdGet("$REGISTER_INDEX", &Pkt_Register_index_fid_01);
    bf_sys_assert(status == BF_SUCCESS);


    status = Port_Register->keyFieldIdGet("$REGISTER_INDEX", &Port_Register_index_fid);
    bf_sys_assert(status == BF_SUCCESS);
    
    status = Port_Register->dataFieldIdGet("SwitchIngress.Port_Register.f1", &Port_Register_value_fid);
    bf_sys_assert(status == BF_SUCCESS);

    status = PACKET_THRESHOLD_th->keyFieldIdGet("$REGISTER_INDEX", &PACKET_THRESHOLD_index_fid);
    bf_sys_assert(status == BF_SUCCESS);

    status = PACKET_THRESHOLD_th->dataFieldIdGet("SwitchIngress.PACKET_THRESHOLD.f1", &PACKET_THRESHOLD_value_fid);
    bf_sys_assert(status == BF_SUCCESS);
   

    status = PORT_THRESHOLD_th->keyFieldIdGet("$REGISTER_INDEX", &PORT_THRESHOLD_index_fid);
    bf_sys_assert(status == BF_SUCCESS);
   
    status = PORT_THRESHOLD_th->dataFieldIdGet("SwitchIngress.PORT_THRESHOLD.f1", &PORT_THRESHOLD_value_fid);
    bf_sys_assert(status == BF_SUCCESS);

    status = bfrtInfo->bfrtTableFromNameGet("pipe.SwitchIngress.HASH1", &HASH1_reg);
    bf_sys_assert(status == BF_SUCCESS);
    status = bfrtInfo->bfrtTableFromNameGet("pipe.SwitchIngress.HASH2", &HASH2_reg);
    bf_sys_assert(status == BF_SUCCESS);
    status = bfrtInfo->bfrtTableFromNameGet("pipe.SwitchIngress.HASH3", &HASH3_reg);
    bf_sys_assert(status == BF_SUCCESS);

    status = HASH1_reg->keyFieldIdGet("$REGISTER_INDEX", &HASH1_reg_index_fid);
    bf_sys_assert(status == BF_SUCCESS);
    
    status = HASH1_reg->dataFieldIdGet("SwitchIngress.HASH1.f1", &HASH1_reg_value_fid);
    bf_sys_assert(status == BF_SUCCESS);

    status = HASH2_reg->keyFieldIdGet("$REGISTER_INDEX", &HASH2_reg_index_fid);
    bf_sys_assert(status == BF_SUCCESS);
    
    status = HASH2_reg->dataFieldIdGet("SwitchIngress.HASH2.f1", &HASH2_reg_value_fid);
    bf_sys_assert(status == BF_SUCCESS);

    status = HASH3_reg->keyFieldIdGet("$REGISTER_INDEX", &HASH3_reg_index_fid);
    bf_sys_assert(status == BF_SUCCESS);
    
    status = HASH3_reg->dataFieldIdGet("SwitchIngress.HASH3.f1", &HASH3_reg_value_fid);
    bf_sys_assert(status == BF_SUCCESS);

}

/* 清空包计数寄存器（带键绑定）*/
void clearPacketCountReg() {
    std::lock_guard<std::mutex> lock(reg_mutex);
    
    std::unique_ptr<BfRtTableKey> key;
    auto status = Pkt_Register->keyAllocate(&key);
    if (status != BF_SUCCESS) {
        fprintf(stderr, "键分配失败: %s\n", bf_err_str(status));
        return;
    }

    for (uint16_t ingress = 0; ingress < MAX_PORTS; ++ingress) {
        for (uint16_t egress = 0; egress < MAX_PORTS; ++egress) {
            // 构造16位索引
            uint16_t index = (ingress << 8) | egress;
            
            // 绑定键值
            Pkt_Register->keyReset(key.get());
            status = key->setValue(Pkt_Register_index_fid, index);
            if (status != BF_SUCCESS) {
                fprintf(stderr, "键设置失败[%u][%u]: %s\n",
                       ingress, egress, bf_err_str(status));
                continue;
            }
            // 分配数据对象
            std::unique_ptr<BfRtTableData> data;
            status = Pkt_Register->dataAllocate(&data);
            if (status != BF_SUCCESS) {
                fprintf(stderr, "数据分配失败[%u][%u]: %s\n",
                       ingress, egress, bf_err_str(status));
                continue;
            }
            uint64_t flags = 0;

            
            uint64_t zero = 0;
            status = data->setValue(Pkt_Register_value_fid, zero);
            if (status != BF_SUCCESS) {
                fprintf(stderr, "数据设置失败[%u][%u]: %s\n",
                       ingress, egress, bf_err_str(status));
                continue;
            }

            // 执行写入操作
            status = Pkt_Register->tableEntryMod(
                *session, dev_tgt, flags, *key, *data
            );
            if (status != BF_SUCCESS) {
                fprintf(stderr, "写入失败[%u][%u]: %s\n",
                       ingress, egress, bf_err_str(status));
            }
            session->sessionCompleteOperations();
        }
    }
}


void clearPacketCountReg_01() {
    std::lock_guard<std::mutex> lock(reg_mutex);
    
    std::unique_ptr<BfRtTableKey> key;
    auto status = Pkt_Register_01->keyAllocate(&key);
    if (status != BF_SUCCESS) {
        fprintf(stderr, "键分配失败: %s\n", bf_err_str(status));
        return;
    }

    for (uint16_t ingress = 0; ingress < MAX_PORTS; ++ingress) {
        for (uint16_t egress = 0; egress < MAX_PORTS; ++egress) {
            // 构造16位索引
            uint16_t index = (ingress << 8) | egress;
            
            // 绑定键值
            Pkt_Register_01->keyReset(key.get());
            status = key->setValue(Pkt_Register_index_fid_01, index);
            if (status != BF_SUCCESS) {
                fprintf(stderr, "键设置失败[%u][%u]: %s\n",
                       ingress, egress, bf_err_str(status));
                continue;
            }
            // 分配数据对象
            std::unique_ptr<BfRtTableData> data;
            status = Pkt_Register_01->dataAllocate(&data);
            if (status != BF_SUCCESS) {
                fprintf(stderr, "数据分配失败[%u][%u]: %s\n",
                       ingress, egress, bf_err_str(status));
                continue;
            }
            uint64_t flags = 0;

            
            uint64_t zero = 0;
            status = data->setValue(Pkt_Register_value_fid_01, zero);
            if (status != BF_SUCCESS) {
                fprintf(stderr, "数据设置失败[%u][%u]: %s\n",
                       ingress, egress, bf_err_str(status));
                continue;
            }

            // 执行写入操作
            
            status = Pkt_Register_01->tableEntryMod(
                *session, dev_tgt, flags, *key, *data
            );
            if (status != BF_SUCCESS) {
                fprintf(stderr, "写入失败[%u][%u]: %s\n",
                       ingress, egress, bf_err_str(status));
            }
            session->sessionCompleteOperations();
        }
    }
}

/* 清空过载计数器（带键绑定）*/
void clearOverloadReg() {
    std::lock_guard<std::mutex> lock(reg_mutex);
    
    std::unique_ptr<BfRtTableKey> key;
    auto status = Port_Register->keyAllocate(&key);
    if (status != BF_SUCCESS) {
        fprintf(stderr, "键分配失败: %s\n", bf_err_str(status));
        return;
    }

    for (uint16_t egress = 0; egress < MAX_PORTS; ++egress) {
        // 绑定键值
        Port_Register->keyReset(key.get());
        status = key->setValue(Port_Register_index_fid, static_cast<uint64_t>(egress));
        if (status != BF_SUCCESS) {
            fprintf(stderr, "键设置失败[%u]: %s\n", egress, bf_err_str(status));
            continue;
        }

        // 分配数据对象
        std::unique_ptr<BfRtTableData> data;
        status = Port_Register->dataAllocate(&data);
        if (status != BF_SUCCESS) {
            fprintf(stderr, "数据分配失败[%u]: %s\n", egress, bf_err_str(status));
            continue;
        }

        // 设置寄存器值为0
        uint64_t zero = 0;
        status = data->setValue(Port_Register_value_fid, zero);
        if (status != BF_SUCCESS) {
            fprintf(stderr, "数据设置失败[%u]: %s\n", egress, bf_err_str(status));
            continue;
        }

        // 执行写入操作
        uint64_t flags = 0;
        status = Port_Register->tableEntryMod(
            *session, dev_tgt, flags, *key, *data
        );
        if (status != BF_SUCCESS) {
            fprintf(stderr, "写入失败[%u]: %s\n", egress, bf_err_str(status));
        }
        session->sessionCompleteOperations();
    }
}




// 定义寄存器信息结构体
struct RegInfo {
     
    const BfRtTable *reg_table = nullptr;    // 寄存器表对象
    bf_rt_id_t& reg_index_fid;   // 索引字段ID
    bf_rt_id_t& reg_value_fid;   // 值字段ID
    const char* reg_name;        // 寄存器名称（调试用）
};

void clearHashRegs() {
    std::lock_guard<std::mutex> lock(reg_mutex);

    // 配置所有需要清除的寄存器
    RegInfo regs[] = {
        {HASH1_reg, HASH1_reg_index_fid, HASH1_reg_value_fid, "HASH1"},
        {HASH2_reg, HASH2_reg_index_fid, HASH2_reg_value_fid, "HASH2"},
        {HASH3_reg, HASH3_reg_index_fid, HASH3_reg_value_fid, "HASH3"}
    };

    // 遍历所有寄存器
    for (auto& reg : regs) {
        std::unique_ptr<BfRtTableKey> key;
        auto status = reg.reg_table->keyAllocate(&key);
        if (status != BF_SUCCESS) {
            fprintf(stderr, "[%s] 键分配失败: %s\n", reg.reg_name, bf_err_str(status));
            continue;
        }

        // 遍历所有egress端口索引
        for (uint16_t egress = 0; egress < 1024; ++egress) {
            // 重置并设置键
            reg.reg_table->keyReset(key.get());
            status = key->setValue(reg.reg_index_fid, static_cast<uint64_t>(egress));
            if (status != BF_SUCCESS) {
                fprintf(stderr, "[%s] 键设置失败[%u]: %s\n", 
                       reg.reg_name, egress, bf_err_str(status));
                continue;
            }

            // 分配并设置数据
            std::unique_ptr<BfRtTableData> data;
            status = reg.reg_table->dataAllocate(&data);
            if (status != BF_SUCCESS) {
                fprintf(stderr, "[%s] 数据分配失败[%u]: %s\n", 
                       reg.reg_name, egress, bf_err_str(status));
                continue;
            }

            // 写入零值
            constexpr uint64_t zero = 0;
            status = data->setValue(reg.reg_value_fid, zero);
            if (status != BF_SUCCESS) {
                fprintf(stderr, "[%s] 数据设置失败[%u]: %s\n", 
                       reg.reg_name, egress, bf_err_str(status));
                continue;
            }

            // 执行修改操作
            status = reg.reg_table->tableEntryMod(
                *session, dev_tgt, 0 /*flags*/, *key, *data
            );
            if (status != BF_SUCCESS) {
                fprintf(stderr, "[%s] 写入失败[%u]: %s\n", 
                       reg.reg_name, egress, bf_err_str(status));
            }
            session->sessionCompleteOperations();
        }
    }
}


/**
 * @brief 直接设置包计数阈值寄存器（无键操作）
 * @param new_th 新的阈值（16位无符号整数）
 * @return BF_RT 状态码，BF_SUCCESS表示成功
 */
/**
 * @brief 更新包计数阈值寄存器（无键操作）
 * @param new_th 新的阈值（16位无符号整数）
 */

   /* 更新包计数阈值 */
void updatePacketThreshold(double new_th) {
    uint64_t flags = 0;
    uint64_t new_th01=(uint64_t)(new_th*10000000000);
  
    new_th01=new_th01>>6;
   // new_th*=10;//时间窗口
    // 分配键和数据对象
    
    auto status = PACKET_THRESHOLD_th->keyAllocate(&bfrtTableKey);
    bf_sys_assert(status == BF_SUCCESS);
    status = PACKET_THRESHOLD_th->dataAllocate(&bfrtTableData);
    bf_sys_assert(status == BF_SUCCESS);

    // 设置键值（假设参数表的键为单索引字段）
    status = bfrtTableKey->setValue(PACKET_THRESHOLD_index_fid, 0); // 固定索引或动态配置
    bf_sys_assert(status == BF_SUCCESS);
    
    // 设置数据值（修正字段映射）
    status = bfrtTableData->setValue(PACKET_THRESHOLD_value_fid, new_th01);
    bf_sys_assert(status == BF_SUCCESS);
    
    // 修正参数顺序：key必须作为第四个参数
    status = PACKET_THRESHOLD_th->tableEntryMod(
        *session, dev_tgt, flags, *bfrtTableKey, *bfrtTableData
    );
    bf_sys_assert(status == BF_SUCCESS);
}


/* 更新incast阈值 */
void updatePortThreshold(uint16_t new_th) {
uint64_t flags = 0;

auto status = PORT_THRESHOLD_th->keyAllocate(&bfrtTableKey);
bf_sys_assert(status == BF_SUCCESS);
status = PORT_THRESHOLD_th->dataAllocate(&bfrtTableData);
bf_sys_assert(status == BF_SUCCESS);

// 设置键（假设索引字段为PORT_THRESHOLD_index_fid）
bfrtTableKey->setValue(PORT_THRESHOLD_index_fid, 0); // 根据表结构调整

// 设置数据
bfrtTableData->setValue(PORT_THRESHOLD_value_fid, static_cast<uint64_t>(new_th));

// 正确调用
status = PORT_THRESHOLD_th->tableEntryMod(
    *session, dev_tgt, flags, *bfrtTableKey, *bfrtTableData
);
bf_sys_assert(status == BF_SUCCESS);
}


/* 维护线程 */
void maintenanceThread(uint32_t interval_sec) {
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
        
        printf("\n--- 定时清空寄存器 ---\n");
        clearPacketCountReg();
        clearPacketCountReg_01();
        clearOverloadReg();
        clearHashRegs();
       printf("--- 清空完成 [%lu] ---\n", 
             std::chrono::system_clock::now().time_since_epoch().count());
    }
}


bf_status_t digestCallback(const bf_rt_target_t &tgt,
                          const std::shared_ptr<BfRtSession> sess,
                          std::vector<std::unique_ptr<BfRtLearnData>> data_vec,
                          bf_rt_learn_msg_hdl *msg_hdl,
                          const void *cookie) {

        (void)tgt;
        (void)cookie;
        static std::unordered_set<std::string> seen;
        
    for (auto &data : data_vec) {
        uint64_t egress_port;  // 使用 uint64_t 接收 9-bit 字段
        uint64_t timestamp;   // 使用 uint64_t 接收 48-bit 字段
        
        auto status = data->getValue(Digest_Port_fid, &egress_port);
        bf_sys_assert(status == BF_SUCCESS);
        
        status = data->getValue(Digest_Time_fid, &timestamp);
        bf_sys_assert(status == BF_SUCCESS);
        
        egress_port &= 0x1FF;         // 0x1FF = 9位掩码[1](@ref)
        timestamp &= 0xFFFFFFFFFFFF;  // 保留低48位[1](@ref)

        // 转换为时分秒格式
        time_t event_time = static_cast<time_t>(timestamp/1000000000);
        struct tm *time_info = localtime(&event_time);
        char time_str[9];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", time_info);
        std::string key = std::to_string(egress_port) + "-" + time_str;
        if (seen.find(key) == seen.end())
        {printf("[INCAST事件] 端口: %-5" PRIu64 " 时间: %s\n", egress_port, time_str);
            seen.insert(key); }
    }
    
    return learn_obj->bfRtLearnNotifyAck(sess, msg_hdl);
}


/* 注册Digest回调 */
void setupDigest() {
    auto status = bfrtInfo->bfrtLearnFromNameGet("pipe.SwitchIngressDeparser.d", &learn_obj);
    bf_sys_assert(status == BF_SUCCESS);
    
    status = learn_obj->learnFieldIdGet("incast_egress_port", &Digest_Port_fid);
    bf_sys_assert(status == BF_SUCCESS);
    
    status = learn_obj->learnFieldIdGet("time", &Digest_Time_fid);
    bf_sys_assert(status == BF_SUCCESS);
    
    status = learn_obj->bfRtLearnCallbackRegister(session, dev_tgt, digestCallback, nullptr);
    bf_sys_assert(status == BF_SUCCESS);
    printf("[Digest] 注册成功\n");
}

    
    static bf_status_t add_port(bf_dev_id_t dev_id,
        bf_dev_port_t dev_port,
        bf_port_speed_t speed,
        bf_fec_type_t fec_type) {
        bf_status_t st;
        bool has_mac = false;
        st = bf_port_has_mac(dev_id, dev_port, &has_mac);
        bf_sys_assert(st == BF_SUCCESS);

        if (has_mac) {
        st = bf_pal_port_add(dev_id, dev_port, speed, fec_type);
        } else {
        st = bf_port_add(dev_id, dev_port, speed, fec_type);
        }
        return st;
        }

        static bf_status_t enable_port(bf_dev_id_t dev_id, bf_dev_port_t dev_port) {
        bf_status_t st;
        bool has_mac = false;
        st = bf_port_has_mac(dev_id, dev_port, &has_mac);
        bf_sys_assert(st == BF_SUCCESS);

        if (has_mac) {
        st = bf_pal_port_enable(dev_id, dev_port);
        } else {
        st = bf_port_enable(dev_id, dev_port, true);
        }
        return st;
        }


    static void setup_port(void) {
        bf_status_t st;
        // 把 OUT_PORT (132) 加到 Tofino 并启用

        st = add_port(dev_tgt.dev_id, OUT_PORT0, BF_SPEED_10G, BF_FEC_TYP_NONE);
        bf_sys_assert(st == BF_SUCCESS);
      
        st = enable_port(dev_tgt.dev_id, OUT_PORT0);
        bf_sys_assert(st == BF_SUCCESS);

        st = add_port(dev_tgt.dev_id, OUT_PORT1, BF_SPEED_10G, BF_FEC_TYP_NONE);
        bf_sys_assert(st == BF_SUCCESS);
      
        st = enable_port(dev_tgt.dev_id, OUT_PORT1);
        bf_sys_assert(st == BF_SUCCESS);

        st = add_port(dev_tgt.dev_id, OUT_PORT2, BF_SPEED_10G, BF_FEC_TYP_NONE);
        bf_sys_assert(st == BF_SUCCESS);
      
        st = enable_port(dev_tgt.dev_id, OUT_PORT2);
        bf_sys_assert(st == BF_SUCCESS);

        st = add_port(dev_tgt.dev_id, OUT_PORT3, BF_SPEED_10G, BF_FEC_TYP_NONE);
        bf_sys_assert(st == BF_SUCCESS);
      
        st = enable_port(dev_tgt.dev_id, OUT_PORT3);
        bf_sys_assert(st == BF_SUCCESS);
        printf( "端口设置成功\n");
      }
    
    /* CLI处理循环 */
    void commandLoop() {
        std::cout << "\nIncast检测控制台 (输入help查看命令)\n";
        //setupDigest();
        while (true) {
            std::string cmd;
            std::cout << ">> ";
            std::getline(std::cin, cmd);
           //setup_port();
            if (cmd == "exit") {
                running = false;
                break;
            }
            else if (cmd == "clear") {
                clearPacketCountReg();
                clearOverloadReg();
                clearPacketCountReg_01();
                std::cout << "立即清空所有寄存器完成\n";
            }
            else if (cmd.find("set-pkt-th") == 0) {
                double th=0;
                if (sscanf(cmd.c_str(), "set-pkt-th %lf", &th) == 1) 
                {
                    updatePacketThreshold(th);
                    
                    std::cout << "包计数阈值更新为: " << th << "Gbps\n";
                }
            }
            else if (cmd.find("set-incast-th") == 0) {
                uint16_t th;
                if (sscanf(cmd.c_str(), "set-port-th %hu", &th) == 1) {
                    updatePortThreshold(th);
                    std::cout << "Incast阈值更新为: " << th << "\n";
                }
            }
            else if (cmd == "help") {
                std::cout << "可用命令:\n"
                          << "  set-pkt-th <值>    设置包计数阈值 (0Gbit/s-10Gbit/s))\n"
                          << "  set-incast-th <值> 设置incast阈值 (1-65535)\n"
                          << "  clear              立即清空所有寄存器\n"
                          << "  exit               退出程序\n";
            }
            else {
                std::cout << "未知命令，输入help查看帮助\n";
            }
        }
    }

    /* 主函数 */
 
}
    }

    int main(int argc, char **argv) {
        parse_opts_and_switchd_init(argc, argv);
    
        bfrt::incast_detector::setUp();
        bfrt::incast_detector::tableSetUp();
        bfrt::incast_detector::setupDigest();
        bfrt::incast_detector::setup_port();
        
        // 启动维护线程（5秒间隔）
        std::thread(bfrt::incast_detector::maintenanceThread, 10).detach();
       
        bfrt::incast_detector::commandLoop();

        run_cli_or_cleanup();
        return 0;
    }

