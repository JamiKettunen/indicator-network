/*
 * Copyright (C) 2013 Canonical, Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Pete Woods <pete.woods@canonical.com>
 */

#include <libqtdbustest/DBusTestRunner.h>
#include <libqtdbustest/QProcessDBusService.h>
#include <libqtdbusmock/DBusMock.h>

#include <menuharness/MatchUtils.h>
#include <menuharness/MenuMatcher.h>

#include <NetworkManager.h>

#include <QDebug>
#include <QTestEventLoop>
#include <QSignalSpy>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

void PrintTo(const QVariant& variant, std::ostream* os) {
        *os << "QVariant(" << variant.toString().toStdString() << ")";
}


#define WAIT_FOR_SIGNALS(signalSpy, signalsExpected)\
{\
    while (signalSpy.size() < signalsExpected)\
    {\
        ASSERT_TRUE(signalSpy.wait());\
    }\
    ASSERT_EQ(signalsExpected, signalSpy.size());\
}

#define CHECK_METHOD_CALL(signalSpy, signalIndex, methodName, ...)\
{\
    QVariantList const& call(signalSpy.at(signalIndex));\
    EXPECT_EQ(methodName, call.at(0));\
    auto arguments = vector<pair<int, QVariant>>{__VA_ARGS__};\
    if (!arguments.empty())\
    {\
        QVariantList const& args(call.at(1).toList());\
        ASSERT_LE(arguments.back().first + 1, args.size());\
        for (auto const& argument : arguments)\
        {\
            EXPECT_EQ(argument.second, args.at(argument.first));\
        }\
    }\
}

using namespace std;
using namespace testing;
using namespace QtDBusTest;
using namespace QtDBusMock;

namespace mh = menuharness;

namespace
{
enum class Secure
{
    secure,
    insecure
};

enum class ApMode
{
    infra,
    adhoc
};

enum class ConnectionStatus
{
    connected,
    disconnected
};

class TestIndicatorNetworkService : public Test
{
protected:
    TestIndicatorNetworkService() :
            dbusMock(dbusTestRunner)
    {
    }

    void SetUp() override
    {
        if (qEnvironmentVariableIsSet("TEST_WITH_BUSTLE"))
        {
            const TestInfo* const test_info =
                    UnitTest::GetInstance()->current_test_info();

            QDir::temp().mkpath("indicator-network-tests");
            QDir testDir(QDir::temp().filePath("indicator-network-tests"));

            dbusTestRunner.registerService(
                    DBusServicePtr(
                            new QProcessDBusService(
                                    "", QDBusConnection::SessionBus,
                                    "/usr/bin/bustle-pcap",
                                    QStringList{"-e", testDir.filePath(QString("%1-%2").arg(test_info->name(), "session.log"))})));
            dbusTestRunner.registerService(
                    DBusServicePtr(
                            new QProcessDBusService(
                                    "", QDBusConnection::SystemBus,
                                    "/usr/bin/bustle-pcap",
                                    QStringList{"-y", testDir.filePath(QString("%1-%2").arg(test_info->name(), "system.log"))})));
        }

        dbusMock.registerNetworkManager();
        dbusMock.registerNotificationDaemon();
        // By default the ofono mock starts with one modem
        dbusMock.registerOfono();
        dbusMock.registerURfkill();

        dbusTestRunner.startServices();

        // Identify the test when looking at Bustle logs
        QDBusConnection systemConnection = dbusTestRunner.systemConnection();
        systemConnection.registerService("org.TestIndicatorNetworkService");
        QDBusConnection sessionConnection = dbusTestRunner.sessionConnection();
        sessionConnection.registerService("org.TestIndicatorNetworkService");
    }

    static mh::MenuMatcher::Parameters phoneParameters()
    {
        return mh::MenuMatcher::Parameters(
                "com.canonical.indicator.network",
                { { "indicator", "/com/canonical/indicator/network" } },
                "/com/canonical/indicator/network/phone");
    }

    mh::MenuMatcher::Parameters unlockSimParameters(std::string const& busName, int exportId)
    {
        return mh::MenuMatcher::Parameters(
                busName,
                { { "notifications", "/com/canonical/indicator/network/unlocksim" + to_string(exportId) } },
                "/com/canonical/indicator/network/unlocksim" + to_string(exportId));
    }

    void startIndicator()
    {
        try
        {
            indicator.reset(
                    new QProcessDBusService("com.canonical.indicator.network",
                                            QDBusConnection::SessionBus,
                                            NETWORK_SERVICE_BIN,
                                            QStringList()));
            indicator->start(dbusTestRunner.sessionConnection());
        }
        catch (exception const& e)
        {
            cout << "startIndicator(): " << e.what() << endl;
            throw;
        }
    }

    QString createWiFiDevice(int state, const QString& id = "0")
    {
        auto& networkManager(dbusMock.networkManagerInterface());
        auto reply = networkManager.AddWiFiDevice(id, "eth1", state);
        reply.waitForFinished();
        return reply;
    }

    static QString randomMac()
    {
        int high = 254;
        int low = 1;
        QString hardwareAddress;
        bool first = true;

        for (unsigned int i = 0; i < 6; ++i)
        {
            if (!first)
            {
                hardwareAddress.append(":");
            }
            int r = qrand() % ((high + 1) - low) + low;
            hardwareAddress.append(QString("%1").arg(r, 2, 16, QChar('0')));
            first = false;
        }

        return hardwareAddress;
    }

    void enableWiFi()
    {
        auto& urfkillInterface = dbusMock.urfkillInterface();
        urfkillInterface.Block(1, false).waitForFinished();
    }

    void disableWiFi()
    {
        auto& urfkillInterface = dbusMock.urfkillInterface();
        urfkillInterface.Block(1, true).waitForFinished();
    }

    QString createAccessPoint(const QString& id, const QString& ssid, const QString& device, int strength = 100,
                              Secure secure = Secure::secure, ApMode apMode = ApMode::infra)
    {

        auto& networkManager(dbusMock.networkManagerInterface());
        auto reply = networkManager.AddAccessPoint(
                            device, id, ssid,
                            randomMac(),
                            apMode == ApMode::adhoc ? NM_802_11_MODE_ADHOC : NM_802_11_MODE_INFRA,
                            0, 0, strength,
                            secure == Secure::secure ? NM_802_11_AP_SEC_KEY_MGMT_PSK : NM_802_11_AP_SEC_NONE);
        reply.waitForFinished();
        return reply;
    }

    void removeAccessPoint(const QString& device, const QString& ap)
    {
        auto& nm = dbusMock.networkManagerInterface();
        nm.RemoveAccessPoint(device, ap).waitForFinished();
    }

    QString createAccessPointConnection(const QString& id, const QString& ssid, const QString& device)
    {
        auto& networkManager(dbusMock.networkManagerInterface());
        auto reply = networkManager.AddWiFiConnection(device, id, ssid,
                                                      "");
        reply.waitForFinished();
        return reply;
    }

    void removeWifiConnection(const QString& device, const QString& connection)
    {
        auto& nm = dbusMock.networkManagerInterface();
        nm.RemoveWifiConnection(device, connection).waitForFinished();
    }

    QString createActiveConnection(const QString& id, const QString& device, const QString& connection, const QString& ap)
    {
        auto& nm = dbusMock.networkManagerInterface();
        auto reply = nm.AddActiveConnection(QStringList() << device,
                               connection,
                               ap,
                               id,
                               NM_ACTIVE_CONNECTION_STATE_ACTIVATED);
        reply.waitForFinished();
        return reply;
    }

    void removeActiveConnection(const QString& device, const QString& active_connection)
    {
        auto& nm = dbusMock.networkManagerInterface();
        nm.RemoveActiveConnection(device, active_connection).waitForFinished();
    }

    void setGlobalConnectedState(int state)
    {
        auto& nm = dbusMock.networkManagerInterface();
        nm.SetGlobalConnectionState(state).waitForFinished();
    }

    void setNmProperty(const QString& path, const QString& iface, const QString& name, const QVariant& value)
    {
        auto& nm = dbusMock.networkManagerInterface();
        nm.SetProperty(path, iface, name, QDBusVariant(value)).waitForFinished();
    }

    QString createModem(const QString& id)
    {
        auto& ofono(dbusMock.ofonoInterface());
        QVariantMap modemProperties {{ "Powered", false } };
        return ofono.AddModem(id, modemProperties);
    }

    void setModemProperty(const QString& path, const QString& propertyName, const QVariant& value)
    {
        auto& ofono(dbusMock.ofonoModemInterface(path));
        ofono.SetProperty(propertyName, QDBusVariant(value)).waitForFinished();
    }

    void setSimManagerProperty(const QString& path, const QString& propertyName, const QVariant& value)
    {
        auto& ofono(dbusMock.ofonoSimManagerInterface(path));
        ofono.SetProperty(propertyName, QDBusVariant(value)).waitForFinished();
    }

    void setConnectionManagerProperty(const QString& path, const QString& propertyName, const QVariant& value)
    {
        auto& ofono(dbusMock.ofonoConnectionManagerInterface(path));
        ofono.SetProperty(propertyName, QDBusVariant(value)).waitForFinished();
    }

    void setNetworkRegistrationProperty(const QString& path, const QString& propertyName, const QVariant& value)
    {
        auto& ofono(dbusMock.ofonoNetworkRegistrationInterface(path));
        ofono.SetProperty(propertyName, QDBusVariant(value)).waitForFinished();
    }

    OrgFreedesktopDBusMockInterface* notificationsMockInterface()
    {
        return &dbusMock.mockInterface("org.freedesktop.Notifications",
                                       "/org/freedesktop/Notifications",
                                       "org.freedesktop.Notifications",
                                       QDBusConnection::SessionBus);
    }

    OrgFreedesktopDBusMockInterface* modemMockInterface(const QString& path)
    {
        return &dbusMock.mockInterface("org.ofono",
                                       path,
                                       "",
                                       QDBusConnection::SystemBus);
    }

    bool qDBusArgumentToMap(QVariant const& variant, QVariantMap& map)
    {
        if (variant.canConvert<QDBusArgument>())
        {
            QDBusArgument value(variant.value<QDBusArgument>());
            if (value.currentType() == QDBusArgument::MapType)
            {
                value >> map;
                return true;
            }
        }
        return false;
    }

    QString firstModem()
    {
        return "/ril_0";
    }

    static mh::MenuItemMatcher flightModeSwitch(bool toggled = false)
    {
        return mh::MenuItemMatcher::checkbox()
            .label("Flight Mode")
            .action("indicator.airplane.enabled")
            .toggled(toggled);
    }

    static mh::MenuItemMatcher accessPoint(const string& ssid, Secure secure,
                ApMode apMode, ConnectionStatus connectionStatus, int strength = 100)
    {
        return mh::MenuItemMatcher::checkbox()
            .label(ssid)
            .widget("unity.widgets.systemsettings.tablet.accesspoint")
            .toggled(connectionStatus == ConnectionStatus::connected)
            .pass_through_attribute(
                "x-canonical-wifi-ap-strength-action",
                shared_ptr<GVariant>(g_variant_new_byte(strength), &mh::gvariant_deleter))
            .boolean_attribute("x-canonical-wifi-ap-is-secure", secure == Secure::secure)
            .boolean_attribute("x-canonical-wifi-ap-is-adhoc", apMode == ApMode::adhoc);
    }

    static mh::MenuItemMatcher wifiEnableSwitch(bool toggled = true)
    {
        return mh::MenuItemMatcher::checkbox()
            .label("Wi-Fi")
            .action("indicator.wifi.enable") // This action is accessed by system-settings-ui, do not change it
            .toggled(toggled);
    }

    static mh::MenuItemMatcher wifiSettings()
    {
        return mh::MenuItemMatcher()
            .label("Wi-Fi settings…")
            .action("indicator.wifi.settings");
    }

