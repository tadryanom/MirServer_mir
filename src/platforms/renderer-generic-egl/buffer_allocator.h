/*
 * Copyright © Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MIR_GRAPHICS_EGL_GENERIC_BUFFER_ALLOCATOR_H_
#define MIR_GRAPHICS_EGL_GENERIC_BUFFER_ALLOCATOR_H_

#include "mir/graphics/graphic_buffer_allocator.h"
#include "mir/graphics/linux_dmabuf.h"

#include <EGL/egl.h>
#include <wayland-server-core.h>

#include <memory>

namespace mir
{
namespace renderer
{
namespace gl
{
class Context;
}
}
namespace graphics
{
class Display;
struct EGLExtensions;

namespace common
{
class EGLContextExecutor;
}

namespace egl::generic
{

class BufferAllocator:
    public graphics::GraphicBufferAllocator
{
public:
    explicit BufferAllocator(Display const& output);

    std::shared_ptr<Buffer> alloc_software_buffer(geometry::Size size, MirPixelFormat) override;
    std::vector<MirPixelFormat> supported_pixel_formats() override;

    void bind_display(wl_display* display, std::shared_ptr<Executor> wayland_executor) override;
    void unbind_display(wl_display* display) override;
    auto buffer_from_resource(
        wl_resource* buffer,
        std::function<void()>&& on_consumed,
        std::function<void()>&& on_release) -> std::shared_ptr<Buffer> override;
    auto buffer_from_shm(
        std::shared_ptr<renderer::software::RWMappableBuffer> data,
        std::function<void()>&& on_consumed,
        std::function<void()>&& on_release) -> std::shared_ptr<Buffer> override;
private:
    std::shared_ptr<renderer::gl::Context> const ctx;
    std::shared_ptr<common::EGLContextExecutor> const egl_delegate;
    std::shared_ptr<Executor> wayland_executor;
    std::unique_ptr<LinuxDmaBufUnstable, std::function<void(LinuxDmaBufUnstable*)>> dmabuf_extension;
    std::shared_ptr<EGLExtensions> const egl_extensions;
    bool egl_display_bound{false};
};

}
}
}

#endif // MIR_GRAPHICS_EGL_GENERIC_BUFFER_ALLOCATOR_H_
