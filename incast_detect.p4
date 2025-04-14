#include <core.p4>
#if __TARGET_TOFINO__ == 2
#include <t2na.p4>
#else
#include <tna.p4>
#endif

#include "common/headers.p4"
#include "common/util.p4"

#define MAX_PORTS 256
#define REGISTER_SIZE 65536

// 修复1：结构体分号补全
struct digest_t {
    bit<8>  incast_egress_port;
    bit<48> time;
};

struct metadata_t {
    bit<8>  incast_egress_port;
    bit<48> time;
    bit<16> index;
    bit<32> PACKET_THRESHOLD;
    bit<16> PORT_THRESHOLD;
    bit<32> data_num;
};

struct pair {
    bit<32>     first;
    bit<32>     second;
}

Hash<bit<10>> (HashAlgorithm_t.CRC32) CRC32_hash;
Hash<bit<10>> (HashAlgorithm_t.RANDOM) RANDOM_hash;
Hash<bit<10>> (HashAlgorithm_t.IDENTITY) IDENTITY_hash;

parser SwitchIngressParser(
        packet_in pkt,
        out header_t hdr,
        out metadata_t ig_md,
        out ingress_intrinsic_metadata_t ig_intr_md) {
    TofinoIngressParser() tofino_parser;

    state start {
        tofino_parser.apply(pkt, ig_intr_md);
        transition parse_ethernet;
    }

    state parse_ethernet {
        pkt.extract(hdr.ethernet);
        transition parse_ipv4;
    }

    state parse_ipv4 {
        pkt.extract(hdr.ipv4);
        transition accept;
    }
}

control SwitchIngressDeparser(
        packet_out pkt,
        inout header_t hdr,
        in metadata_t ig_md,
        in ingress_intrinsic_metadata_for_deparser_t ig_dprsr_md) {

    Digest<digest_t>() d;
    apply {
        if (ig_dprsr_md.digest_type == 1) {
            d.pack({
                ig_md.incast_egress_port, 
                ig_md.time
            });
        }
        pkt.emit(hdr);
    }
}

