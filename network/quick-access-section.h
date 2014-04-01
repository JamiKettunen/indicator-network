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

#ifndef QUICK_ACCESS_SECTION_H
#define QUICK_ACCESS_SECTION_H

#include "menuitems/section.h"
#include <com/ubuntu/connectivity/networking/manager.h>

class QuickAccessSection : public Section
{
    class Private;
    std::unique_ptr<Private> d;

public:
    typedef std::shared_ptr<QuickAccessSection> Ptr;
    QuickAccessSection() = delete;
    QuickAccessSection(std::shared_ptr<com::ubuntu::connectivity::networking::Manager> manager);
    ~QuickAccessSection();

    virtual ActionGroup::Ptr actionGroup();
    virtual MenuModel::Ptr menuModel();
};

#endif
