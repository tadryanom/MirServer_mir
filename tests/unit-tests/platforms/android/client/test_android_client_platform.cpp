/*
 * Copyright © 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#include "mir_native_window.h"
#include "mir/client_platform.h"
#include "mir_toolkit/extensions/android_buffer.h"
#include "mir/test/doubles/mock_client_context.h"
#include "mir/test/doubles/mock_egl_native_surface.h"
#include "mir/test/doubles/mock_egl.h"
#include "mir_test_framework/client_platform_factory.h"
#include <android/system/graphics.h>
#include <EGL/egl.h>
#include <system/window.h>
#include <hardware/gralloc.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <condition_variable>
#include <mutex>

using namespace testing;
using namespace mir::client;
using namespace mir::test;
using namespace mir::test::doubles;
using namespace mir_test_framework;

struct AndroidClientPlatformTest : public Test
{
    AndroidClientPlatformTest()
        : platform{create_android_client_platform()}
    {
    }

    std::shared_ptr<mir::client::ClientPlatform> platform;
    MockEGL mock_egl;
};

TEST_F(AndroidClientPlatformTest, egl_native_display_is_egl_default_display)
{
    MockEGLNativeSurface surface;
    auto mock_egl_native_surface = std::make_shared<MockEGLNativeSurface>();
    auto native_display = platform->create_egl_native_display();
    EXPECT_EQ(EGL_DEFAULT_DISPLAY, *native_display);
}

TEST_F(AndroidClientPlatformTest, egl_native_window_is_set)
{
    MockEGLNativeSurface surface;
    auto mock_egl_native_surface = std::make_shared<MockEGLNativeSurface>();
    auto egl_native_window = platform->create_egl_native_window(&surface);
    EXPECT_NE(nullptr, egl_native_window);
}

TEST_F(AndroidClientPlatformTest, egl_native_window_can_be_set_with_null_native_surface)
{
    auto egl_native_window = platform->create_egl_native_window(nullptr);
    EXPECT_NE(nullptr, egl_native_window);
}

TEST_F(AndroidClientPlatformTest, error_interpreter_used_with_null_native_surface)
{
    auto egl_native_window = platform->create_egl_native_window(nullptr);
    auto native_window = reinterpret_cast<mir::graphics::android::MirNativeWindow*>(egl_native_window.get());
    ANativeWindow& window = *native_window;
    EXPECT_EQ(window.setSwapInterval(&window, 1), -1);
}

TEST_F(AndroidClientPlatformTest, egl_pixel_format_asks_the_driver)
{
    auto const d = reinterpret_cast<EGLDisplay>(0x1234);
    auto const c = reinterpret_cast<EGLConfig>(0x5678);

    EXPECT_CALL(mock_egl, eglGetConfigAttrib(d, c, EGL_NATIVE_VISUAL_ID, _))
        .WillOnce(DoAll(SetArgPointee<3>(HAL_PIXEL_FORMAT_RGB_565),
                        Return(EGL_TRUE)))
        .WillOnce(DoAll(SetArgPointee<3>(HAL_PIXEL_FORMAT_RGB_888),
                        Return(EGL_TRUE)))
        .WillOnce(DoAll(SetArgPointee<3>(HAL_PIXEL_FORMAT_BGRA_8888),
                        Return(EGL_TRUE)))
        .WillOnce(DoAll(SetArgPointee<3>(0),
                        Return(EGL_FALSE)));

    EXPECT_EQ(mir_pixel_format_rgb_565, platform->get_egl_pixel_format(d, c));
    EXPECT_EQ(mir_pixel_format_rgb_888, platform->get_egl_pixel_format(d, c));
    EXPECT_EQ(mir_pixel_format_argb_8888, platform->get_egl_pixel_format(d, c));
    EXPECT_EQ(mir_pixel_format_invalid, platform->get_egl_pixel_format(d, c));
}

TEST_F(AndroidClientPlatformTest, can_allocate_buffer)
{
    using namespace std::literals::chrono_literals;
    int width = 32;
    int height = 90;
    auto ext = static_cast<MirExtensionAndroidBuffer*>(
        platform->request_interface(
            MIR_EXTENSION_ANDROID_BUFFER,MIR_EXTENSION_ANDROID_BUFFER_VERSION_1));
    ASSERT_THAT(ext, Ne(nullptr));
    ASSERT_THAT(ext->allocate_buffer_android, Ne(nullptr));

    static std::condition_variable cv;
    static std::mutex mut;
    static auto called = false;
    auto cb = [] (MirBuffer*, void*)
    {
        std::unique_lock<decltype(mut)> lk(mut);
        called = true;
        cv.notify_all();
    };
    ext->allocate_buffer_android(nullptr,
        width, height,
        HAL_PIXEL_FORMAT_RGBA_8888,
        GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE,
        cb, nullptr);

    std::unique_lock<decltype(mut)> lk(mut);
    EXPECT_TRUE(cv.wait_for(lk, 5s, [&] { return called; }));
}