    static mh::MenuItemMatcher modemInfo(const string& simIdentifier, const string& label, const string& statusIcon, bool locked = false)
    {
        return mh::MenuItemMatcher()
            .widget("com.canonical.indicator.network.modeminfoitem")
            .pass_through_string_attribute("x-canonical-modem-sim-identifier-label-action", simIdentifier)
            .pass_through_string_attribute("x-canonical-modem-connectivity-icon-action", "")
            .pass_through_string_attribute("x-canonical-modem-status-label-action", label)
            .pass_through_string_attribute("x-canonical-modem-status-icon-action", statusIcon)
            .pass_through_boolean_attribute("x-canonical-modem-roaming-action", false)
            .pass_through_boolean_attribute("x-canonical-modem-locked-action", locked);
    }

    static mh::MenuItemMatcher cellularSettings()
    {
        return mh::MenuItemMatcher()
            .label("Cellular settings…")
            .action("indicator.cellular.settings");
    }

    DBusTestRunner dbusTestRunner;

    DBusMock dbusMock;

    DBusServicePtr indicator;
};

TEST_F(TestIndicatorNetworkService, BasicMenuContents)
{
    setGlobalConnectedState(NM_STATE_DISCONNECTED);
    ASSERT_NO_THROW(startIndicator());

    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .action("indicator.phone.network-status")
            .state_icons({"gsm-3g-full", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::all)
            .submenu()
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .section()
                .item(modemInfo("", "fake.tel", "gsm-3g-full"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch())
            .item(wifiSettings())
        ).match());
}

TEST_F(TestIndicatorNetworkService, OneDisconnectedAccessPointAtStartup)
{
    setGlobalConnectedState(NM_STATE_DISCONNECTED);
    auto device = createWiFiDevice(NM_DEVICE_STATE_DISCONNECTED);
    auto ap = createAccessPoint("0", "the ssid", device);
    auto connection = createAccessPointConnection("0", "the ssid", device);

    ASSERT_NO_THROW(startIndicator());

    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "nm-no-connection"})
            .submenu()
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()) // <-- modems are under here
            .item(wifiEnableSwitch())
            .item(mh::MenuItemMatcher()
                .section()
                .item(accessPoint("the ssid",
                      Secure::secure,
                      ApMode::infra,
                      ConnectionStatus::disconnected)
                )
            )
            .item(wifiSettings())
        ).match());
}

TEST_F(TestIndicatorNetworkService, OneConnectedAccessPointAtStartup)
{
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);
    auto device = createWiFiDevice(NM_DEVICE_STATE_ACTIVATED);
    auto ap = createAccessPoint("0", "the ssid", device);
    auto connection = createAccessPointConnection("0", "the ssid", device);
    createActiveConnection("0", device, connection, ap);

    ASSERT_NO_THROW(startIndicator());

    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "nm-signal-100-secure"})
            .submenu()
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()) // <-- modems are under here
            .item(wifiEnableSwitch())
            .item(mh::MenuItemMatcher()
                .section()
                .item(accessPoint("the ssid",
                      Secure::secure,
                      ApMode::infra,
                      ConnectionStatus::connected)
                )
            )
            .item(wifiSettings())
        ).match());
}

TEST_F(TestIndicatorNetworkService, AddOneDisconnectedAccessPointAfterStartup)
{
    setGlobalConnectedState(NM_STATE_DISCONNECTED);
    auto device = createWiFiDevice(NM_DEVICE_STATE_DISCONNECTED);

    ASSERT_NO_THROW(startIndicator());
    auto ap = createAccessPoint("0", "the ssid", device);
    auto connection = createAccessPointConnection("0", "the ssid", device);

    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .submenu()
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher())
            .item(wifiEnableSwitch())
            .item(mh::MenuItemMatcher()
                .section()
                .item(accessPoint("the ssid",
                      Secure::secure,
                      ApMode::infra,
                      ConnectionStatus::disconnected)
                )
            )
            .item(wifiSettings())
        ).match());
}

TEST_F(TestIndicatorNetworkService, AddOneConnectedAccessPointAfterStartup)
{
    setGlobalConnectedState(NM_STATE_DISCONNECTED);
    auto device = createWiFiDevice(NM_DEVICE_STATE_DISCONNECTED);

    ASSERT_NO_THROW(startIndicator());

    auto ap = createAccessPoint("0", "the ssid", device);
    auto connection = createAccessPointConnection("0", "the ssid", device);
    createActiveConnection("0", device, connection, ap);
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .submenu()
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher())
            .item(wifiEnableSwitch())
            .item(mh::MenuItemMatcher()
                .section()
                .item(accessPoint("the ssid",
                      Secure::secure,
                      ApMode::infra,
                      ConnectionStatus::connected)
                )
            )
            .item(wifiSettings())
        ).match());
}

TEST_F(TestIndicatorNetworkService, SecondModem)
{
    createModem("ril_1"); // ril_0 already exists
    ASSERT_NO_THROW(startIndicator());

    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .submenu()
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .section()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-full"))
                .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-full"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch())
            .item(wifiSettings())
        ).match());
}

TEST_F(TestIndicatorNetworkService, FlightModeTalksToURfkill)
{
    ASSERT_NO_THROW(startIndicator());

    auto& urfkillInterface = dbusMock.urfkillInterface();
    QSignalSpy urfkillSpy(&urfkillInterface, SIGNAL(FlightModeChanged(bool)));

    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .submenu()
            .item(flightModeSwitch(false)
                .activate() // <--- Activate the action now
            )
        ).match());

    // Wait to be notified that flight mode was enabled
    WAIT_FOR_SIGNALS(urfkillSpy, 1);
    EXPECT_EQ(urfkillSpy.first(), QVariantList() << QVariant(true));
}

TEST_F(TestIndicatorNetworkService, IndicatorListensToURfkill)
{
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);
    auto device = createWiFiDevice(NM_DEVICE_STATE_ACTIVATED);
    auto ap = createAccessPoint("0", "the ssid", device);
    auto connection = createAccessPointConnection("0", "the ssid", device);
    createActiveConnection("0", device, connection, ap);

    ASSERT_NO_THROW(startIndicator());

    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher())
            .item(wifiEnableSwitch())
            .item(mh::MenuItemMatcher()
                .has_exactly(1) // <-- has one access point
            )
        ).match());

    ASSERT_TRUE(dbusMock.urfkillInterface().FlightMode(true));

    removeWifiConnection(device, connection);
    removeAccessPoint(device, ap);

    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(true))
            .item(mh::MenuItemMatcher())
            .item(wifiEnableSwitch(false))
            .item(mh::MenuItemMatcher()
                .is_empty() // <-- no access points
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, SimStates_NoSIM)
{
    // set flight mode off, wifi off, and cell data off
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set no sim
    setSimManagerProperty(firstModem(), "Present", false);

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // check indicator is just a 0-bar wifi icon
    // check sim status shows “No SIM” with crossed sim card icon
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "No SIM", "no-simcard"))
                .item(cellularSettings())
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, SimStates_NoSIM2)
{
    // set flight mode off, wifi off, and cell data off
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set no sim 2
    auto modem1 = createModem("ril_1");
    setSimManagerProperty(modem1, "Present", false);

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // check indicator is a 4-bar signal icon and a 0-bar wifi icon
    // check sim 2 status shows “No SIM” with crossed sim card icon
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-full"))
                .item(modemInfo("SIM 2", "No SIM", "no-simcard"))
                .item(cellularSettings())
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, SimStates_LockedSIM)
{
    // set flight mode off, wifi off, and cell data off, and sim in
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set sim locked
    setSimManagerProperty(firstModem(), "PinRequired", "pin");

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // check indicator is a locked sim card and a 0-bar wifi icon.
    // check sim status shows “SIM Locked”, with locked sim card icon and a “Unlock SIM” button beneath.
    // check that the “Unlock SIM” button has the correct action name.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"simcard-locked", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "SIM Locked", "simcard-locked", true)
                      .string_attribute("x-canonical-modem-locked-action", "indicator.modem.1::locked")
                )
                .item(cellularSettings())
            )
        ).match());

    // set sim unlocked
    setSimManagerProperty(firstModem(), "PinRequired", "none");

    // check indicator is a 4-bar signal icon and a 0-bar wifi icon
    // check sim status shows correct carrier name with 4-bar signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "fake.tel", "gsm-3g-full"))
                .item(cellularSettings())
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, SimStates_LockedSIM2)
{
    // set flight mode off, wifi off, and cell data off, and sim in
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set sim 2 locked
    auto modem1 = createModem("ril_1");
    setSimManagerProperty(modem1, "PinRequired", "pin");

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // check indicator is a 4-bar signal icon, a locked sim card and a 0-bar wifi icon.
    // check sim 2 status shows “SIM Locked”, with locked sim card icon and a “Unlock SIM” button beneath.
    // check that the “Unlock SIM” button has the correct action name.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "simcard-locked", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-full"))
                .item(modemInfo("SIM 2", "SIM Locked", "simcard-locked", true))
                .item(cellularSettings())
            )
        ).match());

    // set sim 2 unlocked
    setSimManagerProperty(modem1, "PinRequired", "none");

    // check indicator is 4-bar signal icon, a 4-bar signal icon and a 0-bar wifi icon
    // check sim statuses show correct carrier names with 4-bar signal icons.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "gsm-3g-full", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-full"))
                .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-full"))
                .item(cellularSettings())
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, SimStates_UnlockedSIM)
{
    // set flight mode off, wifi off, cell data off, sim in, and sim unlocked
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set no signal
    setNetworkRegistrationProperty(firstModem(), "Strength", QVariant::fromValue(uchar(0)));

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // check indicator is a crossed signal icon and a 0-bar wifi icon.
    // check sim status shows “No Signal” with crossed signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-no-service", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "No Signal", "gsm-3g-no-service"))
                .item(cellularSettings())
            )
        ).match());

    // set sim searching
    setNetworkRegistrationProperty(firstModem(), "Status", "searching");

    // check indicator is a disabled signal icon and a 0-bar wifi icon.
    // check sim status shows “Searching” with disabled signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-disabled", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "Searching", "gsm-3g-disabled"))
                .item(cellularSettings())
            )
        ).match());

    // set sim registered
    setNetworkRegistrationProperty(firstModem(), "Status", "registered");

    // set signal strength to 1
    setNetworkRegistrationProperty(firstModem(), "Strength", QVariant::fromValue(uchar(1)));

    // check indicator is a 0-bar signal icon and a 0-bar wifi icon.
    // check sim status shows correct carrier name with 0-bar signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-none", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "fake.tel", "gsm-3g-none"))
                .item(cellularSettings())
            )
        ).match());

    // set signal strength to 6
    setNetworkRegistrationProperty(firstModem(), "Strength", QVariant::fromValue(uchar(6)));

    // check indicator is a 1-bar signal icon and a 0-bar wifi icon.
    // check sim status shows correct carrier name with 1-bar signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-low", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "fake.tel", "gsm-3g-low"))
                .item(cellularSettings())
            )
        ).match());

    // set signal strength to 16
    setNetworkRegistrationProperty(firstModem(), "Strength", QVariant::fromValue(uchar(16)));

    // check indicator is a 2-bar signal icon and a 0-bar wifi icon.
    // check sim status shows correct carrier name with 2-bar signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-medium", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "fake.tel", "gsm-3g-medium"))
                .item(cellularSettings())
            )
        ).match());

    // set signal strength to 26
    setNetworkRegistrationProperty(firstModem(), "Strength", QVariant::fromValue(uchar(26)));

    // check indicator is a 3-bar signal icon and a 0-bar wifi icon.
    // check sim status shows correct carrier name with 3-bar signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-high", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "fake.tel", "gsm-3g-high"))
                .item(cellularSettings())
            )
        ).match());

    // set signal strength to 39
    setNetworkRegistrationProperty(firstModem(), "Strength", QVariant::fromValue(uchar(39)));

    // check indicator is a 4-bar signal icon and a 0-bar wifi icon.
    // check sim status shows correct carrier name with 4-bar signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "fake.tel", "gsm-3g-full"))
                .item(cellularSettings())
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, SimStates_UnlockedSIM2)
{
    // set flight mode off, wifi off, cell data off, sim in, and sim unlocked
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set no signal on sim 2
    auto modem1 = createModem("ril_1");
    setNetworkRegistrationProperty(modem1, "Strength", QVariant::fromValue(uchar(0)));

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // check indicator is a 4-bar signal icon, a crossed signal icon and a 0-bar wifi icon.
    // check sim 2 status shows “No Signal” with crossed signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "gsm-3g-no-service", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-full"))
                .item(modemInfo("SIM 2", "No Signal", "gsm-3g-no-service"))
                .item(cellularSettings())
            )
        ).match());

    // set sim searching
    setNetworkRegistrationProperty(modem1, "Status", "searching");

    // check indicator is a 4-bar signal icon, a disabled signal icon and a 0-bar wifi icon.
    // check sim 2 status shows “Searching” with disabled signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "gsm-3g-disabled", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-full"))
                .item(modemInfo("SIM 2", "Searching", "gsm-3g-disabled"))
                .item(cellularSettings())
            )
        ).match());

    // set sim registered
    setNetworkRegistrationProperty(modem1, "Status", "registered");

    // set signal strength to 1
    setNetworkRegistrationProperty(modem1, "Strength", QVariant::fromValue(uchar(1)));

    // check indicator is a 4-bar signal icon, a 0-bar signal icon and a 0-bar wifi icon.
    // check sim 2 status shows correct carrier name with 0-bar signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "gsm-3g-none", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-full"))
                .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-none"))
                .item(cellularSettings())
            )
        ).match());

    // set signal strength to 6
    setNetworkRegistrationProperty(modem1, "Strength", QVariant::fromValue(uchar(6)));

    // check indicator is a 4-bar signal icon, a 1-bar signal icon and a 0-bar wifi icon.
    // check sim 2 status shows correct carrier name with 1-bar signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "gsm-3g-low", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-full"))
                .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-low"))
                .item(cellularSettings())
            )
        ).match());

    // set signal strength to 16
    setNetworkRegistrationProperty(modem1, "Strength", QVariant::fromValue(uchar(16)));

    // check indicator is a 4-bar signal icon, a 2-bar signal icon and a 0-bar wifi icon.
    // check sim 2 status shows correct carrier name with 2-bar signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "gsm-3g-medium", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-full"))
                .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-medium"))
                .item(cellularSettings())
            )
        ).match());

    // set signal strength to 26
    setNetworkRegistrationProperty(modem1, "Strength", QVariant::fromValue(uchar(26)));

    // check indicator is a 4-bar signal icon, a 3-bar signal icon and a 0-bar wifi icon.
    // check sim 2 status shows correct carrier name with 3-bar signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "gsm-3g-high", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-full"))
                .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-high"))
                .item(cellularSettings())
            )
        ).match());

    // set signal strength to 39
    setNetworkRegistrationProperty(modem1, "Strength", QVariant::fromValue(uchar(39)));

    // check indicator is a 4-bar signal icon, a 4-bar signal icon and a 0-bar wifi icon.
    // check sim 2 status shows correct carrier name with 4-bar signal icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "gsm-3g-full", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-full"))
                .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-full"))
                .item(cellularSettings())
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, FlightMode_NoSIM)
{
    // set wifi on, flight mode off
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // add and connect to 2-bar unsecure AP
    auto device = createWiFiDevice(NM_DEVICE_STATE_ACTIVATED);
    auto ap = createAccessPoint("0", "the ssid", device, 40, Secure::insecure);
    auto connection = createAccessPointConnection("0", "the ssid", device);
    createActiveConnection("0", device, connection, ap);

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // set no sim
    setSimManagerProperty(firstModem(), "Present", false);

    // check indicator is just a 2-bar wifi icon
    // check sim status shows “No SIM” with crossed sim card icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-50"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "No SIM", "no-simcard"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .section()
                .item(accessPoint("the ssid",
                      Secure::insecure,
                      ApMode::infra,
                      ConnectionStatus::connected,
                      40)
                )
            )
        ).match());

    // set flight mode on
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)
                  .activate()
            )
        ).match());

    setGlobalConnectedState(NM_STATE_DISCONNECTED);
    removeWifiConnection(device, connection);
    removeAccessPoint(device, ap);

    // check that the wifi switch turns off
    // check indicator is a plane icon and a 0-bar wifi icon
    // check sim status shows “No SIM” with crossed sim card icon (unchanged).
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"airplane-mode", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "No SIM", "no-simcard"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(false))
            .item(mh::MenuItemMatcher()
                  .is_empty()
            )
        ).match());

    // set flight mode off
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(true)
                  .activate()
            )
        ).match());

    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // check that the wifi switch turns back on
    // check indicator is just a 2-bar wifi icon
    // check sim status shows “No SIM” with crossed sim card icon.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-50"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "No SIM", "no-simcard"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(true))
        ).match());
}

