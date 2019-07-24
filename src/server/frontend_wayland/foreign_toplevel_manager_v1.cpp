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
#include "wl_seat.h"
#include "mir/frontend/shell.h"
#include "mir/shell/surface_specification.h"
#include "mir/scene/null_observer.h"
#include "mir/scene/null_surface_observer.h"
#include "mir/scene/surface.h"
#include "mir/log.h"

#include <algorithm>
#include <mutex>
#include <map>
#include <boost/throw_exception.hpp>

namespace mf = mir::frontend;
namespace geom = mir::geometry;
namespace mw = mir::wayland;
namespace ms = mir::scene;

namespace mir
{
namespace frontend
{
/// Holds a shell observer
class ForeignToplevelManagerV1::ObserverOwner
{
public:
    ObserverOwner(
        std::shared_ptr<Shell> shell,
        WlSeat& seat,
        std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> wayland_toplevel_manager);
    ~ObserverOwner();
    ObserverOwner(ObserverOwner const&) = delete;
    ObserverOwner& operator=(ObserverOwner const&) = delete;

    /// This should be run in the body of ForeignToplevelManagerV1::ForeignToplevelManagerV1()
    /// Running it in the constructor of this object causes initialization order issues
    void start();

private:
    class Observer
        : public ms::NullObserver
    {
    public:
        Observer(
            WlSeat& seat,
            std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> wayland_toplevel_manager);
        ~Observer();
        Observer(Observer const&) = delete;
        Observer& operator=(Observer const&) = delete;

    private:
        /// Shell observer
        ///@{
        void surface_added(scene::Surface* surface) override;
        void surface_removed(scene::Surface* surface) override;
        void surface_exists(scene::Surface* surface) override;
        void end_observation() override;
        ///@}

        WlSeat& seat; ///< Used to spawn functions on the Wayland thread
        std::map<scene::Surface*, std::unique_ptr<ForeignToplevelHandleV1::ObserverOwner>> surface_observers;
        /// Can only be safely accessed on the Wayland thread
        std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> wayland_toplevel_manager;
        std::mutex mutex;
    };

    std::shared_ptr<Shell> const shell;
    std::shared_ptr<Observer> const observer;
};

/// Holds a surface observer
class ForeignToplevelHandleV1::ObserverOwner
{
public:
    ObserverOwner(
        WlSeat& seat,
        std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> const wayland_toplevel_manager,
        scene::Surface* surface);
    ~ObserverOwner();
    ObserverOwner(ObserverOwner const&) = delete;
    ObserverOwner& operator=(ObserverOwner const&) = delete;

private:
    class Observer
        : public scene::NullSurfaceObserver
    {
    public:
        Observer(
            WlSeat& seat,
            std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> const wayland_toplevel_manager,
            scene::Surface* surface);
        ~Observer();
        Observer(Observer const&) = delete;
        Observer& operator=(Observer const&) = delete;

        void invalidate_surface();

    private:
        void create_toplevel_handle(); ///< Expects calling function to manage mutex locking
        void destroy_toplevel_handle(); ///< Expects calling function to manage mutex locking

        /// Surface observer
        ///@{
        void renamed(scene::Surface const*, char const* name) override;
        ///@}

        WlSeat& seat; ///< Used to spawn functions on the Wayland thread
        /// Initialized with a value. Set to nullopt in invalidate_surface() when the surface is removed from the shell
        std::experimental::optional<scene::Surface* const> surface;
        /// Can only be safely accessed on the Wayland thread
        std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> const wayland_toplevel_manager;
        /// Inner optional can only be safely accessed on the Wayland thread
        /// nullopt means there is no wayland toplevel handle (perhaps this surface is a popup or something)
        /// A pointer to nullopt means we created a wayland object, but it has been deleted
        std::experimental::optional<
            std::shared_ptr<std::experimental::optional<ForeignToplevelHandleV1*>>> wayland_toplevel_handle;
        std::mutex mutex;
    };

    scene::Surface* const surface; ///< Used to add and remove the observer
    std::shared_ptr<Observer> const observer;
};
}
}

