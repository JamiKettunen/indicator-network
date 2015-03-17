/*
 * Copyright (C) 2015 Canonical, Ltd.
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
 *     Pete Woods <pete.woods@canonical.com>
 */

#include <factory.h>

#include <modem-manager.h>
#include <nmofono/manager-impl.h>

using namespace std;

struct Factory::Private
{
    ModemManager::Ptr m_modemManager;

    shared_ptr<connectivity::networking::Manager> m_nmofono;

    SessionBus::Ptr m_sessionBus;

    SessionBus::Ptr singletonSessionBus()
    {
        if (!m_sessionBus)
        {
            m_sessionBus = make_shared<SessionBus>();
        }
        return m_sessionBus;
    }

    shared_ptr<connectivity::networking::Manager> singletonNmofono()
    {
        if (!m_nmofono)
        {
            m_nmofono = make_shared<platform::nmofono::Manager>(QDBusConnection::systemBus());
        }
        return m_nmofono;
    }

    ModemManager::Ptr singletonModemManager()
    {
        if (!m_modemManager)
        {
            m_modemManager = make_shared<ModemManager>();
        }
        return m_modemManager;
    }
};

Factory::Factory() :
        d(new Private)
{
}

Factory::~Factory()
{
}

unique_ptr<MenuBuilder> Factory::newMenuBuilder()
{
    return make_unique<MenuBuilder>(*this);
}

unique_ptr<ConnectivityService> Factory::newConnectivityService()
{
    return make_unique<ConnectivityService>(d->singletonNmofono());
}

unique_ptr<RootState> Factory::newRootState()
{
    return make_unique<RootState>(d->singletonNmofono(), d->singletonModemManager());
}

unique_ptr<IndicatorMenu> Factory::newIndicatorMenu(RootState::Ptr rootState, const string &prefix)
{
    return make_unique<IndicatorMenu>(rootState, prefix);
}

unique_ptr<MenuExporter> Factory::newMenuExporter(const string &path, MenuModel::Ptr menuModel)
{
    return make_unique<MenuExporter>(d->singletonSessionBus(), path, menuModel);
}

unique_ptr<QuickAccessSection> Factory::newQuickAccessSection()
{
    return make_unique<QuickAccessSection>(d->singletonNmofono());
}

unique_ptr<WwanSection> Factory::newWwanSection()
{
    return make_unique<WwanSection>(d->singletonModemManager());
}

unique_ptr<WifiSection> Factory::newWiFiSection()
{
    return make_unique<WifiSection>(d->singletonNmofono());
}

ActionGroupMerger::UPtr Factory::newActionGroupMerger()
{
    return make_unique<ActionGroupMerger>();
}

ActionGroupExporter::UPtr Factory::newActionGroupExporter(ActionGroup::Ptr actionGroup,
                                const string &path)
{
    return make_unique<ActionGroupExporter>(d->singletonSessionBus(), actionGroup, path);
}

BusName::UPtr Factory::newBusName(string name,
                                  function<void(string)> acquired,
                                  function<void(string)> lost)
{
    return make_unique<BusName>(name, acquired, lost, d->singletonSessionBus());
}

