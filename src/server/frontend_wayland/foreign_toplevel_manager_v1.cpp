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

#include "wayland_utils.h"
#include "wl_seat.h"
#include "mir/frontend/shell.h"
#include "mir/shell/surface_specification.h"
#include "mir/scene/null_observer.h"
#include "mir/scene/null_surface_observer.h"
#include "mir/scene/surface.h"
#include "mir/scene/session.h"
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

    /// Called immediately after constructor
    /// Used because of initialization order issues
    void initialize();

    std::shared_ptr<Shell> const shell;

private:
    std::shared_ptr<Observer> const observer;
};

class ForeignToplevelManagerV1::Observer
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
    scene::Surface* const surface; ///< Used to add and remove the observer
    std::shared_ptr<Observer> const observer;
};

class ForeignToplevelHandleV1::Observer
    : public scene::NullSurfaceObserver
{
public:
    Observer(
        WlSeat& seat,
        std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> wayland_toplevel_manager,
        scene::Surface* surface);
    ~Observer();
    Observer(Observer const&) = delete;
    Observer& operator=(Observer const&) = delete;

    /// Sets the surface to nullopt and closes the toplevel handle
    void invalidate_surface();

private:
    /// Expects calling function to manage mutex locking
    /// func is called on the Wayland thread
    /// func is not called and no error is raised if we don't currently have a toplevel
    /// func is not called and no error is raised if the Wayland object is destroyed
    void aquire_toplevel_handle(std::function<void(ForeignToplevelHandleV1*)> func);
    void create_toplevel_handle(); ///< Expects calling function to manage mutex locking
    void close_toplevel_handle(); ///< Expects calling function to manage mutex locking
    void create_or_close_toplevel_handle_as_needed(); ///< Expects calling function to manage mutex locking

    /// Surface observer
    ///@{
    void attrib_changed(scene::Surface const*, MirWindowAttrib attrib, int value) override;
    void renamed(scene::Surface const*, char const* name) override;
    void application_id_set_to(scene::Surface const*, std::string const& application_id) override;
    void session_set_to(scene::Surface const*, std::weak_ptr<scene::Session> const& session) override;
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
    observer->initialize();
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

void mf::ForeignToplevelManagerV1::ObserverOwner::initialize()
{
    shell->add_observer(observer);
}

// ForeignToplevelManagerV1::Observer

mf::ForeignToplevelManagerV1::Observer::Observer(
    WlSeat& seat,
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> wayland_toplevel_manager)
    : seat{seat},
      wayland_toplevel_manager{wayland_toplevel_manager}
{
}

mf::ForeignToplevelManagerV1::Observer::~Observer()
{
}

void mf::ForeignToplevelManagerV1::Observer::surface_added(ms::Surface* surface)
{
    std::lock_guard<std::mutex> lock{mutex};
    auto observer = std::make_unique<ForeignToplevelHandleV1::ObserverOwner>(seat, wayland_toplevel_manager, surface);
    surface_observers[surface] = move(observer);
}

void mf::ForeignToplevelManagerV1::Observer::surface_removed(ms::Surface* surface)
{
    std::lock_guard<std::mutex> lock{mutex};
    surface_observers.erase(surface);
}

void mf::ForeignToplevelManagerV1::Observer::surface_exists(ms::Surface* surface)
{
    std::lock_guard<std::mutex> lock{mutex};
    auto observer = std::make_unique<ForeignToplevelHandleV1::ObserverOwner>(seat, wayland_toplevel_manager, surface);
    surface_observers[surface] = move(observer);
}

void mf::ForeignToplevelManagerV1::Observer::end_observation()
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

// ForeignToplevelHandleV1::Observer

mf::ForeignToplevelHandleV1::Observer::Observer(
    WlSeat& seat,
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> const wayland_toplevel_manager,
    scene::Surface* surface)
    : seat{seat},
      surface{surface},
      wayland_toplevel_manager{wayland_toplevel_manager}
{
    create_or_close_toplevel_handle_as_needed();
}

mf::ForeignToplevelHandleV1::Observer::~Observer()
{
    invalidate_surface();
}

void mf::ForeignToplevelHandleV1::Observer::invalidate_surface()
{
    std::lock_guard<std::mutex> lock{mutex};
    surface = std::experimental::nullopt;
    create_or_close_toplevel_handle_as_needed();
}

void mf::ForeignToplevelHandleV1::Observer::aquire_toplevel_handle(
    std::function<void(ForeignToplevelHandleV1*)> func)
{
    /// It is documented that func is not called if there is no toplevel handle
    if (!wayland_toplevel_handle)
        return;

    seat.spawn(
        [toplevel_handle = wayland_toplevel_handle.value(), func]()
        {
            /// It is documented that func is not called if the toplevel handle is destroyed
            if (!*toplevel_handle)
                return;

            func(toplevel_handle->value());
        });
}

void mf::ForeignToplevelHandleV1::Observer::create_toplevel_handle()
{
    if (wayland_toplevel_handle)
        BOOST_THROW_EXCEPTION(std::logic_error("create_toplevel_handle() when toplevel already created"));

    if (!surface)
        BOOST_THROW_EXCEPTION(std::logic_error("create_toplevel_handle() called after surface was invalidated"));

    wayland_toplevel_handle =
        std::make_shared<std::experimental::optional<ForeignToplevelHandleV1*>>(std::experimental::nullopt);
    auto session = surface.value()->session();
    auto session_shared = session.lock();
    if (!session_shared)
        BOOST_THROW_EXCEPTION(std::logic_error("create_toplevel_handle() when surface was not attached to a session"));
    auto surface_id = session_shared->get_surface_id(surface.value());
    std::string name = surface.value()->name();
    std::string app_id = surface.value()->application_id();
    auto focused = surface.value()->focus_state();
    auto state = surface.value()->state();

    seat.spawn(
        [toplevel_manager = wayland_toplevel_manager,
         toplevel_handle = wayland_toplevel_handle.value(),
         session,
         surface_id,
         name,
         app_id,
         focused,
         state]()
        {
            // If the manager has been destroyed we can't create a toplevel handle
            if (!*toplevel_manager)
                return;

            new ForeignToplevelHandleV1{*toplevel_manager->value(), session, surface_id, toplevel_handle};
            if (!*toplevel_handle)
                BOOST_THROW_EXCEPTION(std::logic_error("toplevel_handle not set up by constructor"));

            if (!name.empty())
                toplevel_handle->value()->send_title_event(name);
            if (!app_id.empty())
                toplevel_handle->value()->send_app_id_event(app_id);
            toplevel_handle->value()->send_state(focused, state);
            toplevel_handle->value()->send_done_event();
        });
}

void mf::ForeignToplevelHandleV1::Observer::close_toplevel_handle()
{
    if (!wayland_toplevel_handle)
        BOOST_THROW_EXCEPTION(std::logic_error("destroy_toplevel_handle() when toplevel not created"));

    aquire_toplevel_handle([](ForeignToplevelHandleV1* toplevel_handle)
        {
            toplevel_handle->has_closed();
        });

    wayland_toplevel_handle = std::experimental::nullopt;
}

void mf::ForeignToplevelHandleV1::Observer::create_or_close_toplevel_handle_as_needed()
{
    bool should_have_toplevel = true;

    if (surface)
    {
        auto& surface_value = this->surface.value();

        switch(surface_value->state())
        {
        case mir_window_state_attached:
        case mir_window_state_hidden:
            should_have_toplevel = false;
            break;

        default:
            break;
        }

        switch (surface_value->type())
        {
        case mir_window_type_normal:
        case mir_window_type_utility:
        case mir_window_type_freestyle:
            break;

        default:
            should_have_toplevel = false;
            break;
        }

        if (!surface_value->session().lock())
            should_have_toplevel = false;
    }
    else
    {
        should_have_toplevel = false;
    }

    if (should_have_toplevel && !wayland_toplevel_handle)
    {
        create_toplevel_handle();
    }
    else if (!should_have_toplevel && wayland_toplevel_handle)
    {
        close_toplevel_handle();
    }
}

void mf::ForeignToplevelHandleV1::Observer::attrib_changed(
    const scene::Surface*,
    MirWindowAttrib attrib,
    int value)
{
    std::lock_guard<std::mutex> lock{mutex};

    auto toplevel_handel_existed_before = wayland_toplevel_handle.operator bool();
    if (!surface)
        return;
    auto& surface_value = this->surface.value();

    switch (attrib)
    {
    case mir_window_attrib_state:
        create_or_close_toplevel_handle_as_needed();
        if (toplevel_handel_existed_before)
        {
            auto focused = surface_value->focus_state();
            auto state = static_cast<MirWindowState>(value);
            aquire_toplevel_handle([focused, state](ForeignToplevelHandleV1* toplevel_handle)
            {
                toplevel_handle->send_state(focused, state);
                toplevel_handle->send_done_event();
            });
        }
        break;

    case mir_window_attrib_focus:
    {
        auto focused = static_cast<MirWindowFocusState>(value);
        auto state = surface_value->state();
        aquire_toplevel_handle([focused, state](ForeignToplevelHandleV1* toplevel_handle)
            {
                toplevel_handle->send_state(focused, state);
                toplevel_handle->send_done_event();
            });
        break;
    }

    case mir_window_attrib_type:
        create_or_close_toplevel_handle_as_needed();
        break;

    default:
        break;
    }
}

void mf::ForeignToplevelHandleV1::Observer::renamed(ms::Surface const*, char const* name_c_str)
{
    std::lock_guard<std::mutex> lock{mutex};

    std::string name = name_c_str;
    aquire_toplevel_handle([name](ForeignToplevelHandleV1* toplevel_handle)
        {
            toplevel_handle->send_title_event(name);
            toplevel_handle->send_done_event();
        });
}

void mf::ForeignToplevelHandleV1::Observer::application_id_set_to(
    scene::Surface const*, std::string const& application_id)
{
    std::lock_guard<std::mutex> lock{mutex};

    std::string id = application_id;
    aquire_toplevel_handle([id](ForeignToplevelHandleV1* toplevel_handle)
        {
            toplevel_handle->send_app_id_event(id);
            toplevel_handle->send_done_event();
        });
}

void mf::ForeignToplevelHandleV1::Observer::session_set_to(
    scene::Surface const*,
    std::weak_ptr<scene::Session> const& /*session*/)
{
    std::lock_guard<std::mutex> lock{mutex};
    create_or_close_toplevel_handle_as_needed();
}

// ForeignToplevelHandleV1

void mf::ForeignToplevelHandleV1::send_state(MirWindowFocusState focused, MirWindowState state)
{
    switch (state)
    {
    case mir_window_state_restored:
    case mir_window_state_maximized:
    case mir_window_state_horizmaximized:
    case mir_window_state_vertmaximized:
        cached_normal_state = state;
        cached_fullscreen = false;
        break;

    case mir_window_state_fullscreen:
        cached_fullscreen = true;
        break;

    default:
        break;
    }

    wl_array states;
    wl_array_init(&states);

    if (focused == mir_window_focus_state_focused)
    {
        if (uint32_t* state = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t))))
            *state = State::activated;
    }

    switch (state)
    {
    case mir_window_state_maximized:
    case mir_window_state_horizmaximized:
    case mir_window_state_vertmaximized:
        if (uint32_t *state = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t))))
            *state = State::maximized;
        break;

    case mir_window_state_fullscreen:
        if (uint32_t *state = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t))))
            *state = State::fullscreen;
        break;

    case mir_window_state_minimized:
        if (uint32_t *state = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t))))
            *state = State::minimized;
        break;

    default:
        break;
    }

    send_state_event(&states);
    wl_array_release(&states);
}