// ForeignToplevelManagerV1Global

mf::ForeignToplevelManagerV1Global::ForeignToplevelManagerV1Global(
    struct wl_display* display,
    std::shared_ptr<Shell> shell,
    WlSeat& seat,
    OutputManager* output_manager)
    : Global(display, Version<2>()),
      shell{shell},
      seat{seat},
      output_manager{output_manager}
{
}

void mf::ForeignToplevelManagerV1Global::bind(wl_resource* new_resource)
{
    new ForeignToplevelManagerV1{new_resource, *this};
}

// ForeignToplevelManagerV1

mf::ForeignToplevelManagerV1::ForeignToplevelManagerV1(
    wl_resource* new_resource,
    ForeignToplevelManagerV1Global& global)
    : mw::ForeignToplevelManagerV1{new_resource, Version<2>()},
      weak_self{std::make_shared<std::experimental::optional<ForeignToplevelManagerV1*>>(this)},
      observer{std::make_shared<ObserverOwner>(global.shell, global.seat, weak_self)}
{
    observer->start();
}

mf::ForeignToplevelManagerV1::~ForeignToplevelManagerV1()
{
    *weak_self = std::experimental::nullopt;
}

auto mf::ForeignToplevelManagerV1::observer_owner() const -> std::shared_ptr<ObserverOwner>
{
    return observer;
}

void mf::ForeignToplevelManagerV1::stop()
{
    send_finished_event();
    destroy_wayland_object();
}

// ForeignToplevelManagerV1::ObserverOwner

mf::ForeignToplevelManagerV1::ObserverOwner::ObserverOwner(
    std::shared_ptr<Shell> shell,
    WlSeat& seat,
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> wayland_toplevel_manager)
    : shell{shell},
      observer{std::make_shared<Observer>(seat, wayland_toplevel_manager)}
{
}

mf::ForeignToplevelManagerV1::ObserverOwner::~ObserverOwner()
{
    shell->remove_observer(observer);
}

void mf::ForeignToplevelManagerV1::ObserverOwner::start()
{
    shell->add_observer(observer);
}

// ForeignToplevelManagerV1::ObserverOwner::Observer

mf::ForeignToplevelManagerV1::ObserverOwner::Observer::Observer(
    WlSeat& seat,
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> wayland_toplevel_manager)
    : seat{seat},
      wayland_toplevel_manager{wayland_toplevel_manager}
{
}

mf::ForeignToplevelManagerV1::ObserverOwner::Observer::~Observer()
{
}

void mf::ForeignToplevelManagerV1::ObserverOwner::Observer::surface_added(ms::Surface* surface)
{
    std::lock_guard<std::mutex> lock{mutex};
    auto observer = std::make_unique<ForeignToplevelHandleV1::ObserverOwner>(seat, wayland_toplevel_manager, surface);
    surface_observers[surface] = move(observer);
}

void mf::ForeignToplevelManagerV1::ObserverOwner::Observer::surface_removed(ms::Surface* surface)
{
    std::lock_guard<std::mutex> lock{mutex};
    surface_observers.erase(surface);
}

void mf::ForeignToplevelManagerV1::ObserverOwner::Observer::surface_exists(ms::Surface* surface)
{
    std::lock_guard<std::mutex> lock{mutex};
    auto observer = std::make_unique<ForeignToplevelHandleV1::ObserverOwner>(seat, wayland_toplevel_manager, surface);
    surface_observers[surface] = move(observer);
}

void mf::ForeignToplevelManagerV1::ObserverOwner::Observer::end_observation()
{
    std::lock_guard<std::mutex> lock{mutex};
    surface_observers.clear();
}

// ForeignToplevelHandleV1::ObserverOwner

mf::ForeignToplevelHandleV1::ObserverOwner::ObserverOwner(
    WlSeat& seat,
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> const wayland_toplevel_manager,
    scene::Surface* surface)
    : surface{surface},
      observer{std::make_shared<Observer>(seat, wayland_toplevel_manager, surface)}
{
    surface->add_observer(observer);
}