TEST_F(TestIndicatorNetworkService, FlightMode_LockedSIM)
{
    // set wifi on, flight mode off
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // add and connect to 1-bar secure AP
    auto device = createWiFiDevice(NM_DEVICE_STATE_ACTIVATED);
    auto ap = createAccessPoint("0", "the ssid", device, 20, Secure::secure);
    auto connection = createAccessPointConnection("0", "the ssid", device);
    createActiveConnection("0", device, connection, ap);

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // set sim locked
    setSimManagerProperty(firstModem(), "PinRequired", "pin");

    // check indicator is a locked sim card and a 1-bar locked wifi icon
    // check sim status shows “SIM Locked”, with locked sim card icon and a “Unlock SIM” button beneath.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"simcard-locked", "nm-signal-25-secure"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "SIM Locked", "simcard-locked", true))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .section()
                .item(accessPoint("the ssid",
                      Secure::secure,
                      ApMode::infra,
                      ConnectionStatus::connected,
                      20)
                )
            )
        ).match());

    // set flight mode on
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)
                  .activate()
            )
        ).match());

    setGlobalConnectedState(NM_STATE_DISCONNECTED);
    removeWifiConnection(device, connection);
    removeAccessPoint(device, ap);

    // check that the wifi switch turns off
    // check indicator is a plane icon, a locked sim card and a 0-bar wifi icon
    // check sim status shows “SIM Locked”, with locked sim card icon and a “Unlock SIM” button beneath (unchanged).
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"airplane-mode", "simcard-locked", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "SIM Locked", "simcard-locked", true))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(false))
            .item(mh::MenuItemMatcher()
                .is_empty()
            )
        ).match());

    // set flight mode off
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(true)
                  .activate()
            )
        ).match());

    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // check that the wifi switch turns back on
    // check indicator is a locked sim card and a 1-bar locked wifi icon
    // check sim status shows “SIM Locked”, with locked sim card icon and a “Unlock SIM” button beneath.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"simcard-locked", "nm-signal-25-secure"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "SIM Locked", "simcard-locked", true))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(true))
        ).match());
}

