/*
 * Copyright © 2014 Canonical Ltd.
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
 * Authored by: Pete Woods <pete.woods@canonical.com>
 */

#include <memory>
#include <string>

#include <gio/gio.h>

#pragma once

namespace menuharness
{
void waitForCore(GObject* obj, const std::string& signalName);

void menuWaitForItems(const std::shared_ptr<GMenuModel>& menu, int expectedCount);

void g_object_deleter(gpointer object);

void gvariant_deleter(GVariant* varptr);
}