mf::ForeignToplevelHandleV1::ObserverOwner::~ObserverOwner()
{
    observer->invalidate_surface();
    surface->remove_observer(observer);
}

// ForeignToplevelHandleV1::ObserverOwner::Observer

mf::ForeignToplevelHandleV1::ObserverOwner::Observer::Observer(
    WlSeat& seat,
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> const wayland_toplevel_manager,
    scene::Surface* surface)
    : seat{seat},
      surface{surface},
      wayland_toplevel_manager{wayland_toplevel_manager}
{
    create_toplevel_handle();
}

mf::ForeignToplevelHandleV1::ObserverOwner::Observer::~Observer()
{
    destroy_toplevel_handle();
}

void mf::ForeignToplevelHandleV1::ObserverOwner::Observer::invalidate_surface()
{
    std::lock_guard<std::mutex> lock{mutex};
    surface = std::experimental::nullopt;
}

void mf::ForeignToplevelHandleV1::ObserverOwner::Observer::create_toplevel_handle()
{
    // If there is already a toplevel, we can return
    if (wayland_toplevel_handle)
        return;

    if (!surface)
        BOOST_THROW_EXCEPTION(std::logic_error("create_toplevel_handle() called after surface was invalidated"));

    wayland_toplevel_handle =
        std::make_shared<std::experimental::optional<ForeignToplevelHandleV1*>>(std::experimental::nullopt);
    std::string name = surface.value()->name();

    seat.spawn(
        [toplevel_manager = wayland_toplevel_manager,
         toplevel_handle = wayland_toplevel_handle.value(),
         name]()
        {
            // If the manager has been destroyed we can't create a toplevel handle
            if (!*toplevel_manager)
                return;

            new ForeignToplevelHandleV1{*toplevel_manager->value(), toplevel_handle};
            if (!*toplevel_handle)
                BOOST_THROW_EXCEPTION(std::logic_error("toplevel_handle not set up by constructor"));

            toplevel_handle->value()->send_title_event(name);
            toplevel_handle->value()->send_done_event();
        });
}

void mf::ForeignToplevelHandleV1::ObserverOwner::Observer::destroy_toplevel_handle()
{
    // If there already isn't a toplevel handle, we can safely return
    if (!wayland_toplevel_handle)
        return;

    auto captured_toplevel_handle = wayland_toplevel_handle.value();
    wayland_toplevel_handle = std::experimental::nullopt;

    seat.spawn(
        [captured_toplevel_handle]()
        {
            // If the toplevel handle has already been destroyed there's nothing to do'
            if (!*captured_toplevel_handle)
                return;

            captured_toplevel_handle->value()->destroy_wayland_object();
        });
}

void mf::ForeignToplevelHandleV1::ObserverOwner::Observer::renamed(ms::Surface const*, char const* name_c_str)
{
    std::lock_guard<std::mutex> lock{mutex};

    // If we aren't currently connected to a toplevel handle, do nothing
    if (!wayland_toplevel_handle)
        return;

    std::string name = name_c_str;

    seat.spawn([toplevel_handle = wayland_toplevel_handle.value(), name]()
        {
            // If the toplevel handle has been destroyed, there's nothing to do
            if (!*toplevel_handle)
                return;

            toplevel_handle->value()->send_title_event(name);
            toplevel_handle->value()->send_done_event();
        });
}

// ForeignToplevelHandleV1

mf::ForeignToplevelHandleV1::ForeignToplevelHandleV1(
    ForeignToplevelManagerV1 const& manager,
    std::shared_ptr<std::experimental::optional<ForeignToplevelHandleV1*>> const weak_self)
    : mw::ForeignToplevelHandleV1(manager),
      weak_self{weak_self},
      manager_observer_owner{manager.observer_owner()}
{
    *weak_self = this;
    manager.send_toplevel_event(resource);
}

mf::ForeignToplevelHandleV1::~ForeignToplevelHandleV1()
{
    *weak_self = std::experimental::nullopt;
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