void mf::ForeignToplevelHandleV1::has_closed()
{
    send_closed_event();
    *weak_self = std::experimental::nullopt;
    surface_id = std::experimental::nullopt;
}

mf::ForeignToplevelHandleV1::ForeignToplevelHandleV1(
    ForeignToplevelManagerV1 const& manager,
    std::weak_ptr<Session> session,
    SurfaceId surface_id,
    std::shared_ptr<std::experimental::optional<ForeignToplevelHandleV1*>> weak_self)
    : mw::ForeignToplevelHandleV1{manager},
      weak_self{weak_self},
      manager_observer_owner{manager.observer_owner()},
      shell{manager_observer_owner->shell},
      session{session},
      surface_id{surface_id}
{
    *weak_self = this;
    manager.send_toplevel_event(resource);
}

mf::ForeignToplevelHandleV1::~ForeignToplevelHandleV1()
{
    *weak_self = std::experimental::nullopt;
}

void mf::ForeignToplevelHandleV1::modify_surface(shell::SurfaceSpecification const& spec)
{
    if (!surface_id)
        return;
    auto shell = this->shell.lock();
    if (!shell)
        return;
    auto session = this->session.lock();
    if (!session)
        return;
    shell->modify_surface(session, surface_id.value(), spec);
}

void mf::ForeignToplevelHandleV1::set_maximized()
{
    if (cached_fullscreen)
    {
        cached_normal_state = mir_window_state_maximized;
    }
    else
    {
        shell::SurfaceSpecification spec;
        spec.state = mir_window_state_maximized;
        modify_surface(spec);
    }
}