TEST_F(TestIndicatorNetworkService, FlightMode_WifiOff)
{
    // set wifi on, flight mode off
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // add some APs (secure / unsecure / adhoc / varied strength)
    auto device = createWiFiDevice(NM_DEVICE_STATE_ACTIVATED);
    auto ap1 = createAccessPoint("1", "NSD", device, 0, Secure::secure, ApMode::infra);
    auto ap2 = createAccessPoint("2", "JDR", device, 20, Secure::secure, ApMode::adhoc);
    auto ap3 = createAccessPoint("3", "DGN", device, 40, Secure::secure, ApMode::infra);
    auto ap4 = createAccessPoint("4", "JDY", device, 60, Secure::secure, ApMode::adhoc);
    auto ap5 = createAccessPoint("5", "SCE", device, 20, Secure::insecure, ApMode::infra);
    auto ap6 = createAccessPoint("6", "ADS", device, 40, Secure::insecure, ApMode::adhoc);
    auto ap7 = createAccessPoint("7", "CFT", device, 60, Secure::insecure, ApMode::infra);
    auto ap8 = createAccessPoint("8", "GDF", device, 80, Secure::insecure, ApMode::adhoc);

    // connect to 2-bar secure AP
    auto connection = createAccessPointConnection("3", "DGN", device);
    auto active_connection = createActiveConnection("3", device, connection, ap3);

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // set sim unlocked on carrier with 3-bar signal
    setNetworkRegistrationProperty(firstModem(), "Strength", QVariant::fromValue(uchar(26)));

    // check that the wifi switch is on
    // check indicator is a 3-bar signal icon and 2-bar locked wifi icon
    // check sim status shows correct carrier name with 3-bar signal icon.
    // check that AP list contains the connected AP at top then other APs underneath in alphabetical order.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-high", "nm-signal-50-secure"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "fake.tel", "gsm-3g-high"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .section()
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::connected, 40))
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 80))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 20))
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 0))
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 20))
            )
        ).match());

    // set wifi off
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
              .item(flightModeSwitch(false))
              .item(mh::MenuItemMatcher())
              .item(wifiEnableSwitch(true)
                  .activate()
              )
        ).match());

    setGlobalConnectedState(NM_STATE_DISCONNECTED);
    removeActiveConnection(device, active_connection);
    removeWifiConnection(device, connection);
    removeAccessPoint(device, ap1);
    removeAccessPoint(device, ap2);
    removeAccessPoint(device, ap3);
    removeAccessPoint(device, ap4);
    removeAccessPoint(device, ap5);
    removeAccessPoint(device, ap6);
    removeAccessPoint(device, ap7);
    removeAccessPoint(device, ap8);

    // check that the flight mode switch is still off
    // check that the wifi switch is off
    // check indicator is a 3-bar signal icon and 0-bar wifi icon
    // check sim status shows correct carrier name with 3-bar signal icon.
    // check that AP list is empty
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-high", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "fake.tel", "gsm-3g-high"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(false))
            .item(mh::MenuItemMatcher()
                .is_empty()
            )
        ).match());

    // set flight mode on
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)
                  .activate()
            )
        ).match());

    setModemProperty(firstModem(), "Online", false);

    // check indicator is a plane icon and a 0-bar wifi icon
    // check sim status shows “Offline” with 0-bar signal icon.
    // check that AP list is empty
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"airplane-mode", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "Offline", "gsm-3g-disabled"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(false))
            .item(mh::MenuItemMatcher()
                .is_empty()
            )
        ).match());

    // set flight mode off
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(true)
                  .activate()
            )
        ).match());

    setModemProperty(firstModem(), "Online", true);
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // check that the wifi switch is still off
    // check indicator is a 3-bar signal icon and 0-bar wifi icon
    // check sim status shows correct carrier name with 3-bar signal icon.
    // check that AP list is empty
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-high", "network-cellular-pre-edge"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "fake.tel", "gsm-3g-high"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(false))
            .item(mh::MenuItemMatcher()
                .is_empty()
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, FlightMode_WifiOn)
{
    // set wifi on, flight mode off
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // add some APs (secure / unsecure / adhoc / varied strength)
    auto device = createWiFiDevice(NM_DEVICE_STATE_ACTIVATED);
    auto ap1 = createAccessPoint("1", "NSD", device, 0, Secure::secure, ApMode::infra);
    auto ap2 = createAccessPoint("2", "JDR", device, 20, Secure::secure, ApMode::adhoc);
    auto ap3 = createAccessPoint("3", "DGN", device, 40, Secure::secure, ApMode::infra);
    auto ap4 = createAccessPoint("4", "JDY", device, 60, Secure::secure, ApMode::adhoc);
    auto ap5 = createAccessPoint("5", "SCE", device, 20, Secure::insecure, ApMode::infra);
    auto ap6 = createAccessPoint("6", "ADS", device, 40, Secure::insecure, ApMode::adhoc);
    auto ap7 = createAccessPoint("7", "CFT", device, 60, Secure::insecure, ApMode::infra);
    auto ap8 = createAccessPoint("8", "GDF", device, 80, Secure::insecure, ApMode::adhoc);

    // connect to 4-bar insecure AP
    auto connection = createAccessPointConnection("8", "GDF", device);
    auto active_connection = createActiveConnection("8", device, connection, ap8);

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // set sim unlocked on carrier with 1-bar signal
    setNetworkRegistrationProperty(firstModem(), "Strength", QVariant::fromValue(uchar(6)));

    // check that the wifi switch is on
    // check indicator is a 1-bar signal icon and 4-bar wifi icon
    // check sim status shows correct carrier name with 1-bar signal icon.
    // check that AP list contains the connected AP at top then other APs underneath in alphabetical order.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-low", "nm-signal-100"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "fake.tel", "gsm-3g-low"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::connected, 80))
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 40))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 20))
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 0))
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 20))
            )
        ).match());

    // set flight mode on
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)
                  .activate()
            )
        ).match());

    setModemProperty(firstModem(), "Online", false);
    setGlobalConnectedState(NM_STATE_DISCONNECTED);
    removeActiveConnection(device, active_connection);
    removeWifiConnection(device, connection);
    removeAccessPoint(device, ap1);
    removeAccessPoint(device, ap2);
    removeAccessPoint(device, ap3);
    removeAccessPoint(device, ap4);
    removeAccessPoint(device, ap5);
    removeAccessPoint(device, ap6);
    removeAccessPoint(device, ap7);
    removeAccessPoint(device, ap8);

    // check that the wifi switch turns off
    // check indicator is a plane icon and a 0-bar wifi icon
    // check sim status shows “Offline” with 0-bar signal icon.
    // check that AP list is empty
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"airplane-mode", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "Offline", "gsm-3g-disabled"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(false))
            .item(mh::MenuItemMatcher()
                .is_empty()
            )
        ).match());


    // set wifi on
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
              .item(flightModeSwitch(true))
              .item(mh::MenuItemMatcher())
              .item(wifiEnableSwitch(false)
                  .activate()
              )
        ).match());

    // NOTE: every newly created access point increments AP index (see: AccessPointItem::Private::ConstructL())
    //       so here we need to start at index 1+8 as we've had 8 APs previously.
    ap1 = createAccessPoint("9", "NSD", device, 0, Secure::secure, ApMode::infra);
    ap2 = createAccessPoint("10", "JDR", device, 20, Secure::secure, ApMode::adhoc);
    ap3 = createAccessPoint("11", "DGN", device, 40, Secure::secure, ApMode::infra);
    ap4 = createAccessPoint("12", "JDY", device, 60, Secure::secure, ApMode::adhoc);
    ap5 = createAccessPoint("13", "SCE", device, 20, Secure::insecure, ApMode::infra);
    ap6 = createAccessPoint("14", "ADS", device, 40, Secure::insecure, ApMode::adhoc);
    ap7 = createAccessPoint("15", "CFT", device, 60, Secure::insecure, ApMode::infra);
    ap8 = createAccessPoint("16", "GDF", device, 80, Secure::insecure, ApMode::adhoc);
    connection = createAccessPointConnection("16", "GDF", device);
    active_connection = createActiveConnection("16", device, connection, ap8);
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // check that the flight mode switch is still on
    // check that the wifi switch is on
    // check indicator is a plane icon and a 4-bar wifi icon
    // check sim status shows “Offline” with 0-bar signal icon.
    // check that AP list contains the connected AP highlighted at top then other APs underneath in alphabetical order.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"airplane-mode", "nm-signal-100"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "Offline", "gsm-3g-disabled"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::connected, 80))
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 40))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 20))
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 0))
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 20))
            )
        ).match());

    // set flight mode off
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(true)
                  .activate()
            )
        ).match());

    setModemProperty(firstModem(), "Online", true);

    // check that the wifi switch remains on
    // check indicator is a 1-bar signal icon and 4-bar wifi icon
    // check sim status shows correct carrier name with 1-bar signal icon.
    // check that AP list contains the connected AP highlighted at top then other APs underneath in alphabetical order.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
              .state_icons({"gsm-3g-low", "nm-signal-100"})
              .mode(mh::MenuItemMatcher::Mode::starts_with)
              .item(flightModeSwitch(false))
              .item(mh::MenuItemMatcher()
                  .item(modemInfo("", "fake.tel", "gsm-3g-low"))
                  .item(cellularSettings())
            )
            .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::connected, 80))
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 40))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 20))
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 0))
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 20))
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, GroupedWiFiAccessPoints)
{
    // set wifi on, flight mode off
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // create the wifi device
    auto device = createWiFiDevice(NM_DEVICE_STATE_ACTIVATED);

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // add a single AP
    auto ap1 = createAccessPoint("1", "groupA", device, 40, Secure::secure, ApMode::infra);

    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher())
            .item(wifiEnableSwitch())
            .item(mh::MenuItemMatcher()
                .section()
                .item(accessPoint("groupA", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 40))
            )
        ).match());

    // add a second AP with the same SSID
    auto ap2 = createAccessPoint("2", "groupA", device, 60, Secure::secure, ApMode::infra);

    // check that we see a single AP with the higher strength
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher())
            .item(wifiEnableSwitch())
            .item(mh::MenuItemMatcher()
                .section()
                .item(accessPoint("groupA", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 60))
            )
        ).match());

    // up the strength of the first AP
    setNmProperty(ap1, NM_DBUS_INTERFACE_ACCESS_POINT, "Strength", QVariant::fromValue(uchar(80)));

    // check that we see a single AP with the higher strength
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher())
            .item(wifiEnableSwitch())
            .item(mh::MenuItemMatcher()
                .section()
                .item(accessPoint("groupA", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 80))
            )
        ).match());

    // add another AP with a different SSID
    auto ap3 = createAccessPoint("3", "groupB", device, 75, Secure::secure, ApMode::infra);

    // check that we see a single AP with the higher strength
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher())
            .item(wifiEnableSwitch())
            .item(mh::MenuItemMatcher()
                .section()
                .item(accessPoint("groupA", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 80))
                .item(accessPoint("groupB", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 75))
            )
        ).match());

    // remove the first access point
    removeAccessPoint(device, ap1);

    // verify the list has the old combined access point and the strength matches the second ap initial strength
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher())
            .item(wifiEnableSwitch())
            .item(mh::MenuItemMatcher()
                .section()
                .item(accessPoint("groupA", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("groupB", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 75))
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, WifiStates_SSIDs)
{
    // set wifi on, flight mode off
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // add some APs (secure / unsecure / adhoc / varied strength)
    auto device = createWiFiDevice(NM_DEVICE_STATE_ACTIVATED);

    // prepend a non-utf8 character to the end of AP 1's SSID
    auto ap1 = createAccessPoint("1", "NSD", device, 20, Secure::secure, ApMode::infra);
    setNmProperty(ap1, NM_DBUS_INTERFACE_ACCESS_POINT, "Ssid", QByteArray(1, -1) + QByteArray("NSD"));

    // append a non-utf8 character to the end of AP 2's SSID
    auto ap2 = createAccessPoint("2", "DGN", device, 20, Secure::secure, ApMode::infra);
    setNmProperty(ap2, NM_DBUS_INTERFACE_ACCESS_POINT, "Ssid", QByteArray("DGN") + QByteArray(1, -1));

    // insert a non-utf8 character into AP 3's SSID
    auto ap3 = createAccessPoint("3", "JDY", device, 20, Secure::secure, ApMode::infra);
    setNmProperty(ap3, NM_DBUS_INTERFACE_ACCESS_POINT, "Ssid", QByteArray("JD") + QByteArray(1, -1) + QByteArray("Y"));

    // use only non-utf8 characters for AP 4's SSID
    auto ap4 = createAccessPoint("4", "---", device, 20, Secure::secure, ApMode::infra);
    setNmProperty(ap4, NM_DBUS_INTERFACE_ACCESS_POINT, "Ssid", QByteArray(4, -1));

    // leave AP 5's SSID blank
    auto ap5 = createAccessPoint("5", "", device, 20, Secure::secure, ApMode::infra);

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // check indicator is just a 4-bar locked wifi icon
    // check that AP list contains the connected AP highlighted at top then other APs underneath in alphabetical order.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-full", "nm-signal-0"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "fake.tel", "gsm-3g-full"))
                .item(cellularSettings())
            )
          .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("DGN�", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 20))
                .item(accessPoint("JD�Y", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 20))
                .item(accessPoint("�NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 20))
                .item(accessPoint("����", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 20))
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, WifiStates_Connect1AP)
{
    // create a wifi device
    auto device = createWiFiDevice(NM_DEVICE_STATE_ACTIVATED);

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // set wifi off
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
              .item(flightModeSwitch(false))
              .item(mh::MenuItemMatcher())
              .item(wifiEnableSwitch(true)
                  .activate()
              )
        ).match());

    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set no sim
    setSimManagerProperty(firstModem(), "Present", false);

    // check indicator is just a 0-bar wifi icon
    // check that AP list is empty
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "No SIM", "no-simcard"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(false))
            .item(mh::MenuItemMatcher()
                .is_empty()
            )
        ).match());

    // set wifi switch on and add some APs (secure/unsecure/adhoc/varied strength)
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
              .item(flightModeSwitch(false))
              .item(mh::MenuItemMatcher())
              .item(wifiEnableSwitch(false)
                  .activate()
              )
        ).match());

    auto ap1 = createAccessPoint("1", "NSD", device, 20, Secure::secure, ApMode::infra);
    auto ap2 = createAccessPoint("2", "JDR", device, 40, Secure::secure, ApMode::adhoc);
    auto ap3 = createAccessPoint("3", "DGN", device, 60, Secure::secure, ApMode::infra);
    auto ap4 = createAccessPoint("4", "JDY", device, 80, Secure::secure, ApMode::adhoc);
    auto ap5 = createAccessPoint("5", "SCE", device, 0, Secure::insecure, ApMode::infra);
    auto ap6 = createAccessPoint("6", "ADS", device, 20, Secure::insecure, ApMode::adhoc);
    auto ap7 = createAccessPoint("7", "CFT", device, 40, Secure::insecure, ApMode::infra);
    auto ap8 = createAccessPoint("8", "GDF", device, 60, Secure::insecure, ApMode::adhoc);
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // check indicator is still a 0-bar wifi icon
    // check that AP list contains available APs in alphabetical order (with correct signal and security icons).
    // check AP items have the correct associated action names.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-0"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "No SIM", "no-simcard"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 20))
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 40))
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 80))
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 20))
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 0))
            )
        ).match());

    // connect to 1-bar unsecure AP
    setGlobalConnectedState(NM_STATE_CONNECTING);
    auto connection = createAccessPointConnection("6", "ADS", device);
    auto active_connection = createActiveConnection("6", device, connection, ap6);
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // check indicator is just a 1-bar wifi icon
    // check that AP list contains the connected AP highlighted at top then other APs underneath in alphabetical order.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-25"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)).item(mh::MenuItemMatcher()).item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::connected, 20))
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 40))
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 80))
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 20))
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 0))
            )
        ).match());

    // set AP signal strength 0
    setNmProperty(ap6, NM_DBUS_INTERFACE_ACCESS_POINT, "Strength", QVariant::fromValue(uchar(0)));

    // check indicator is a 0-bar wifi icon.
    // check that AP signal icon also updates accordingly.
    auto ap_item = mh::MenuItemMatcher::checkbox();
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-0"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)).item(mh::MenuItemMatcher()).item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::connected, 0))
                .item(ap_item).item(ap_item).item(ap_item).item(ap_item).item(ap_item).item(ap_item).item(ap_item)
            )
        ).match());

    // set AP signal strength 40
    setNmProperty(ap6, NM_DBUS_INTERFACE_ACCESS_POINT, "Strength", QVariant::fromValue(uchar(40)));

    // check indicator is a 2-bar wifi icon.
    // check that AP signal icon also updates accordingly.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-50"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)).item(mh::MenuItemMatcher()).item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::connected, 40))
                .item(ap_item).item(ap_item).item(ap_item).item(ap_item).item(ap_item).item(ap_item).item(ap_item)
            )
        ).match());

    // set AP signal strength 60
    setNmProperty(ap6, NM_DBUS_INTERFACE_ACCESS_POINT, "Strength", QVariant::fromValue(uchar(60)));

    // check indicator is a 3-bar wifi icon.
    // check that AP signal icon also updates accordingly.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-75"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)).item(mh::MenuItemMatcher()).item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::connected, 60))
                .item(ap_item).item(ap_item).item(ap_item).item(ap_item).item(ap_item).item(ap_item).item(ap_item)
            )
        ).match());

    // set AP signal strength 80
    setNmProperty(ap6, NM_DBUS_INTERFACE_ACCESS_POINT, "Strength", QVariant::fromValue(uchar(80)));

    // check indicator is a 4-bar wifi icon.
    // check that AP signal icon also updates accordingly.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-100"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)).item(mh::MenuItemMatcher()).item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::connected, 80))
                .item(ap_item).item(ap_item).item(ap_item).item(ap_item).item(ap_item).item(ap_item).item(ap_item)
            )
        ).match());

    // set wifi off
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
              .item(flightModeSwitch(false))
              .item(mh::MenuItemMatcher())
              .item(wifiEnableSwitch(true)
                  .activate()
              )
        ).match());

    setGlobalConnectedState(NM_STATE_DISCONNECTED);
    removeActiveConnection(device, active_connection);
    removeWifiConnection(device, connection);
    removeAccessPoint(device, ap1);
    removeAccessPoint(device, ap2);
    removeAccessPoint(device, ap3);
    removeAccessPoint(device, ap4);
    removeAccessPoint(device, ap5);
    removeAccessPoint(device, ap6);
    removeAccessPoint(device, ap7);
    removeAccessPoint(device, ap8);

    // check indicator is just a 0-bar wifi icon
    // check that AP list is empty
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)).item(mh::MenuItemMatcher()).item(wifiEnableSwitch(false))
            .item(mh::MenuItemMatcher()
                .is_empty()
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, WifiStates_Connect2APs)
{
    // set wifi on, flight mode off
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // set no sim
    setSimManagerProperty(firstModem(), "Present", false);

    // add some APs (secure / unsecure / adhoc / varied strength)
    auto device = createWiFiDevice(NM_DEVICE_STATE_ACTIVATED);
    auto ap1 = createAccessPoint("1", "NSD", device, 20, Secure::secure, ApMode::infra);
    auto ap2 = createAccessPoint("2", "JDR", device, 40, Secure::secure, ApMode::adhoc);
    auto ap3 = createAccessPoint("3", "DGN", device, 60, Secure::secure, ApMode::infra);
    auto ap4 = createAccessPoint("4", "JDY", device, 80, Secure::secure, ApMode::adhoc);
    auto ap5 = createAccessPoint("5", "SCE", device, 0, Secure::insecure, ApMode::infra);
    auto ap6 = createAccessPoint("6", "ADS", device, 20, Secure::insecure, ApMode::adhoc);
    auto ap7 = createAccessPoint("7", "CFT", device, 40, Secure::insecure, ApMode::infra);
    auto ap8 = createAccessPoint("8", "GDF", device, 60, Secure::insecure, ApMode::adhoc);

    // connect to 4-bar secure AP
    auto connection = createAccessPointConnection("4", "JDY", device);
    auto active_connection = createActiveConnection("4", device, connection, ap4);

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // check indicator is just a 4-bar locked wifi icon
    // check that AP list contains the connected AP highlighted at top then other APs underneath in alphabetical order.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-100-secure"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "No SIM", "no-simcard"))
                .item(cellularSettings())
            )
          .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::connected, 80))
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 20))
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 40))
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 20))
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 0))
            )
        ).match());

    // connect to 2-bar unsecure AP
    setGlobalConnectedState(NM_STATE_DISCONNECTED);
    removeActiveConnection(device, active_connection);
    removeWifiConnection(device, connection);
    setGlobalConnectedState(NM_STATE_CONNECTING);
    connection = createAccessPointConnection("7", "CFT", device);
    active_connection = createActiveConnection("7", device, connection, ap7);
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // check indicator is just a 2-bar wifi icon
    // check that AP list contains the connected AP highlighted at top then other APs underneath in alphabetical order.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-50"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)).item(mh::MenuItemMatcher()).item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::connected, 40))
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 20))
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 80))
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 20))
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 0))
            )
        ).match());

    // set wifi off
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
              .item(flightModeSwitch(false))
              .item(mh::MenuItemMatcher())
              .item(wifiEnableSwitch(true)
                  .activate()
              )
        ).match());

    setGlobalConnectedState(NM_STATE_DISCONNECTED);
    removeActiveConnection(device, active_connection);
    removeWifiConnection(device, connection);
    removeAccessPoint(device, ap1);
    removeAccessPoint(device, ap2);
    removeAccessPoint(device, ap3);
    removeAccessPoint(device, ap4);
    removeAccessPoint(device, ap5);
    removeAccessPoint(device, ap6);
    removeAccessPoint(device, ap7);
    removeAccessPoint(device, ap8);

    // check indicator is just a 0-bar wifi icon
    // check that AP list is empty
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)).item(mh::MenuItemMatcher()).item(wifiEnableSwitch(false))
            .item(mh::MenuItemMatcher()
                .is_empty()
            )
        ).match());

    // set wifi on
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
              .item(flightModeSwitch(false))
              .item(mh::MenuItemMatcher())
              .item(wifiEnableSwitch(false)
                  .activate()
              )
        ).match());

    // NOTE: every newly created access point increments AP index (see: AccessPointItem::Private::ConstructL())
    //       so here we need to start at index 1+8 as we've had 8 APs previously.
    ap1 = createAccessPoint("9", "NSD", device, 20, Secure::secure, ApMode::infra);
    ap2 = createAccessPoint("10", "JDR", device, 40, Secure::secure, ApMode::adhoc);
    ap3 = createAccessPoint("11", "DGN", device, 60, Secure::secure, ApMode::infra);
    ap4 = createAccessPoint("12", "JDY", device, 80, Secure::secure, ApMode::adhoc);
    ap5 = createAccessPoint("13", "SCE", device, 0, Secure::insecure, ApMode::infra);
    ap6 = createAccessPoint("14", "ADS", device, 20, Secure::insecure, ApMode::adhoc);
    ap7 = createAccessPoint("15", "CFT", device, 40, Secure::insecure, ApMode::infra);
    ap8 = createAccessPoint("16", "GDF", device, 60, Secure::insecure, ApMode::adhoc);

    connection = createAccessPointConnection("12", "JDY", device);
    active_connection = createActiveConnection("12", device, connection, ap4);
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // check that the 4-bar secure AP is reconnected (as it has the highest signal).
    // check indicator is just a 4-bar locked wifi icon
    // check that AP list contains the connected AP highlighted at top then other APs underneath in alphabetical order.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-100-secure"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false)).item(mh::MenuItemMatcher()).item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::connected, 80))
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 20))
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 40))
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 20))
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 0))
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, WifiStates_AddAndActivate)
{
    // set wifi on, flight mode off
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set no sim
    setSimManagerProperty(firstModem(), "Present", false);

    // add some APs (secure / unsecure / adhoc / varied strength)
    auto device = createWiFiDevice(NM_DEVICE_STATE_ACTIVATED);
    auto ap1 = createAccessPoint("1", "NSD", device, 40, Secure::secure, ApMode::infra);
    auto ap2 = createAccessPoint("2", "JDR", device, 40, Secure::secure, ApMode::adhoc);
    auto ap3 = createAccessPoint("3", "DGN", device, 60, Secure::secure, ApMode::infra);
    auto ap4 = createAccessPoint("4", "JDY", device, 80, Secure::secure, ApMode::adhoc);
    auto ap5 = createAccessPoint("5", "SCE", device, 20, Secure::insecure, ApMode::infra);
    auto ap6 = createAccessPoint("6", "ADS", device, 20, Secure::insecure, ApMode::adhoc);
    auto ap7 = createAccessPoint("7", "CFT", device, 40, Secure::insecure, ApMode::infra);
    auto ap8 = createAccessPoint("8", "GDF", device, 60, Secure::insecure, ApMode::adhoc);

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    // check indicator is just a 0-bar wifi icon
    // check that AP list contains the APs in alphabetical order.
    // activate the "SCE" AP (AddAndActivateConnection)
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "No SIM", "no-simcard"))
                .item(cellularSettings())
            )
          .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 20))
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 40))
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 80))
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 40))
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 20)
                      .activate())
            )
        ).match());

    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // check indicator is just a 1-bar wifi icon
    // check that AP list contains the connected AP highlighted at top then other APs underneath in alphabetical order.
    // activate the "NSD" AP (AddAndActivateConnection)
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-25"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "No SIM", "no-simcard"))
                .item(cellularSettings())
            )
          .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::connected, 20))
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 20))
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 40))
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 80))
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 40)
                      .activate())
            )
        ).match());

    setGlobalConnectedState(NM_STATE_CONNECTING);
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // check indicator is just a 2-bar locked wifi icon
    // check that AP list contains the connected AP highlighted at top then other APs underneath in alphabetical order.
    // re-activate the "SCE" AP (ActivateConnection)
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-50-secure"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "No SIM", "no-simcard"))
                .item(cellularSettings())
            )
          .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::connected, 40))
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 20))
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 40))
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 80))
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 20)
                      .activate())
            )
        ).match());

    setGlobalConnectedState(NM_STATE_CONNECTING);
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // check indicator is just a 1-bar wifi icon
    // check that AP list contains the connected AP highlighted at top then other APs underneath in alphabetical order.
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"nm-signal-25"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch(false))
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "No SIM", "no-simcard"))
                .item(cellularSettings())
            )
          .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("SCE", Secure::insecure, ApMode::infra, ConnectionStatus::connected, 20))
                .item(accessPoint("ADS", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 20))
                .item(accessPoint("CFT", Secure::insecure, ApMode::infra, ConnectionStatus::disconnected, 40))
                .item(accessPoint("DGN", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 60))
                .item(accessPoint("GDF", Secure::insecure, ApMode::adhoc, ConnectionStatus::disconnected, 60))
                .item(accessPoint("JDR", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 40))
                .item(accessPoint("JDY", Secure::secure, ApMode::adhoc, ConnectionStatus::disconnected, 80))
                .item(accessPoint("NSD", Secure::secure, ApMode::infra, ConnectionStatus::disconnected, 40))
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, CellDataEnabled)
{
    // We are connected
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // Create a WiFi device and power it off
    auto device = createWiFiDevice(NM_DEVICE_STATE_DISCONNECTED);
    disableWiFi();

    // sim in with carrier and 4-bar signal and HSPA
    setNetworkRegistrationProperty(firstModem(), "Strength", QVariant::fromValue(uchar(26)));
    setNetworkRegistrationProperty(firstModem(), "Technology", "hspa");
    setModemProperty(firstModem(), "Online", true);
    setConnectionManagerProperty(firstModem(), "Powered", true);

    // second sim with umts (3G)
    auto secondModem = createModem("ril_1");
    setNetworkRegistrationProperty(secondModem, "Strength", QVariant::fromValue(uchar(6)));
    setNetworkRegistrationProperty(secondModem, "Technology", "umts");
    setModemProperty(secondModem, "Online", true);
    setConnectionManagerProperty(secondModem, "Powered", false);

    ASSERT_NO_THROW(startIndicator());

    // Should be connected to HSPA
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-high", "gsm-3g-low", "network-cellular-hspa"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-high"))
                .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-low"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(false))
        ).match());

    // First SIM card now only has EDGE
    setNetworkRegistrationProperty(firstModem(), "Technology", "edge");

    // Now we should have an EDGE icon
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
            .item(mh::MenuItemMatcher()
                .state_icons({"gsm-3g-high", "gsm-3g-low", "network-cellular-edge"})
                .mode(mh::MenuItemMatcher::Mode::starts_with)
                .item(flightModeSwitch())
                .item(mh::MenuItemMatcher()
                    .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-high"))
                    .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-low"))
                    .item(cellularSettings())
                )
                .item(wifiEnableSwitch(false))
            ).match());

    // Set second SIM as the active data connection
    setConnectionManagerProperty(firstModem(), "Powered", false);
    setConnectionManagerProperty(secondModem, "Powered", true);

    // Now we should have a 3G icon
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
            .item(mh::MenuItemMatcher()
                .state_icons({"gsm-3g-high", "gsm-3g-low", "network-cellular-3g"})
                .mode(mh::MenuItemMatcher::Mode::starts_with)
                .item(flightModeSwitch())
                .item(mh::MenuItemMatcher()
                    .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-high"))
                    .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-low"))
                    .item(cellularSettings())
                )
                .item(wifiEnableSwitch(false))
            ).match());
}

