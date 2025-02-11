/*
 * Copyright © Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "request.h"

Request::Request(xmlpp::Element const& node, std::string const& class_name)
    : Method{node, class_name, false}
{
}

// TODO: Decide whether to resolve wl_resource* to wrapped types (ie: Region, Surface, etc).
Emitter Request::virtual_mir_prototype() const
{
    if (is_destroy())
    {
        return nullptr;
    }
    else
    {
        return {"virtual void ", name, "(", mir_args(), ") = 0;"};
    }
}

// TODO: Decide whether to resolve wl_resource* to wrapped types (ie: Region, Surface, etc).
Emitter Request::thunk_impl() const
{
    return {"static void ", name, "_thunk(", wl_args(), ")",
        Block{
            wl2mir_converters(),
            "try",
            Block{
                (is_destroy() ?
                    Emitter{"wl_resource_destroy(resource);"}
                :
                    Lines{
                        {"auto me = static_cast<", class_name, "*>(wl_resource_get_user_data(resource));"},
                        {"me->", name, "(", mir_call_args(), ");"},
                    }
                )
            },
            "catch(ProtocolError const& err)",
            Block{
                {"wl_resource_post_error(err.resource(), err.code(), \"%s\", err.message());"},
            },
            "catch(...)",
            Block{
                {"internal_error_processing_request(client, \"", class_name, "::", name, "()\");"},
            }
        }
    };
}

Emitter Request::vtable_initialiser() const
{
    return {name, "_thunk"};
}

bool Request::is_destroy() const
{
    return type == "destructor";
}

Emitter Request::wl_args() const
{
    Emitter client_arg = "struct wl_client* client";
    std::vector<Emitter> wl_args{client_arg, "struct wl_resource* resource"};
    for (auto const& arg : arguments)
        wl_args.push_back(arg.wl_prototype());
    return Emitter::seq(wl_args, ", ");
}

Emitter Request::mir_args() const
{
    std::vector<Emitter> mir_args;
    for (auto& i : arguments)
    {
        mir_args.push_back(i.mir_prototype());
    }
    return Emitter::seq(mir_args, ", ");
}

Emitter Request::wl2mir_converters() const
{
    std::vector<Emitter> thunk_converters;
    for (auto const& arg : arguments)
    {
        if (auto converter = arg.converter())
            thunk_converters.push_back(converter.value());
    }
    return Lines{thunk_converters};
}

Emitter Request::mir_call_args() const
{
    std::vector<Emitter> call_args;
    for (auto& arg : arguments)
        call_args.push_back(arg.call_fragment());
    return Emitter::seq(call_args, ", ");
}
