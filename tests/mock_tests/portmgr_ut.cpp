#include "portmgr.h"
#include "gtest/gtest.h"
#include "mock_table.h"
#include "redisutility.h"

extern std::vector<std::string> mockCallArgs;

namespace portmgr_ut
{
    using namespace swss;
    using namespace std;

    struct PortMgrTest : public ::testing::Test
    {
        shared_ptr<swss::DBConnector> m_app_db;
        shared_ptr<swss::DBConnector> m_config_db;
        shared_ptr<swss::DBConnector> m_state_db;
        shared_ptr<PortMgr> m_portMgr;
        PortMgrTest()
        {
            m_app_db = make_shared<swss::DBConnector>(
                "APPL_DB", 0);
            m_config_db = make_shared<swss::DBConnector>(
                "CONFIG_DB", 0);
            m_state_db = make_shared<swss::DBConnector>(
                "STATE_DB", 0);
        }

        virtual void SetUp() override
        {
            ::testing_db::reset();
            vector<string> cfg_port_tables = {
                CFG_PORT_TABLE_NAME,
            };
            m_portMgr.reset(new PortMgr(m_config_db.get(), m_app_db.get(), m_state_db.get(), cfg_port_tables));
        }
    };

    TEST_F(PortMgrTest, DoTask)
    {
        Table state_port_table(m_state_db.get(), STATE_PORT_TABLE_NAME);
        Table app_port_table(m_app_db.get(), APP_PORT_TABLE_NAME);
        Table cfg_port_table(m_config_db.get(), CFG_PORT_TABLE_NAME);

        // Port is not ready, verify that doTask does not handle port configuration
        
        cfg_port_table.set("Ethernet0", {
            {"speed", "100000"},
            {"index", "1"}
        });
        mockCallArgs.clear();
        m_portMgr->addExistingData(&cfg_port_table);
        m_portMgr->doTask();
        ASSERT_TRUE(mockCallArgs.empty());
        std::vector<FieldValueTuple> values;
        app_port_table.get("Ethernet0", values);
        auto value_opt = swss::fvsGetValue(values, "mtu", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ(DEFAULT_MTU_STR, value_opt.get());
        value_opt = swss::fvsGetValue(values, "admin_status", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ(DEFAULT_ADMIN_STATUS_STR, value_opt.get());
        value_opt = swss::fvsGetValue(values, "speed", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ("100000", value_opt.get());
        value_opt = swss::fvsGetValue(values, "index", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ("1", value_opt.get());

        // Set port state to ok, verify that doTask handle port configuration
        state_port_table.set("Ethernet0", {
            {"state", "ok"}
        });
        m_portMgr->doTask();
        ASSERT_EQ(size_t(2), mockCallArgs.size());
        ASSERT_EQ("/sbin/ip link set dev \"Ethernet0\" mtu \"9100\"", mockCallArgs[0]);
        ASSERT_EQ("/sbin/ip link set dev \"Ethernet0\" down", mockCallArgs[1]);
        
        // Set port admin_status, verify that it could override the default value
        cfg_port_table.set("Ethernet0", {
            {"admin_status", "up"}
        });
        m_portMgr->addExistingData(&cfg_port_table);
        m_portMgr->doTask();
        app_port_table.get("Ethernet0", values);
        value_opt = swss::fvsGetValue(values, "admin_status", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ("up", value_opt.get());
    }

    TEST_F(PortMgrTest, ConfigureDuringRetry)
    {
        Table state_port_table(m_state_db.get(), STATE_PORT_TABLE_NAME);
        Table app_port_table(m_app_db.get(), APP_PORT_TABLE_NAME);
        Table cfg_port_table(m_config_db.get(), CFG_PORT_TABLE_NAME);

        cfg_port_table.set("Ethernet0", {
            {"speed", "100000"},
            {"index", "1"}
        });

        mockCallArgs.clear();
        m_portMgr->addExistingData(&cfg_port_table);
        m_portMgr->doTask();
        ASSERT_TRUE(mockCallArgs.empty());

        cfg_port_table.set("Ethernet0", {
            {"speed", "50000"},
            {"index", "1"},
            {"mtu", "1518"},
            {"admin_status", "up"}
        });

        m_portMgr->addExistingData(&cfg_port_table);
        m_portMgr->doTask();
        ASSERT_TRUE(mockCallArgs.empty());

        state_port_table.set("Ethernet0", {
            {"state", "ok"}
        });
        m_portMgr->doTask();
        ASSERT_EQ(size_t(2), mockCallArgs.size());
        ASSERT_EQ("/sbin/ip link set dev \"Ethernet0\" mtu \"1518\"", mockCallArgs[0]);
        ASSERT_EQ("/sbin/ip link set dev \"Ethernet0\" up", mockCallArgs[1]);
    }

        TEST_F(PortMgrTest, ConfigurePortDHCPRateLimit)
    {
        Table state_port_table(m_state_db.get(), STATE_PORT_TABLE_NAME);
        Table app_port_table(m_app_db.get(), APP_PORT_TABLE_NAME);
        Table cfg_port_table(m_config_db.get(), CFG_PORT_TABLE_NAME);

        // Step 1: Configure port with dhcp_rate_limit, but port not ready yet
        cfg_port_table.set("Ethernet0", {
            {"speed", "100000"},
            {"index", "1"},
            {"dhcp_rate_limit", "100"}
        });

        mockCallArgs.clear();
        m_portMgr->addExistingData(&cfg_port_table);
        m_portMgr->doTask();

        // Should not invoke any system calls yet
        ASSERT_TRUE(mockCallArgs.empty());

        // APP_DB should NOT contain dhcp_rate_limit
        std::vector<FieldValueTuple> values;
        app_port_table.get("Ethernet0", values);
        auto value_opt = swss::fvsGetValue(values, "dhcp_rate_limit", true);
        ASSERT_FALSE(value_opt);

        // Step 2: Mark port as ready
        state_port_table.set("Ethernet0", {
            {"state", "ok"}
        });
        m_portMgr->doTask();

        // Expect tc command to enforce DHCP rate limit
        ASSERT_EQ(size_t(2), mockCallArgs.size());
        std::string expected_prefix = "/sbin/tc qdisc add dev \"Ethernet0\"";
        ASSERT_TRUE(mockCallArgs[0].find(expected_prefix) == 0)
            << "Unexpected tc command: " << mockCallArgs[0];

        // Step 3: Set dhcp_rate_limit to 0 → should issue delete command
        cfg_port_table.set("Ethernet0", {
            {"dhcp_rate_limit", "0"}
        });
        m_portMgr->addExistingData(&cfg_port_table);
        mockCallArgs.clear();
        m_portMgr->doTask();

        ASSERT_EQ(size_t(1), mockCallArgs.size());
        ASSERT_EQ("/sbin/tc qdisc del dev \"Ethernet0\" handle ffff: ingress", mockCallArgs[0]);
    }


    TEST_F(PortMgrTest, ConfigurePortPTDefaultTimestampTemplate)
    {
        Table state_port_table(m_state_db.get(), STATE_PORT_TABLE_NAME);
        Table app_port_table(m_app_db.get(), APP_PORT_TABLE_NAME);
        Table cfg_port_table(m_config_db.get(), CFG_PORT_TABLE_NAME);

        // Port is not ready, verify that doTask does not handle port configuration

        cfg_port_table.set("Ethernet0", {
            {"speed", "100000"},
            {"index", "1"},
            {"pt_interface_id", "129"}
        });
        mockCallArgs.clear();
        m_portMgr->addExistingData(&cfg_port_table);
        m_portMgr->doTask();
        ASSERT_TRUE(mockCallArgs.empty());
        std::vector<FieldValueTuple> values;
        app_port_table.get("Ethernet0", values);
        auto value_opt = swss::fvsGetValue(values, "mtu", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ(DEFAULT_MTU_STR, value_opt.get());
        value_opt = swss::fvsGetValue(values, "admin_status", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ(DEFAULT_ADMIN_STATUS_STR, value_opt.get());
        value_opt = swss::fvsGetValue(values, "speed", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ("100000", value_opt.get());
        value_opt = swss::fvsGetValue(values, "index", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ("1", value_opt.get());
        value_opt = swss::fvsGetValue(values, "pt_interface_id", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ("129", value_opt.get());
        value_opt = swss::fvsGetValue(values, "pt_timestamp_template", true);
        ASSERT_FALSE(value_opt);
    }

    TEST_F(PortMgrTest, ConfigurePortPTNonDefaultTimestampTemplate)
    {
        Table state_port_table(m_state_db.get(), STATE_PORT_TABLE_NAME);
        Table app_port_table(m_app_db.get(), APP_PORT_TABLE_NAME);
        Table cfg_port_table(m_config_db.get(), CFG_PORT_TABLE_NAME);

        // Port is not ready, verify that doTask does not handle port configuration

        cfg_port_table.set("Ethernet0", {
            {"speed", "100000"},
            {"index", "1"},
            {"pt_interface_id", "129"},
            {"pt_timestamp_template", "template2"}
        });
        mockCallArgs.clear();
        m_portMgr->addExistingData(&cfg_port_table);
        m_portMgr->doTask();
        ASSERT_TRUE(mockCallArgs.empty());
        std::vector<FieldValueTuple> values;
        app_port_table.get("Ethernet0", values);
        auto value_opt = swss::fvsGetValue(values, "mtu", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ(DEFAULT_MTU_STR, value_opt.get());
        value_opt = swss::fvsGetValue(values, "admin_status", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ(DEFAULT_ADMIN_STATUS_STR, value_opt.get());
        value_opt = swss::fvsGetValue(values, "speed", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ("100000", value_opt.get());
        value_opt = swss::fvsGetValue(values, "index", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ("1", value_opt.get());
        value_opt = swss::fvsGetValue(values, "pt_interface_id", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ("129", value_opt.get());
        value_opt = swss::fvsGetValue(values, "pt_timestamp_template", true);
        ASSERT_TRUE(value_opt);
        ASSERT_EQ("template2", value_opt.get());
    }
}