TEST_F(TestIndicatorNetworkService, CellDataDisabled)
{
    // We are connected
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // Create a WiFi device and power it off
    auto device = createWiFiDevice(NM_DEVICE_STATE_DISCONNECTED);
    disableWiFi();

    // sim in with carrier and 1-bar signal and HSPA, data disabled
    setNetworkRegistrationProperty(firstModem(), "Strength", QVariant::fromValue(uchar(6)));
    setNetworkRegistrationProperty(firstModem(), "Technology", "hspa");
    setModemProperty(firstModem(), "Online", true);
    setConnectionManagerProperty(firstModem(), "Powered", false);

    // second sim with 4-bar signal umts (3G), data disabled
    auto secondModem = createModem("ril_1");
    setNetworkRegistrationProperty(secondModem, "Strength", QVariant::fromValue(uchar(26)));
    setNetworkRegistrationProperty(secondModem, "Technology", "umts");
    setModemProperty(secondModem, "Online", true);
    setConnectionManagerProperty(secondModem, "Powered", false);

    ASSERT_NO_THROW(startIndicator());

    // Should be totally disconnected
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-low", "gsm-3g-high", "nm-no-connection"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-low"))
                .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-high"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(false))
        ).match());

    // Set second SIM as the active data connection
    setGlobalConnectedState(NM_STATE_CONNECTING);
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);
    setConnectionManagerProperty(firstModem(), "Powered", false);
    setConnectionManagerProperty(secondModem, "Powered", true);

    // Should be connected to 3G
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .state_icons({"gsm-3g-low", "gsm-3g-high", "network-cellular-3g"})
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-low"))
                .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-high"))
                .item(cellularSettings())
            )
            .item(wifiEnableSwitch(false))
        ).match());

    // Enable WiFi and connect to it
    enableWiFi();
    auto ap1 = createAccessPoint("1", "ABC", device, 20, Secure::secure, ApMode::infra);
    auto connection = createAccessPointConnection("1", "ABC", device);
    setNmProperty(device, NM_DBUS_INTERFACE_DEVICE, "State", QVariant::fromValue(uint(NM_DEVICE_STATE_ACTIVATED)));
    auto active_connection = createActiveConnection("1", device, connection, ap1);
    setGlobalConnectedState(NM_STATE_CONNECTING);
    setGlobalConnectedState(NM_STATE_CONNECTED_GLOBAL);

    // Should be connected to WiFi
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
       .item(mh::MenuItemMatcher()
           .state_icons({"gsm-3g-low", "gsm-3g-high", "nm-signal-25-secure"})
           .mode(mh::MenuItemMatcher::Mode::starts_with)
           .item(flightModeSwitch())
           .item(mh::MenuItemMatcher()
               .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-low"))
               .item(modemInfo("SIM 2", "fake.tel", "gsm-3g-high"))
               .item(cellularSettings())
            )
            .item(wifiEnableSwitch(true))
            .item(mh::MenuItemMatcher()
                .item(accessPoint("ABC",
                    Secure::secure,
                    ApMode::infra,
                    ConnectionStatus::connected,
                    20)
                )
            )
        ).match());
}

