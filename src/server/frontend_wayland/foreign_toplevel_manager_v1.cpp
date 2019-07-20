/*
 * Copyright © 2019 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: William Wold <william.wold@canonical.com>
 */

#include "foreign_toplevel_manager_v1.h"

#include "window_wl_surface_role.h"
#include "mir/shell/surface_specification.h"
#include "mir/log.h"

#include <boost/throw_exception.hpp>

namespace mf = mir::frontend;
namespace geom = mir::geometry;
namespace mw = mir::wayland;

namespace mir
{
namespace frontend
{
/// An instance of the ForeignToplevelManagerV1 global, bound to a specific client
class ForeignToplevelManagerV1::Instance
    : public mw::ForeignToplevelManagerV1
{
public:
    Instance(wl_resource* new_resource, mf::ForeignToplevelManagerV1* manager);

private:
    /// Wayland requests
    ///@{
    void stop() override;
    ///@}

    mf::ForeignToplevelManagerV1* const manager;
};

/// Used by a client to aquire information about or control a specific toplevel
class ForeignToplevelHandleV1
    : public mw::ForeignToplevelHandleV1
{
public:
    ForeignToplevelHandleV1(ForeignToplevelManagerV1::Instance const& manager);

    ~ForeignToplevelHandleV1() = default;

private:
    /// Wayland requests
    ///@{
    virtual void set_maximized() = 0;
    virtual void unset_maximized() = 0;
    virtual void set_minimized() = 0;
    virtual void unset_minimized() = 0;
    virtual void activate(struct wl_resource* seat) = 0;
    virtual void close() = 0;
    virtual void set_rectangle(struct wl_resource* surface, int32_t x, int32_t y, int32_t width, int32_t height) = 0;
    virtual void destroy() = 0;
    virtual void set_fullscreen(std::experimental::optional<struct wl_resource*> const& output) = 0;
    virtual void unset_fullscreen() = 0;
    ///@}
};

}
}

// ForeignToplevelManagerV1::Instance

mf::ForeignToplevelManagerV1::ForeignToplevelManagerV1(
    struct wl_display* display,
    std::shared_ptr<Shell> const shell,
    WlSeat& seat,
    OutputManager* output_manager)
    : Global(display, Version<2>()),
      shell{shell},
      seat{seat},
      output_manager{output_manager}
{
}

void mf::ForeignToplevelManagerV1::bind(wl_resource* new_resource)
{
    new Instance{new_resource, this};
}

// ForeignToplevelManagerV1::Instance

mf::ForeignToplevelManagerV1::Instance::Instance(wl_resource* new_resource, mf::ForeignToplevelManagerV1* manager)
    : ForeignToplevelManagerV1{new_resource, Version<2>()},
      manager{manager}
{
    (void)this->manager;
    log_warning("zwlr_foreign_toplevel_manager_v1 not implemented");
}

void mf::ForeignToplevelManagerV1::Instance::stop()
{
    send_finished_event();
    destroy_wayland_object();
}

// ForeignToplevelHandleV1

mf::ForeignToplevelHandleV1::ForeignToplevelHandleV1(ForeignToplevelManagerV1::Instance const& manager)
    : mw::ForeignToplevelHandleV1(manager)
{
}

void mf::ForeignToplevelHandleV1::set_maximized()
{
    log_warning("zwlr_foreign_toplevel_handle_v1.set_maximized not implemented");
    // TODO
}

void mf::ForeignToplevelHandleV1::unset_maximized()
{
    log_warning("zwlr_foreign_toplevel_handle_v1.unset_maximized not implemented");
    // TODO
}

void mf::ForeignToplevelHandleV1::set_minimized()
{
    log_warning("zwlr_foreign_toplevel_handle_v1.set_minimized not implemented");
    // TODO
}

void mf::ForeignToplevelHandleV1::unset_minimized()
{
    log_warning("zwlr_foreign_toplevel_handle_v1.unset_minimized not implemented");
    // TODO
}

void mf::ForeignToplevelHandleV1::activate(struct wl_resource* /*seat*/)
{
    log_warning("zwlr_foreign_toplevel_handle_v1.activate not implemented");
    // TODO
}

void mf::ForeignToplevelHandleV1::close()
{
    log_warning("zwlr_foreign_toplevel_handle_v1.close not implemented");
    // TODO
}

void mf::ForeignToplevelHandleV1::set_rectangle(
    struct wl_resource* /*surface*/,
    int32_t /*x*/,
    int32_t /*y*/,
    int32_t /*width*/,
    int32_t /*height*/)
{
    log_warning("zwlr_foreign_toplevel_handle_v1.set_rectangle not implemented");
    // TODO
}

void mf::ForeignToplevelHandleV1::destroy()
{
    log_warning("zwlr_foreign_toplevel_handle_v1.destroy not implemented");
    // TODO
}

void mf::ForeignToplevelHandleV1::set_fullscreen(std::experimental::optional<struct wl_resource*> const& /*output*/)
{
    log_warning("zwlr_foreign_toplevel_handle_v1.set_fullscreen not implemented");
    // TODO
}

void mf::ForeignToplevelHandleV1::unset_fullscreen()
{
    log_warning("zwlr_foreign_toplevel_handle_v1.unset_fullscreen not implemented");
    // TODO
}
