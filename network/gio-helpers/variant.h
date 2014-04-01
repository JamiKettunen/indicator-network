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

#ifndef VARIANT_H
#define VARIANT_H

#include <map>
#include <vector>
#include <memory>
#include <gio/gio.h>

#include <cassert>

#include "util.h"

class Variant;

template<typename T>
struct Codec
{
    static GVariant *encode_argument(const T& arg);
    static void decode_argument(const Variant &variant, T &arg);
};

class Variant
{
public:
    static Variant fromGVariant(GVariant *variant) {
        return Variant(variant);
    }

    Variant()
        : Variant(nullptr)
    {}

    virtual ~Variant() = default;

    inline Variant(const Variant&) = default;

    inline Variant& operator=(Variant&& rhs)
    {
        m_variant = std::move(rhs.m_variant);
        return *this;
    }

    inline Variant(Variant&& rhs)
        : m_variant(std::move(rhs.m_variant))
    {}

    inline Variant& operator=(const Variant& rhs)
    {
        m_variant = rhs.m_variant;
        return *this;
    }

    operator bool() const { return m_variant.get() != nullptr; }

    template<typename T>
    T as()
    {
        T value;
        Codec<T>::decode_argument(*this, value);
        return value;
    }

    bool operator==(Variant &rhs) const
    {
        return m_variant == rhs.m_variant;
    }
    bool operator!=(Variant &rhs) const
    {
        return !(*this == rhs);
    }

    operator GVariant*() const { return m_variant.get(); }

protected:
    Variant(GVariant *variant)
    {
        m_variant = make_gvariant_ptr(variant);
    }

    GVariantPtr m_variant;
};

template<typename T>
class TypedVariant : public Variant
{
public:
    TypedVariant(const T &value = T())
    {
        m_variant = make_gvariant_ptr(Codec<T>::encode_argument(value));
    }


};


template<>
struct Codec<bool>
{
    inline static GVariant *encode_argument(bool value)
    {
        return g_variant_new_boolean(value);
    }
    inline static void decode_argument(const Variant &variant, bool &value)
    {
        assert(variant);
        assert(g_variant_is_of_type(variant, G_VARIANT_TYPE_BOOLEAN));
        value = g_variant_get_boolean(variant);
    }
};

template<>
struct Codec<std::string>
{
    inline static GVariant *encode_argument(const std::string &value)
    {
        return g_variant_new_string(value.c_str());
    }
    inline static void decode_argument(const Variant &variant, std::string &value)
    {
        assert(variant);
        assert(g_variant_is_of_type(variant, G_VARIANT_TYPE_STRING));
        value = g_variant_get_string(variant, NULL);
    }
};

template<>
struct Codec<std::uint8_t>
{
    inline static GVariant *encode_argument(const std::uint8_t value)
    {
        return g_variant_new_byte(value);
    }
    inline static void decode_argument(const Variant &variant, std::uint8_t &value)
    {
        assert(variant);
        assert(g_variant_is_of_type(variant, G_VARIANT_TYPE_BYTE));
        value = g_variant_get_byte(variant);
    }
};

template<>
struct Codec<std::int32_t>
{
    inline static GVariant *encode_argument(const std::int32_t value)
    {
        return g_variant_new_int32(value);
    }
    inline static void decode_argument(const Variant &variant, std::int32_t &value)
    {
        assert(variant);
        assert(g_variant_is_of_type(variant, G_VARIANT_TYPE_INT32));
        value = g_variant_get_int32(variant);
    }
};

template<>
struct Codec<std::map<std::string, Variant>>
{
    inline static GVariant *encode_argument(const std::map<std::string, Variant> &value)
    {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
        for (auto pair : value) {
            g_variant_builder_add(&builder,
                                  "{sv}",
                                  pair.first.c_str(),
                                  pair.second.operator GVariant *());
        }
        return g_variant_builder_end(&builder);
    }
    inline static void decode_argument(const Variant &variant, std::map<std::string, Variant> &value)
    {
        assert(variant);
        assert(g_variant_is_of_type(variant, G_VARIANT_TYPE_VARDICT));
        GVariantIter iter;
        GVariant *val = 0;
        gchar *key = 0;
        g_variant_iter_init (&iter, variant);
        while (g_variant_iter_loop (&iter, "{sv}", &key, &val))
        {
            /// @todo do we trust the keys to be unique..?
            value[key] = Variant::fromGVariant(g_variant_ref(val));
        }
    }
};

template<>
struct Codec<std::vector<Variant>>
{
    inline static GVariant *encode_argument(const std::vector<Variant> &values)
    {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("av"));
        for (auto value : values) {
            g_variant_builder_add(&builder,
                                  "v",
                                  value.operator GVariant*());
        }
        return g_variant_builder_end(&builder);
    }
    inline static void decode_argument(const Variant &variant, std::vector<Variant> &values)
    {
        assert(variant);
        assert(g_variant_is_of_type(variant, G_VARIANT_TYPE("av")));
        GVariantIter iter;
        GVariant *val = 0;
        g_variant_iter_init (&iter, variant);
        while (g_variant_iter_loop (&iter, "v", &val))
        {
            /// @todo do we trust the keys to be unique..?
            values.push_back(Variant::fromGVariant(val));
        }
    }
};

#endif // VARIANT_H