TEST_F(TestIndicatorNetworkService, UnlockSIM_MenuContents)
{
    // set flight mode off, wifi off, and cell data off, and sim in
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set sim locked
    setSimManagerProperty(firstModem(), "PinRequired", "pin");

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    QSignalSpy notificationsSpy(notificationsMockInterface(),
                               SIGNAL(MethodCalled(const QString &, const QVariantList &)));

    // check indicator is a locked sim card and a 0-bar wifi icon.
    // check sim status shows “SIM Locked”, with locked sim card icon and a “Unlock SIM” button beneath.
    // check that the “Unlock SIM” button has the correct action name.
    // activate “Unlock SIM” action
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "SIM Locked", "simcard-locked", true)
                      .pass_through_activate("x-canonical-modem-locked-action")
                )
                .item(cellularSettings())
            )
        ).match());

    // check that the "GetServerInformation" method was called
    // check that the "Notify" method was called twice
    // check method arguments are correct
    std::string busName;
    WAIT_FOR_SIGNALS(notificationsSpy, 3);
    {
        QVariantList const& call(notificationsSpy.at(0));
        EXPECT_EQ("GetServerInformation", call.at(0));
        QVariantList const& args(call.at(1).toList());
        ASSERT_EQ(0, args.size());
    }
    {
        QVariantList const& call(notificationsSpy.at(1));
        EXPECT_EQ("Notify", call.at(0));
        QVariantList const& args(call.at(1).toList());
        ASSERT_EQ(8, args.size());
        EXPECT_EQ("indicator-network", args.at(0));
        EXPECT_EQ(0, args.at(1));
        EXPECT_EQ("", args.at(2));
        EXPECT_EQ("Enter SIM PIN", args.at(3));
        EXPECT_EQ("3 attempts remaining", args.at(4));
        EXPECT_EQ(QStringList(), args.at(5));
        EXPECT_EQ(-1, args.at(7));

        QVariantMap hints;
        ASSERT_TRUE(qDBusArgumentToMap(args.at(6), hints));
        ASSERT_EQ(3, hints.size());
        ASSERT_TRUE(hints.contains("x-canonical-private-menu-model"));
        ASSERT_TRUE(hints.contains("x-canonical-snap-decisions"));
        ASSERT_TRUE(hints.contains("x-canonical-snap-decisions-timeout"));
        EXPECT_EQ(true, hints["x-canonical-snap-decisions"]);
        EXPECT_EQ(numeric_limits<int32_t>::max(), hints["x-canonical-snap-decisions-timeout"]);

        QVariantMap menuInfo;
        ASSERT_TRUE(qDBusArgumentToMap(hints["x-canonical-private-menu-model"], menuInfo));
        ASSERT_EQ(3, menuInfo.size());
        ASSERT_TRUE(menuInfo.contains("actions"));
        ASSERT_TRUE(menuInfo.contains("busName"));
        ASSERT_TRUE(menuInfo.contains("menuPath"));
        busName = menuInfo["busName"].toString().toStdString();
        EXPECT_EQ("/com/canonical/indicator/network/unlocksim0", menuInfo["menuPath"]);

        QVariantMap actions;
        ASSERT_TRUE(qDBusArgumentToMap(menuInfo["actions"], actions));
        ASSERT_EQ(1, actions.size());
        ASSERT_TRUE(actions.contains("notifications"));
        EXPECT_EQ("/com/canonical/indicator/network/unlocksim0", actions["notifications"]);
    }
    {
        QVariantList const& call(notificationsSpy.at(2));
        EXPECT_EQ("Notify", call.at(0));
        QVariantList const& args(call.at(1).toList());
        ASSERT_EQ(8, args.size());
        EXPECT_EQ("indicator-network", args.at(0));
        EXPECT_EQ(1, args.at(1));
        EXPECT_EQ("", args.at(2));
        EXPECT_EQ("Enter SIM PIN", args.at(3));
        EXPECT_EQ("3 attempts remaining", args.at(4));
        EXPECT_EQ(QStringList(), args.at(5));
        EXPECT_EQ(-1, args.at(7));

        QVariantMap hints;
        ASSERT_TRUE(qDBusArgumentToMap(args.at(6), hints));
        ASSERT_EQ(3, hints.size());
        ASSERT_TRUE(hints.contains("x-canonical-private-menu-model"));
        ASSERT_TRUE(hints.contains("x-canonical-snap-decisions"));
        ASSERT_TRUE(hints.contains("x-canonical-snap-decisions-timeout"));
        EXPECT_EQ(true, hints["x-canonical-snap-decisions"]);
        EXPECT_EQ(numeric_limits<int32_t>::max(), hints["x-canonical-snap-decisions-timeout"]);

        QVariantMap menuInfo;
        ASSERT_TRUE(qDBusArgumentToMap(hints["x-canonical-private-menu-model"], menuInfo));
        ASSERT_EQ(3, menuInfo.size());
        ASSERT_TRUE(menuInfo.contains("actions"));
        ASSERT_TRUE(menuInfo.contains("busName"));
        ASSERT_TRUE(menuInfo.contains("menuPath"));
        EXPECT_EQ(busName, menuInfo["busName"].toString().toStdString());
        EXPECT_EQ("/com/canonical/indicator/network/unlocksim0", menuInfo["menuPath"]);

        QVariantMap actions;
        ASSERT_TRUE(qDBusArgumentToMap(menuInfo["actions"], actions));
        ASSERT_EQ(1, actions.size());
        ASSERT_TRUE(actions.contains("notifications"));
        EXPECT_EQ("/com/canonical/indicator/network/unlocksim0", actions["notifications"]);
    }
    notificationsSpy.clear();

    // check contents of x-canonical-private-menu-model
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .action("notifications.simunlock")
            .string_attribute("x-canonical-type", "com.canonical.snapdecision.pinlock")
            .string_attribute("x-canonical-pin-min-max", "notifications.pinMinMax")
            .string_attribute("x-canonical-pin-popup", "notifications.popup")
            .string_attribute("x-canonical-pin-error", "notifications.error")
        ).match());
}

