/*
 * Copyright (C) 2014 Canonical, Ltd.
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
 * Authors:
 *     Antti Kaijanmäki <antti.kaijanmaki@canonical.com>
 */

#ifndef SERVICE_H
#define SERVICE_H

#include <com/ubuntu/connectivity/networking/manager.h>
namespace networking = com::ubuntu::connectivity::networking;

#include <functional>
#include <iostream>

#include <cassert>

#include "indicator-menu.h"

#include "menumodel-cpp/action-group-merger.h"
#include "menumodel-cpp/menu-exporter.h"

#include "root-state.h"
#include "modem-manager.h"

#include "quick-access-section.h"
#include "wifi-section.h"
#include "wwan-section.h"

class Service
{
    IndicatorMenu::Ptr m_desktopMenu;
    IndicatorMenu::Ptr m_desktopGreeterMenu;
    IndicatorMenu::Ptr m_desktopWifiSettingsMenu;

    IndicatorMenu::Ptr m_tabletMenu;
    IndicatorMenu::Ptr m_tabletGreeterMenu;
    IndicatorMenu::Ptr m_tabletWifiSettingsMenu;

    IndicatorMenu::Ptr m_phoneMenu;
    IndicatorMenu::Ptr m_phoneGreeterMenu;
    IndicatorMenu::Ptr m_phoneWifiSettingsMenu;

    IndicatorMenu::Ptr m_ubiquityMenu;


    std::shared_ptr<networking::Manager> m_manager;
    ModemManager::Ptr m_modemManager;
    RootState::Ptr m_rootState;


    QuickAccessSection::Ptr m_quickAccessSection;
    WifiSection::Ptr m_wifiSection;
    WwanSection::Ptr m_wwanSection;


    std::unique_ptr<MenuExporter> m_desktopMenuExporter;
    std::unique_ptr<MenuExporter> m_desktopGreeterMenuExporter;
    std::unique_ptr<MenuExporter> m_desktopWifiSettingsMenuExporter;

    std::unique_ptr<MenuExporter> m_phoneMenuExporter;
    std::unique_ptr<MenuExporter> m_phoneGreeterMenuExporter;
    std::unique_ptr<MenuExporter> m_phoneWifiSettingsMenuExporter;

    std::unique_ptr<MenuExporter> m_tabletpMenuExporter;
    std::unique_ptr<MenuExporter> m_tabletGreeterMenuExporter;
    std::unique_ptr<MenuExporter> m_tabletWifiSettingsMenuExporter;

    std::unique_ptr<MenuExporter> m_ubiquityMenuExporter;

    std::unique_ptr<ActionGroupExporter> m_actionGroupExporter;
    std::unique_ptr<ActionGroupMerger> m_actionGroupMerger;

    std::shared_ptr<SessionBus> m_sessionBus;
    std::unique_ptr<BusName> m_busName;

public:
    Service()
    {

        m_manager = networking::Manager::createInstance();
        m_modemManager = std::make_shared<ModemManager>();

        m_rootState = std::make_shared<RootState>(m_manager, m_modemManager);

        m_desktopMenu = std::make_shared<IndicatorMenu>(m_rootState, "desktop");
        m_desktopGreeterMenu = std::make_shared<IndicatorMenu>(m_rootState, "desktop.greeter");
        m_desktopWifiSettingsMenu = std::make_shared<IndicatorMenu>(m_rootState, "desktop.wifi.settings");

        m_tabletMenu = std::make_shared<IndicatorMenu>(m_rootState, "tablet");
        m_tabletGreeterMenu = std::make_shared<IndicatorMenu>(m_rootState, "tablet.greeter");
        m_tabletWifiSettingsMenu = std::make_shared<IndicatorMenu>(m_rootState, "tablet.wifi.settings");

        m_phoneMenu = std::make_shared<IndicatorMenu>(m_rootState, "phone");
        m_phoneGreeterMenu = std::make_shared<IndicatorMenu>(m_rootState, "phone.greeter");
        m_phoneWifiSettingsMenu = std::make_shared<IndicatorMenu>(m_rootState, "phone.wifi.settings");

        m_ubiquityMenu = std::make_shared<IndicatorMenu>(m_rootState, "ubiquity");


        m_quickAccessSection = std::make_shared<QuickAccessSection>(m_manager);;
        m_desktopMenu->addSection(m_quickAccessSection);
        m_phoneMenu->addSection(m_quickAccessSection);

        m_wifiSection = std::make_shared<WifiSection>(m_manager);
        m_desktopMenu->addSection(m_wifiSection);
        m_phoneMenu->addSection(m_wifiSection);

        m_wwanSection = std::make_shared<WwanSection>(m_modemManager);
        m_desktopMenu->addSection(m_wwanSection);
        m_phoneMenu->addSection(m_wwanSection);

        m_desktopMenuExporter.reset(new MenuExporter("/com/canonical/indicator/network/desktop", m_desktopMenu->menu()));
        m_desktopGreeterMenuExporter.reset(new MenuExporter("/com/canonical/indicator/network/desktop_greeter", m_desktopGreeterMenu->menu()));
        m_desktopWifiSettingsMenuExporter.reset(new MenuExporter("/com/canonical/indicator/network/desktop_wifi_settings", m_desktopWifiSettingsMenu->menu()));

        m_tabletpMenuExporter.reset(new MenuExporter("/com/canonical/indicator/network/tablet", m_tabletMenu->menu()));
        m_tabletGreeterMenuExporter.reset(new MenuExporter("/com/canonical/indicator/network/tablet_greeter", m_tabletGreeterMenu->menu()));
        m_tabletWifiSettingsMenuExporter.reset(new MenuExporter("/com/canonical/indicator/network/tablet_wifi_settings", m_tabletWifiSettingsMenu->menu()));

        m_phoneMenuExporter.reset(new MenuExporter("/com/canonical/indicator/network/phone", m_phoneMenu->menu()));
        m_phoneGreeterMenuExporter.reset(new MenuExporter("/com/canonical/indicator/network/phone_greeter", m_phoneGreeterMenu->menu()));
        m_phoneWifiSettingsMenuExporter.reset(new MenuExporter("/com/canonical/indicator/network/phone_wifi_settings", m_phoneWifiSettingsMenu->menu()));

        m_ubiquityMenuExporter.reset(new MenuExporter("/com/canonical/indicator/network/ubiquity", m_ubiquityMenu->menu()));

        // we have a single actiongroup for all the menus.
        m_actionGroupMerger.reset(new ActionGroupMerger());
        m_actionGroupMerger->add(m_desktopMenu->actionGroup());
        m_actionGroupMerger->add(m_phoneMenu->actionGroup());
        m_actionGroupExporter.reset(new ActionGroupExporter(m_actionGroupMerger->actionGroup(),
                                                            "/com/canonical/indicator/network",
                                                            "indicator"));

        m_sessionBus.reset(new SessionBus());
        m_busName.reset(new BusName("com.canonical.indicator.network",
                                    [](std::string) { std::cout << "acquired" << std::endl;
                                    },
                                    [](std::string) { std::cout << "lost" << std::endl; },
                        m_sessionBus));
    }
};

#endif
