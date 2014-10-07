/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *     Antti Kaijanmäki <antti.kaijanmaki@canonical.com>
 */

#include "platform/nmofono/manager.h"
#include <connectivity/networking/manager.h>


using namespace connectivity::networking;

Manager::Manager()
{
}

std::unique_ptr<Manager>
Manager::createInstance()
{
    std::unique_ptr<Manager> mgr;
    try {
        mgr.reset(new platform::nmofono::Manager);
    } catch(std::exception &e) {
        /// @bug dbus-cpp internal logic exploded
        // If this happens, indicator-network is in an unknown state with no clear way of
        // recovering. The only reasonable way out is a graceful exit.
        std::cerr << __PRETTY_FUNCTION__ << " Failed to connect to managerinstance: " << e.what() << std::endl;
        exit(0);
    }
    return mgr;
}