TEST_F(TestIndicatorNetworkService, UnlockSIM_Cancel)
{
    // set flight mode off, wifi off, and cell data off, and sim in
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set sim locked
    setSimManagerProperty(firstModem(), "PinRequired", "pin");

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    QSignalSpy notificationsSpy(notificationsMockInterface(),
                               SIGNAL(MethodCalled(const QString &, const QVariantList &)));

    // check indicator is a locked sim card and a 0-bar wifi icon.
    // check sim status shows “SIM Locked”, with locked sim card icon and a “Unlock SIM” button beneath.
    // check that the “Unlock SIM” button has the correct action name.
    // activate “Unlock SIM” action
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "SIM Locked", "simcard-locked", true)
                      .pass_through_activate("x-canonical-modem-locked-action")
                )
                .item(cellularSettings())
            )
        ).match());

    // check that the "GetServerInformation" method was called
    // check that the "Notify" method was called twice
    // check method arguments are correct
    std::string busName;
    WAIT_FOR_SIGNALS(notificationsSpy, 3);
    CHECK_METHOD_CALL(notificationsSpy, 0, "GetServerInformation", /* no_args */);
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 0}, {3, "Enter SIM PIN"}, {4, "3 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "3 attempts remaining"});
    {
        QVariantList const& call(notificationsSpy.at(2));
        QVariantList const& args(call.at(1).toList());
        QVariantMap hints;
        QVariantMap menuInfo;
        ASSERT_TRUE(qDBusArgumentToMap(args.at(6), hints));
        ASSERT_TRUE(qDBusArgumentToMap(hints["x-canonical-private-menu-model"], menuInfo));
        busName = menuInfo["busName"].toString().toStdString();
    }
    notificationsSpy.clear();

    // cancel the notification
    QSignalSpy notificationClosedSpy(&dbusMock.notificationDaemonInterface(),
                                     SIGNAL(NotificationClosed(uint, uint)));

    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .action("notifications.simunlock")
            .string_attribute("x-canonical-type", "com.canonical.snapdecision.pinlock")
            .string_attribute("x-canonical-pin-min-max", "notifications.pinMinMax")
            .string_attribute("x-canonical-pin-popup", "notifications.popup")
            .string_attribute("x-canonical-pin-error", "notifications.error")
            .activate(shared_ptr<GVariant>(g_variant_new_boolean(false), &mh::gvariant_deleter))
        ).match());

    // check that the "NotificationClosed" signal was emitted
    WAIT_FOR_SIGNALS(notificationClosedSpy, 1);
    EXPECT_EQ(notificationClosedSpy.first(), QVariantList() << QVariant(1) << QVariant(1));
    notificationClosedSpy.clear();

    // check that the "CloseNotification" method was called
    // check method arguments are correct
    WAIT_FOR_SIGNALS(notificationsSpy, 1);
    CHECK_METHOD_CALL(notificationsSpy, 0, "CloseNotification", {0, "1"});
    notificationsSpy.clear();

    // re-activate “Unlock SIM” action
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "SIM Locked", "simcard-locked", true)
                      .pass_through_activate("x-canonical-modem-locked-action")
                )
                .item(cellularSettings())
            )
        ).match());

    // check that the "Notify" method was called twice
    // check method arguments are correct (new notification index should be 2)
    WAIT_FOR_SIGNALS(notificationsSpy, 2);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 0});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 2});
    notificationsSpy.clear();

    // cancel the notification again
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .action("notifications.simunlock")
            .string_attribute("x-canonical-type", "com.canonical.snapdecision.pinlock")
            .string_attribute("x-canonical-pin-min-max", "notifications.pinMinMax")
            .string_attribute("x-canonical-pin-popup", "notifications.popup")
            .string_attribute("x-canonical-pin-error", "notifications.error")
            .activate(shared_ptr<GVariant>(g_variant_new_boolean(false), &mh::gvariant_deleter))
        ).match());

    // check that the "NotificationClosed" signal was emitted (new notification index should be 2)
    WAIT_FOR_SIGNALS(notificationClosedSpy, 1);
    EXPECT_EQ(notificationClosedSpy.first(), QVariantList() << QVariant(2) << QVariant(1));
    notificationClosedSpy.clear();

    // check that the "CloseNotification" method was called
    // check method arguments are correct (new notification index should be 2)
    WAIT_FOR_SIGNALS(notificationsSpy, 1);
    CHECK_METHOD_CALL(notificationsSpy, 0, "CloseNotification", {0, "2"});
    notificationsSpy.clear();
}

TEST_F(TestIndicatorNetworkService, UnlockSIM_CorrectPin)
{
    // set flight mode off, wifi off, and cell data off, and sim in
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set sim locked
    setSimManagerProperty(firstModem(), "PinRequired", "pin");

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    QSignalSpy notificationsSpy(notificationsMockInterface(),
                               SIGNAL(MethodCalled(const QString &, const QVariantList &)));

    // check indicator is a locked sim card and a 0-bar wifi icon.
    // check sim status shows “SIM Locked”, with locked sim card icon and a “Unlock SIM” button beneath.
    // check that the “Unlock SIM” button has the correct action name.
    // activate “Unlock SIM” action
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "SIM Locked", "simcard-locked", true)
                      .pass_through_activate("x-canonical-modem-locked-action")
                )
                .item(cellularSettings())
            )
        ).match());

    // check that the "GetServerInformation" method was called
    // check that the "Notify" method was called twice
    // check method arguments are correct
    std::string busName;
    WAIT_FOR_SIGNALS(notificationsSpy, 3);
    CHECK_METHOD_CALL(notificationsSpy, 0, "GetServerInformation", /* no_args */);
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 0}, {3, "Enter SIM PIN"}, {4, "3 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "3 attempts remaining"});
    {
        QVariantList const& call(notificationsSpy.at(2));
        QVariantList const& args(call.at(1).toList());
        QVariantMap hints;
        QVariantMap menuInfo;
        ASSERT_TRUE(qDBusArgumentToMap(args.at(6), hints));
        ASSERT_TRUE(qDBusArgumentToMap(hints["x-canonical-private-menu-model"], menuInfo));
        busName = menuInfo["busName"].toString().toStdString();
    }
    notificationsSpy.clear();

    // enter correct pin
    QSignalSpy notificationClosedSpy(&dbusMock.notificationDaemonInterface(),
                                     SIGNAL(NotificationClosed(uint, uint)));

    QSignalSpy modemSpy(modemMockInterface(firstModem()),
                        SIGNAL(MethodCalled(const QString &, const QVariantList &)));

    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .action("notifications.simunlock")
            .string_attribute("x-canonical-type", "com.canonical.snapdecision.pinlock")
            .string_attribute("x-canonical-pin-min-max", "notifications.pinMinMax")
            .string_attribute("x-canonical-pin-popup", "notifications.popup")
            .string_attribute("x-canonical-pin-error", "notifications.error")
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("1234"), &mh::gvariant_deleter))
        ).match());

    // check that the "EnterPin" method was called
    // check method arguments are correct
    WAIT_FOR_SIGNALS(modemSpy, 1);
    CHECK_METHOD_CALL(modemSpy, 0, "EnterPin", {0, "pin"}, {1, "1234"});
    modemSpy.clear();

    // check that the "NotificationClosed" signal was emitted
    WAIT_FOR_SIGNALS(notificationClosedSpy, 1);
    EXPECT_EQ(notificationClosedSpy.first(), QVariantList() << QVariant(1) << QVariant(1));
    notificationClosedSpy.clear();

    // check that the "CloseNotification" method was called
    // check method arguments are correct
    WAIT_FOR_SIGNALS(notificationsSpy, 1);
    CHECK_METHOD_CALL(notificationsSpy, 0, "CloseNotification", {0, "1"});
    notificationsSpy.clear();
}

TEST_F(TestIndicatorNetworkService, UnlockSIM_IncorrectPin)
{
    // set flight mode off, wifi off, and cell data off, and sim in
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set sim locked
    setSimManagerProperty(firstModem(), "PinRequired", "pin");

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    QSignalSpy notificationsSpy(notificationsMockInterface(),
                                SIGNAL(MethodCalled(const QString &, const QVariantList &)));

    // check indicator is a locked sim card and a 0-bar wifi icon.
    // check sim status shows “SIM Locked”, with locked sim card icon and a “Unlock SIM” button beneath.
    // check that the “Unlock SIM” button has the correct action name.
    // activate “Unlock SIM” action
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("", "SIM Locked", "simcard-locked", true)
                      .pass_through_activate("x-canonical-modem-locked-action")
                )
                .item(cellularSettings())
            )
        ).match());

    // check that the "GetServerInformation" method was called
    // check that the "Notify" method was called twice
    // check method arguments are correct
    std::string busName;
    WAIT_FOR_SIGNALS(notificationsSpy, 3);
    CHECK_METHOD_CALL(notificationsSpy, 0, "GetServerInformation", /* no_args */);
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 0}, {3, "Enter SIM PIN"}, {4, "3 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "3 attempts remaining"});
    {
        QVariantList const& call(notificationsSpy.at(2));
        QVariantList const& args(call.at(1).toList());
        QVariantMap hints;
        QVariantMap menuInfo;
        ASSERT_TRUE(qDBusArgumentToMap(args.at(6), hints));
        ASSERT_TRUE(qDBusArgumentToMap(hints["x-canonical-private-menu-model"], menuInfo));
        busName = menuInfo["busName"].toString().toStdString();
    }
    notificationsSpy.clear();

    // enter incorrect pin
    // check that the notification is displaying no error message
    QSignalSpy modemSpy(modemMockInterface(firstModem()),
                        SIGNAL(MethodCalled(const QString &, const QVariantList &)));

    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "")
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "EnterPin" method was called
    // check method arguments are correct
    WAIT_FOR_SIGNALS(modemSpy, 1);
    CHECK_METHOD_CALL(modemSpy, 0, "EnterPin", {0, "pin"}, {1, "4321"});
    modemSpy.clear();

    // check that the "Notify" method was called twice when retries changes, then twice again for incorrect pin
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 4);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "2 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "2 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "2 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 3, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "2 attempts remaining"});
    notificationsSpy.clear();

    // check that the notification is displaying the appropriate error message
    // close the error message
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "Sorry, incorrect PIN")
            .pass_through_activate("x-canonical-pin-error")
        ).match());

    // check that the error message is no longer displayed
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "")
        ).match());

    // enter incorrect pin again
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "EnterPin" method was called
    WAIT_FOR_SIGNALS(modemSpy, 1);
    CHECK_METHOD_CALL(modemSpy, 0, "EnterPin", {0, "pin"}, {1, "4321"});
    modemSpy.clear();

    // check that the "Notify" method was called twice when retries changes, then twice again for incorrect pin
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 4);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "1 attempt remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "1 attempt remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "1 attempt remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 3, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "1 attempt remaining"});
    notificationsSpy.clear();

    // check that the error message and last attempt popup are displayed
    // close the error and popup
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "Sorry, incorrect PIN")
            .pass_through_string_attribute("x-canonical-pin-popup",
                "Sorry, incorrect SIM PIN. This will be your last attempt. "
                "If SIM PIN is entered incorrectly you will require your PUK code to unlock.")
            .pass_through_activate("x-canonical-pin-error")
            .pass_through_activate("x-canonical-pin-popup")
        ).match());

    // check that the error and popup are no longer displayed
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "")
            .pass_through_string_attribute("x-canonical-pin-popup", "")
        ).match());

    // enter incorrect pin again
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "EnterPin" method was called
    WAIT_FOR_SIGNALS(modemSpy, 1);
    CHECK_METHOD_CALL(modemSpy, 0, "EnterPin", {0, "pin"}, {1, "4321"});
    modemSpy.clear();

    // check that the "Notify" method was called twice when retries changes, then twice again for incorrect pin
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 4);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "0 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "0 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "0 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 3, "Notify", {1, 1}, {3, "Enter SIM PIN"}, {4, "0 attempts remaining"});
    notificationsSpy.clear();

    // set sim blocked
    setSimManagerProperty(firstModem(), "PinRequired", "puk");

    // clear the "SetProperty" method call
    WAIT_FOR_SIGNALS(modemSpy, 1);
    modemSpy.clear();

    // check that the "Notify" method was called twice
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 2);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Enter PUK code"}, {4, "10 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Enter PUK code"}, {4, "10 attempts remaining"});
    notificationsSpy.clear();

    // check that the error message and last attempt popup are displayed
    // close the error and popup
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "Sorry, incorrect PIN")
            .pass_through_string_attribute("x-canonical-pin-popup",
                "Sorry, your SIM is now blocked. Please enter your PUK code to unblock SIM card. "
                "You may need to contact your network provider for PUK code.")
            .pass_through_activate("x-canonical-pin-error")
            .pass_through_activate("x-canonical-pin-popup")
        ).match());

    // check that the error and popup are no longer displayed
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "")
            .pass_through_string_attribute("x-canonical-pin-popup", "")
        ).match());

    // enter incorrect puk
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("87654321"), &mh::gvariant_deleter))
        ).match());

    // check that the "Notify" method was called twice
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 2);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Enter new SIM PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Enter new SIM PIN"}, {4, "Create new PIN"});
    notificationsSpy.clear();

    // enter new pin
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "Notify" method was called twice
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 2);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Confirm new SIM PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Confirm new SIM PIN"}, {4, "Create new PIN"});
    notificationsSpy.clear();

    // enter new pin again
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "ResetPin" method was called
    // check method arguments are correct
    WAIT_FOR_SIGNALS(modemSpy, 1);
    CHECK_METHOD_CALL(modemSpy, 0, "ResetPin", {0, "puk"}, {1, "87654321"}, {2, "4321"});
    modemSpy.clear();

    // check that the "Notify" method was called twice when retries changes, then twice again for incorrect pin
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 4);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Confirm new SIM PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Confirm new SIM PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "Notify", {1, 1}, {3, "Enter PUK code"}, {4, "9 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 3, "Notify", {1, 1}, {3, "Enter PUK code"}, {4, "9 attempts remaining"});
    notificationsSpy.clear();

    // check that the notification is displaying the appropriate error message
    // close the error message
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "Sorry, incorrect PUK")
            .pass_through_activate("x-canonical-pin-error")
        ).match());

    // check that the error message is no longer displayed
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "")
        ).match());

    // enter correct puk
    QSignalSpy notificationClosedSpy(&dbusMock.notificationDaemonInterface(),
                                     SIGNAL(NotificationClosed(uint, uint)));

    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("12345678"), &mh::gvariant_deleter))
        ).match());

    // check that the "Notify" method was called twice
    WAIT_FOR_SIGNALS(notificationsSpy, 2);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Enter new SIM PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Enter new SIM PIN"}, {4, "Create new PIN"});
    notificationsSpy.clear();

    // enter new pin
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "Notify" method was called twice
    WAIT_FOR_SIGNALS(notificationsSpy, 2);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Confirm new SIM PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Confirm new SIM PIN"}, {4, "Create new PIN"});
    notificationsSpy.clear();

    // enter new pin again
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "ResetPin" method was called
    // check method arguments are correct
    WAIT_FOR_SIGNALS(modemSpy, 1);
    CHECK_METHOD_CALL(modemSpy, 0, "ResetPin", {0, "puk"}, {1, "12345678"}, {2, "4321"});
    modemSpy.clear();

    // check that the "NotificationClosed" signal was emitted
    WAIT_FOR_SIGNALS(notificationClosedSpy, 1);
    EXPECT_EQ(notificationClosedSpy.first(), QVariantList() << QVariant(1) << QVariant(1));
    notificationClosedSpy.clear();

    // check that the "Notify" method was called twice when retries changes
    // check that the "CloseNotification" method was called
    // check method arguments are correct
    WAIT_FOR_SIGNALS(notificationsSpy, 3);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Confirm new SIM PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Confirm new SIM PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "CloseNotification", {0, "1"});
    notificationsSpy.clear();
}