void mf::ForeignToplevelHandleV1::unset_maximized()
{
    if (cached_fullscreen)
    {
        cached_normal_state = mir_window_state_restored;
    }
    else
    {
        shell::SurfaceSpecification spec;
        spec.state = mir_window_state_restored;
        modify_surface(spec);
    }
}

void mf::ForeignToplevelHandleV1::set_minimized()
{
    shell::SurfaceSpecification spec;
    spec.state = mir_window_state_minimized;
    modify_surface(spec);
}

void mf::ForeignToplevelHandleV1::unset_minimized()
{
    shell::SurfaceSpecification spec;
    spec.state = cached_normal_state;
    modify_surface(spec);
    activate(nullptr);
}

void mf::ForeignToplevelHandleV1::activate(struct wl_resource* /*seat*/)
{
    if (!surface_id)
        return;
    auto shell = this->shell.lock();
    if (!shell)
        return;
    auto session = this->session.lock();
    if (!session)
        return;
    auto timestamp = std::numeric_limits<uint64_t>::max();
    shell->request_operation(session, surface_id.value(), timestamp, Shell::UserRequest::activate);
}

void mf::ForeignToplevelHandleV1::close()
{
    if (!surface_id)
        return;
    auto session = this->session.lock();
    if (!session)
        return;
    auto frontend_surface = session->get_surface(surface_id.value());
    auto surface = std::dynamic_pointer_cast<ms::Surface>(frontend_surface);
    if (surface)
        surface->request_client_surface_close();
}

void mf::ForeignToplevelHandleV1::set_rectangle(
    struct wl_resource* /*surface*/,
    int32_t /*x*/,
    int32_t /*y*/,
    int32_t /*width*/,
    int32_t /*height*/)
{
    // This would be used for the destination of a window minimization animation
    // Nothing must be done with this info. It is not a protocol violation to ignore it
}

void mf::ForeignToplevelHandleV1::destroy()
{
    destroy_wayland_object();
}

void mf::ForeignToplevelHandleV1::set_fullscreen(std::experimental::optional<struct wl_resource*> const& /*output*/)
{
    shell::SurfaceSpecification spec;
    spec.state = mir_window_state_fullscreen;
    modify_surface(spec);
}

void mf::ForeignToplevelHandleV1::unset_fullscreen()
{
    shell::SurfaceSpecification spec;
    spec.state = cached_normal_state;
    modify_surface(spec);
}
