/*
 * Copyright © Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "mir/events/window_event.h"
#include "mir_blob.h"

MirWindowEvent::MirWindowEvent() : MirEvent{mir_event_type_window}
{
}

auto MirWindowEvent::clone() const -> MirWindowEvent*
{
    return new MirWindowEvent{*this};
}

int MirWindowEvent::id() const
{
    return id_;
}

void MirWindowEvent::set_id(int id)
{
    id_ = id;
}

MirWindowAttrib MirWindowEvent::attrib() const
{
    return attrib_;
}

void MirWindowEvent::set_attrib(MirWindowAttrib attrib)
{
    attrib_ = attrib;
}

int MirWindowEvent::value() const
{
    return value_;
}

void MirWindowEvent::set_value(int value)
{
    value_ = value;
}

void MirWindowEvent::set_dnd_handle(std::vector<uint8_t> const& handle)
{
    dnd_handle_ = handle;
}

namespace
{
struct MyMirBlob : MirBlob
{

    size_t size() const override { return data_.size(); }
    virtual void const* data() const override { return data_.data(); }

    std::vector<uint8_t> data_;
};
}

MirBlob* MirWindowEvent::dnd_handle() const
{
    if (!dnd_handle_)
        return nullptr;

    auto blob = std::make_unique<MyMirBlob>();

    auto reader = *dnd_handle_;

    blob->data_.reserve(reader.size());

    // Can't use std::copy() as the CapnP iterators don't provide an iterator category
    for (auto p = reader.begin(); p != reader.end(); ++p)
        blob->data_.push_back(*p);

    return blob.release();
}

