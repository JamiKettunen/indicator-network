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

#ifndef CONNECTIVITY_NETWORKING_LINK
#define CONNECTIVITY_NETWORKING_LINK

#include <core/property.h>

#include <QObject>

namespace connectivity {
namespace networking {

#ifndef CONNECTIVITY_CPP_EXPORT
#define CONNECTIVITY_CPP_EXPORT __attribute ((visibility ("default")))
#endif

class CONNECTIVITY_CPP_EXPORT
Link: public QObject
{
    Q_OBJECT

public:
    typedef std::shared_ptr<Link> Ptr;

    Link(const Link&) = delete;
    virtual ~Link() = default;

    Link& operator=(const Link&) = delete;
    bool operator==(const Link&) const = delete;

    /// @private
    enum class Type
    {
        wifi,
        wired,
        wwan,
        service
    };

    /// @private
    enum class Status
    {
        disabled,
        offline,
        connecting,  // link is in process of becoming online
        connected,   // the link is up, but not fully configured yet
                     // happens with wifi for example when captive portal
                     // login is required
        online
    };

    /**
     * @brief Characteristics of the link.
     * \see Manager::characteristics
     */
    enum Characteristics : std::uint32_t
    {
        /** No special characteristics. */
        empty                 = 0,

        /**
         * The link has monetary costs,
         * No data should be transfered before getting confirmation from the user.
         */
        has_monetary_costs    = 1 << 0,

        /**
         * The link has limited volume.
         * No large files should be transfered before getting a confirmation from the user.
         */
        is_volume_limited     = 1 << 1,

        /**
         * The link has limited bandwith.
         * Large transfer should be postponed until high bandwith link becomes available.
         */
        is_bandwidth_limited  = 1 << 2
    };

    /// @private
    virtual void enable()  = 0;

    /// @private
    virtual void disable() = 0;

    /// @private
    virtual Type type() const = 0;

    /// @private
    Q_PROPERTY(std::uint32_t characteristics READ characteristics NOTIFY characteristicsUpdated)
    virtual std::uint32_t characteristics() const = 0;

    /// @private
    Q_PROPERTY(connectivity::networking::Link::Status status READ status NOTIFY statusUpdated)
    virtual Status status() const = 0;

    /// @private
    typedef unsigned int Id;

    /// @private
    virtual Id id() const = 0;

    /// @private
    virtual QString name() const = 0;

Q_SIGNALS:
    void characteristicsUpdated(std::uint32_t);

    void statusUpdated(Status);

protected:
    /// @private
    Link() = default;
};

}
}

#endif