control SwitchIngress(
        inout header_t hdr,
        inout metadata_t ig_md,
        in ingress_intrinsic_metadata_t ig_intr_md,
        in ingress_intrinsic_metadata_from_parser_t ig_prsr_md,
        inout ingress_intrinsic_metadata_for_deparser_t ig_dprsr_md,
        inout ingress_intrinsic_metadata_for_tm_t ig_tm_md) {
            
    //PACKET_THRESHOLD 
    Register<bit<32>,bit<16>>(1,531250000) PACKET_THRESHOLD;  //时间窗口:10s 链路带宽10Gbps 3条发送端一条接受端 34Gbit>>6
    RegisterAction<bit<32>,bit<16>, bit<32>>(PACKET_THRESHOLD) PACKET_THRESHOLD_action = {
        void apply(inout bit<32> value, out bit<32> read_value) {
            read_value = value;
        }
    };
    //PORT_THRESHOLD 
    Register<bit<16>,bit<16>>(1,2) PORT_THRESHOLD;
    RegisterAction<bit<16>,bit<16>,bit<16>>(PORT_THRESHOLD) PORT_THRESHOLD_action = {
        void apply(inout bit<16> value, out bit<16> read_value) {
             read_value = value;
        }
    };
    //HASH1
    Register<bit<8>,bit<10>>(1024,0) HASH1;
    RegisterAction<bit<8>,bit<10>, bit<8>>(HASH1) HASH1_action = {
        void apply(inout bit<8> value, out bit<8> read_value) {
            value=value+1;
            read_value=value;
        }
    };
    //HASH2
    Register<bit<8>,bit<10>>(1024,0) HASH2;
    RegisterAction<bit<8>,bit<10>, bit<8>>(HASH2) HASH2_action = {
        void apply(inout bit<8> value,  out bit<8> read_value) {
            value=value+1;
           read_value=value;
        }
    };
    //HASH3
    Register<bit<8>,bit<10>>(1024,0) HASH3;
    RegisterAction<bit<8>,bit<10>, bit<8>>(HASH3) HASH3_action = {
        void apply(inout bit<8> value, out  bit<8> read_value) {
            value=value+1;
           read_value=value;
        }
    };
    //Pkt_Register
    Register<pair, bit<16>>(REGISTER_SIZE, {0, 0}) Pkt_Register;
   
    RegisterAction<pair,bit<16>, bool>(Pkt_Register) Pkt_Register_action = {
        void apply(inout pair value, out bool flag) {
            value.second = value.second + ig_md.data_num;
            bool flag2=value.second>ig_md.PACKET_THRESHOLD;
            flag=false;
            if(value.first==0&&flag2) 
            {
            flag=true;
            value.first=1;
            }
        }
    };
    //Port_Register
    Register<bit<16> ,bit<8>>(MAX_PORTS, 0) Port_Register;
    RegisterAction<bit<16>, bit<8>, bool>(Port_Register) Port_Register_action = {
        void apply(inout bit<16> value, out bool flag) {
            flag=false;
            if(value+1 >= ig_md.PORT_THRESHOLD)
            flag=true;
            value = value + 1;
        }
    };
    
    action set_egress_port(PortId_t egress_port) {
        ig_tm_md.ucast_egress_port = egress_port;
    }

    table port_mapping {
        key = { ig_intr_md.ingress_port : exact; }
        actions = { set_egress_port; }
        size = 4;
        const entries = {
            (153) : set_egress_port(152);
            (154) : set_egress_port(152);
            (155) : set_egress_port(152);
            (152) : set_egress_port(152);
        }
    }

    apply {
        // 基础转发
        port_mapping.apply();


        // 生成复合索引（入口端口+出口端口）
        ig_md.index[7:0]=ig_intr_md.ingress_port[7:0];
        ig_md.index[15:8]=ig_tm_md.ucast_egress_port[7:0];

        //求该数据包的数据量
        ig_md.data_num=0;
        ig_md.data_num[15:0]=hdr.ipv4.total_len[15:0];
        ig_md.data_num=ig_md.data_num+26;
        ig_md.data_num=ig_md.data_num>>3;

        //读取阈值
        ig_md.PORT_THRESHOLD=PORT_THRESHOLD_action.execute(0);
        ig_md.PACKET_THRESHOLD=PACKET_THRESHOLD_action.execute(0);

        bool local_flag_00=false;
        bool local_flag_01=false;
        

        // 第一层检测
        local_flag_00=Pkt_Register_action.execute(ig_md.index);

        bit<8> egress_port = ig_tm_md.ucast_egress_port[7:0];

        // 第二层检测
        if (local_flag_00) {
            local_flag_01=Port_Register_action.execute(egress_port);
        }

        bool flag1=false;
        bool flag2=false;
        bool flag3=false;
        bool flag_notice=false;
        //Bloom Filter 过滤 防止同一事件多次上报
        if(local_flag_01){
        bit<48> time=ig_intr_md.ingress_mac_tstamp;
        bit<10> index1 = CRC32_hash.get({time,ig_tm_md.ucast_egress_port});
        bit<10> index2 = RANDOM_hash.get({time,ig_tm_md.ucast_egress_port});
        bit<10> index3 = IDENTITY_hash.get({time,ig_tm_md.ucast_egress_port});

        bit<8>  num1=HASH1_action.execute(index1);
        bit<8>  num2=HASH2_action.execute(index2);
        bit<8>  num3=HASH3_action.execute(index3);

        if(num1==1)
        flag1=true;

        if(num2==1)
        flag2=true;

        if(num3==1)
        flag3=true;

        bool flag4=flag1&&flag2;
        bool flag5=flag4&&flag3;

        if(flag5)
        flag_notice=true;
        

        if (flag_notice) {
            //生成digest信息
            ig_dprsr_md.digest_type = 1;
            ig_md.incast_egress_port = ig_tm_md.ucast_egress_port[7:0];
            ig_md.time = ig_intr_md.ingress_mac_tstamp;
        }
        }
        //跳过Egress
        ig_tm_md.bypass_egress = 1w1;
    }
}

Pipeline(SwitchIngressParser(),
         SwitchIngress(),
         SwitchIngressDeparser(),
         EmptyEgressParser(),
         EmptyEgress(),
         EmptyEgressDeparser()) pipe;

Switch(pipe) main;