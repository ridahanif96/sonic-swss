
#pragma once

#include <set>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <bitset>

#include "dbconnector.h"
#include "netmsg.h"
#include "orch.h"
#include "producerstatetable.h"
#include <stp_ipc.h>
#include <stddef.h>
#include <algorithm>

#define STPMGRD_SOCK_NAME "/var/run/stpmgrd.sock"

#define TAGGED_MODE      1
#define UNTAGGED_MODE    0
#define INVALID_MODE    -1

#define MAX_VLANS   4096
// Maximum number of instances supported
#define L2_INSTANCE_MAX             MAX_VLANS
#define STP_DEFAULT_MAX_INSTANCES   255
#define INVALID_INSTANCE            -1 


#define GET_FIRST_FREE_INST_ID(_idx) \
    while (_idx < (int)l2InstPool.size() && l2InstPool.test(_idx)) ++_idx; \
    l2InstPool.set(_idx)

#define FREE_INST_ID(_idx) l2InstPool.reset(_idx)

#define FREE_ALL_INST_ID() l2InstPool.reset()

#define IS_INST_ID_AVAILABLE() (l2InstPool.count() < max_stp_instances) ? true : false


namespace swss {

class StpMgr : public Orch
{
public:
    StpMgr(DBConnector *cfgDb, DBConnector *appDb, DBConnector *stateDb,
            const vector<TableConnector> &tables);

    using Orch::doTask;
	void ipcInitStpd();
    int sendMsgStpd(STP_MSG_TYPE msgType, uint32_t msgLen, void *data);
    MacAddress macAddress;
    bool isPortInitDone(DBConnector *app_db);
    uint16_t getStpMaxInstances(void);

private:
    Table m_cfgStpGlobalTable;
    Table m_cfgStpVlanTable;
    Table m_cfgStpVlanPortTable;
    Table m_cfgStpPortTable;
    Table m_cfgLagMemberTable;
    Table m_cfgVlanMemberTable;

    //new tables 
    Table m_cfgStpMstTable;
    Table m_cfgStpMstInstTable;
    Table m_cfgStpMstPortTable;

    Table m_appStpMstInstTable;
    Table m_appStpMstPortTable;
    Table m_appStpPortTable;
    Table m_appStpInstPortFlushTable;

    Table m_stateVlanTable;
    Table m_stateVlanMemberTable;
    Table m_stateLagTable;
    Table m_stateStpTable;

    bitset<L2_INSTANCE_MAX> l2InstPool;
	int stpd_fd;
    enum L2_PROTO_MODE l2ProtoEnabled;
    int m_vlanInstMap[MAX_VLANS];
    bool portCfgDone;
    uint16_t max_stp_instances;
    map<string, int> m_lagMap;

    bitset<L2_INSTANCE_MAX> l2InstPool; // This may need extension for MSTP
    int max_mst_instances;
    map<string, int> m_stpMstInstanceMap;
    map<string, int> m_stpMstPortMap;

    bool stpGlobalTask;
    bool stpVlanTask;
    bool stpVlanPortTask;
    bool stpPortTask;

    void doTask(Consumer &consumer);
    void doStpGlobalTask(Consumer &consumer);
    void doStpVlanTask(Consumer &consumer);
    void doStpVlanPortTask(Consumer &consumer);
    void doStpPortTask(Consumer &consumer);
    void doVlanMemUpdateTask(Consumer &consumer);
    void doLagMemUpdateTask(Consumer &consumer);
    
    void doStpMstpTask(Consumer &consumer);
    void doMstpInstanceTask(Consumer &consumer);
    void doMstpPortTask(Consumer &consumer);


    bool isVlanStateOk(const string &alias);
    bool isLagStateOk(const string &alias);
    bool isStpPortEmpty();
    bool isStpEnabled(const string &intf_name);
    int getAllVlanMem(const string &vlanKey, vector<PORT_ATTR>&port_list);
    int getAllPortVlan(const string &intfKey, vector<VLAN_ATTR>&vlan_list);
    int8_t getVlanMemMode(const string &key);
    int allocL2Instance(uint32_t vlan_id);
    void deallocL2Instance(uint32_t vlan_id);
    bool isLagEmpty(const string &key);
    void processStpPortAttr(const string op, vector<FieldValueTuple>&tupEntry, const string intfName);
    void processStpVlanPortAttr(const string op, uint32_t vlan_id, const string intfName,
                    vector<FieldValueTuple>&tupEntry);
};

}