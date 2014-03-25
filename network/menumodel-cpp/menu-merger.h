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

#ifndef MENU_MERGER_H
#define MENU_MERGER_H

#include <memory>
#include <vector>
#include <map>

#include <gio/gio.h>

#include "gio-helpers/util.h"
#include "menu-model.h"
#include "menu.h"

class MenuMerger : public MenuModel
{
    GMenuPtr m_gmenu;
    std::vector<MenuModel::Ptr> m_menus;

    std::map<GMenuModel*, MenuModel::Ptr> m_gmodelToMenu;
    std::map<GMenuModel*, int> m_startPositions;

    static void items_changed_cb(GMenuModel *model,
                                 gint        position,
                                 gint        removed,
                                 gint        added,
                                 gpointer    user_data)
    {
        MenuMerger *that = static_cast<MenuMerger*>(user_data);
        that->itemsChanged(model, position, removed, added);
    }

    void itemsChanged(GMenuModel *model,
                      gint        position,
                      gint        removed,
                      gint        added)
    {
        int offset = m_startPositions[model] + position;

        for (int i = 0; i < removed; ++i) {
            g_menu_remove(m_gmenu.get(), offset);
        }
        for (int i = added-1; i >= 0; --i) {
            auto item = g_menu_item_new_from_model(model, position + i);
            g_menu_insert_item(m_gmenu.get(), offset, item);
            g_object_unref(item);
        }

        int delta = added - removed;
        bool update = false;
        for (auto iter : m_menus) {
            if (update) {
                m_startPositions[*iter] += delta;
                continue;
            }
            if (m_gmodelToMenu[model] == iter) {
                // the remaining positions need updating
                update = true;
                continue;
            }
        }
    }

public:
    typedef std::shared_ptr<MenuMerger> Ptr;

    MenuMerger()
    {
        m_gmenu = make_gmenu_ptr();
    }

    void append(MenuModel::Ptr menu)
    {
        // calculate the start position for the items for the new menu
        int start_position;
        if (m_menus.empty()) {
            start_position = 0;
        } else {
            start_position = m_startPositions[*m_menus.back()];
            start_position += g_menu_model_get_n_items(*m_menus.back());
        }

        m_menus.push_back(menu);
        m_gmodelToMenu[*menu] = menu;
        m_startPositions[*menu] = start_position;

        // add all items
        itemsChanged(*menu, 0, 0, g_menu_model_get_n_items(*menu));

        /// @todo disconnect
        g_signal_connect(menu->operator GMenuModel *(),
                         "items-changed",
                         G_CALLBACK(MenuMerger::items_changed_cb),
                         this);
    }

    operator GMenuModel*() { return G_MENU_MODEL(m_gmenu.get()); }
};

#endif