TEST_F(TestIndicatorNetworkService, UnlockSIM2_IncorrectPin)
{
    // set flight mode off, wifi off, and cell data off, and sim in
    setGlobalConnectedState(NM_STATE_DISCONNECTED);

    // set sim locked
    auto secondModem = createModem("ril_1");
    setSimManagerProperty(secondModem, "PinRequired", "pin");

    // start the indicator
    ASSERT_NO_THROW(startIndicator());

    QSignalSpy notificationsSpy(notificationsMockInterface(),
                                SIGNAL(MethodCalled(const QString &, const QVariantList &)));

    // check indicator is a locked sim card and a 0-bar wifi icon.
    // check sim status shows “SIM Locked”, with locked sim card icon and a “Unlock SIM” button beneath.
    // check that the “Unlock SIM” button has the correct action name.
    // activate “Unlock SIM” action
    EXPECT_MATCHRESULT(mh::MenuMatcher(phoneParameters())
        .item(mh::MenuItemMatcher()
            .mode(mh::MenuItemMatcher::Mode::starts_with)
            .item(flightModeSwitch())
            .item(mh::MenuItemMatcher()
                .item(modemInfo("SIM 1", "fake.tel", "gsm-3g-full"))
                .item(modemInfo("SIM 2", "SIM Locked", "simcard-locked", true)
                      .pass_through_activate("x-canonical-modem-locked-action")
                )
                .item(cellularSettings())
            )
        ).match());

    // check that the "GetServerInformation" method was called
    // check that the "Notify" method was called twice
    // check method arguments are correct
    std::string busName;
    WAIT_FOR_SIGNALS(notificationsSpy, 3);
    CHECK_METHOD_CALL(notificationsSpy, 0, "GetServerInformation", /* no_args */);
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 0}, {3, "Enter SIM 2 PIN"}, {4, "3 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "3 attempts remaining"});
    {
        QVariantList const& call(notificationsSpy.at(2));
        QVariantList const& args(call.at(1).toList());
        QVariantMap hints;
        QVariantMap menuInfo;
        ASSERT_TRUE(qDBusArgumentToMap(args.at(6), hints));
        ASSERT_TRUE(qDBusArgumentToMap(hints["x-canonical-private-menu-model"], menuInfo));
        busName = menuInfo["busName"].toString().toStdString();
    }
    notificationsSpy.clear();

    // enter incorrect pin
    // check that the notification is displaying no error message
    QSignalSpy modemSpy(modemMockInterface(secondModem),
                        SIGNAL(MethodCalled(const QString &, const QVariantList &)));

    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "")
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "EnterPin" method was called
    // check method arguments are correct
    WAIT_FOR_SIGNALS(modemSpy, 1);
    CHECK_METHOD_CALL(modemSpy, 0, "EnterPin", {0, "pin"}, {1, "4321"});
    modemSpy.clear();

    // check that the "Notify" method was called twice when retries changes, then twice again for incorrect pin
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 4);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "2 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "2 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "2 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 3, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "2 attempts remaining"});
    notificationsSpy.clear();

    // check that the notification is displaying the appropriate error message
    // close the error message
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "Sorry, incorrect PIN")
            .pass_through_activate("x-canonical-pin-error")
        ).match());

    // check that the error message is no longer displayed
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "")
        ).match());

    // enter incorrect pin again
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "EnterPin" method was called
    WAIT_FOR_SIGNALS(modemSpy, 1);
    CHECK_METHOD_CALL(modemSpy, 0, "EnterPin", {0, "pin"}, {1, "4321"});
    modemSpy.clear();

    // check that the "Notify" method was called twice when retries changes, then twice again for incorrect pin
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 4);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "1 attempt remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "1 attempt remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "1 attempt remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 3, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "1 attempt remaining"});
    notificationsSpy.clear();

    // check that the error message and last attempt popup are displayed
    // close the error and popup
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "Sorry, incorrect PIN")
            .pass_through_string_attribute("x-canonical-pin-popup",
                "Sorry, incorrect SIM 2 PIN. This will be your last attempt. "
                "If SIM 2 PIN is entered incorrectly you will require your PUK code to unlock.")
            .pass_through_activate("x-canonical-pin-error")
            .pass_through_activate("x-canonical-pin-popup")
        ).match());

    // check that the error and popup are no longer displayed
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "")
            .pass_through_string_attribute("x-canonical-pin-popup", "")
        ).match());

    // enter incorrect pin again
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "EnterPin" method was called
    WAIT_FOR_SIGNALS(modemSpy, 1);
    CHECK_METHOD_CALL(modemSpy, 0, "EnterPin", {0, "pin"}, {1, "4321"});
    modemSpy.clear();

    // check that the "Notify" method was called twice when retries changes, then twice again for incorrect pin
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 4);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "0 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "0 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "0 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 3, "Notify", {1, 1}, {3, "Enter SIM 2 PIN"}, {4, "0 attempts remaining"});
    notificationsSpy.clear();

    // set sim blocked
    setSimManagerProperty(secondModem, "PinRequired", "puk");

    // clear the "SetProperty" method call
    WAIT_FOR_SIGNALS(modemSpy, 1);
    modemSpy.clear();

    // check that the "Notify" method was called twice
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 2);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Enter PUK code for SIM 2"}, {4, "10 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Enter PUK code for SIM 2"}, {4, "10 attempts remaining"});
    notificationsSpy.clear();

    // check that the error message and last attempt popup are displayed
    // close the error and popup
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "Sorry, incorrect PIN")
            .pass_through_string_attribute("x-canonical-pin-popup",
                "Sorry, your SIM 2 is now blocked. Please enter your PUK code to unblock SIM card. "
                "You may need to contact your network provider for PUK code.")
            .pass_through_activate("x-canonical-pin-error")
            .pass_through_activate("x-canonical-pin-popup")
        ).match());

    // check that the error and popup are no longer displayed
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "")
            .pass_through_string_attribute("x-canonical-pin-popup", "")
        ).match());

    // enter incorrect puk
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("87654321"), &mh::gvariant_deleter))
        ).match());

    // check that the "Notify" method was called twice
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 2);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Enter new SIM 2 PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Enter new SIM 2 PIN"}, {4, "Create new PIN"});
    notificationsSpy.clear();

    // enter new pin
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "Notify" method was called twice
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 2);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Confirm new SIM 2 PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Confirm new SIM 2 PIN"}, {4, "Create new PIN"});
    notificationsSpy.clear();

    // enter new pin again
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "ResetPin" method was called
    // check method arguments are correct
    WAIT_FOR_SIGNALS(modemSpy, 1);
    CHECK_METHOD_CALL(modemSpy, 0, "ResetPin", {0, "puk"}, {1, "87654321"}, {2, "4321"});
    modemSpy.clear();

    // check that the "Notify" method was called twice when retries changes, then twice again for incorrect pin
    // check method arguments are correct (notification index should still be 1)
    WAIT_FOR_SIGNALS(notificationsSpy, 4);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Confirm new SIM 2 PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Confirm new SIM 2 PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "Notify", {1, 1}, {3, "Enter PUK code for SIM 2"}, {4, "9 attempts remaining"});
    CHECK_METHOD_CALL(notificationsSpy, 3, "Notify", {1, 1}, {3, "Enter PUK code for SIM 2"}, {4, "9 attempts remaining"});
    notificationsSpy.clear();

    // check that the notification is displaying the appropriate error message
    // close the error message
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "Sorry, incorrect PUK")
            .pass_through_activate("x-canonical-pin-error")
        ).match());

    // check that the error message is no longer displayed
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .pass_through_string_attribute("x-canonical-pin-error", "")
        ).match());

    // enter correct puk
    QSignalSpy notificationClosedSpy(&dbusMock.notificationDaemonInterface(),
                                     SIGNAL(NotificationClosed(uint, uint)));

    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("12345678"), &mh::gvariant_deleter))
        ).match());

    // check that the "Notify" method was called twice
    WAIT_FOR_SIGNALS(notificationsSpy, 2);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Enter new SIM 2 PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Enter new SIM 2 PIN"}, {4, "Create new PIN"});
    notificationsSpy.clear();

    // enter new pin
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "Notify" method was called twice
    WAIT_FOR_SIGNALS(notificationsSpy, 2);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Confirm new SIM 2 PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Confirm new SIM 2 PIN"}, {4, "Create new PIN"});
    notificationsSpy.clear();

    // enter new pin again
    EXPECT_MATCHRESULT(mh::MenuMatcher(unlockSimParameters(busName, 0))
        .item(mh::MenuItemMatcher()
            .set_action_state(shared_ptr<GVariant>(g_variant_new_string("4321"), &mh::gvariant_deleter))
        ).match());

    // check that the "ResetPin" method was called
    // check method arguments are correct
    WAIT_FOR_SIGNALS(modemSpy, 1);
    CHECK_METHOD_CALL(modemSpy, 0, "ResetPin", {0, "puk"}, {1, "12345678"}, {2, "4321"});
    modemSpy.clear();

    // check that the "NotificationClosed" signal was emitted
    WAIT_FOR_SIGNALS(notificationClosedSpy, 1);
    EXPECT_EQ(notificationClosedSpy.first(), QVariantList() << QVariant(1) << QVariant(1));
    notificationClosedSpy.clear();

    // check that the "Notify" method was called twice when retries changes
    // check that the "CloseNotification" method was called
    // check method arguments are correct
    WAIT_FOR_SIGNALS(notificationsSpy, 3);
    CHECK_METHOD_CALL(notificationsSpy, 0, "Notify", {1, 1}, {3, "Confirm new SIM 2 PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 1, "Notify", {1, 1}, {3, "Confirm new SIM 2 PIN"}, {4, "Create new PIN"});
    CHECK_METHOD_CALL(notificationsSpy, 2, "CloseNotification", {0, "1"});
    notificationsSpy.clear();
}

} // namespace
