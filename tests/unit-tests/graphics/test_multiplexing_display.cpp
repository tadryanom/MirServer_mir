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

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <boost/throw_exception.hpp>

#include "mir/geometry/forward.h"
#include "mir/graphics/display_configuration.h"
#include "mir/graphics/display_configuration_policy.h"
#include "mir/test/doubles/mock_display.h"
#include "mir/test/doubles/mock_main_loop.h"
#include "mir/test/doubles/null_gl_context.h"
#include "mir/test/doubles/stub_display_configuration.h"
#include "mir/test/doubles/null_display_configuration_policy.h"

#include "mir_toolkit/common.h"
#include "src/server/graphics/multiplexing_display.h"


namespace mtd = mir::test::doubles;
namespace mg = mir::graphics;
namespace geom = mir::geometry;

using namespace testing;

namespace
{

class DisplayConfigurationOutputGenerator
{
public:
    DisplayConfigurationOutputGenerator()
        : next_id{0}
    {
    }

    auto generate_output(mg::DisplayConfigurationCardId card)
        -> mg::DisplayConfigurationOutput
    {
        auto const output_id = next_output_id();
        return mg::DisplayConfigurationOutput {
            output_id,
            card,
            mg::DisplayConfigurationLogicalGroupId{1},
            mg::DisplayConfigurationOutputType::edp,
            pixel_formats_for_output_id(output_id),
            {
                {geom::Size{3840, 2160}, 59.98},
            },
            0,
            geom::Size{340, 190},
            true,
            true,
            geom::Point{0,0},
            0,
            pixel_formats_for_output_id(output_id)[0],
            mir_power_mode_on,
            mir_orientation_normal,
            2.0f,
            mir_form_factor_monitor,
            mir_subpixel_arrangement_horizontal_rgb,
            {},
            mir_output_gamma_unsupported,
            {},
            {}
        };
    }

private:
    // Dumbest possible version; 
    auto pixel_formats_for_output_id(mg::DisplayConfigurationOutputId id)
        -> std::vector<MirPixelFormat>
    {
        if (id.as_value() <= 0 || id.as_value() >= mir_pixel_formats)
        {
            BOOST_THROW_EXCEPTION((std::runtime_error{"Too many configurations requested"}));
        }
        return {static_cast<MirPixelFormat>(id.as_value())};
    }

    auto next_output_id() -> mg::DisplayConfigurationOutputId
    {
        return mg::DisplayConfigurationOutputId{++next_id};
    }

    int next_id;
};

MATCHER_P(OutputConfigurationEqualIgnoringId, output, "")
{
    mg::DisplayConfigurationOutput arg_copy = arg;
    arg_copy.id = output.id;
    return ExplainMatchResult(
        Eq(output),
        arg_copy,
        result_listener);
}

// Make a MockDisplay that returns safe stubs
auto make_safe_mock_display() -> std::unique_ptr<mtd::MockDisplay>
{
    auto display = std::make_unique<testing::NiceMock<mtd::MockDisplay>>();
    ON_CALL(*display, configuration())
        .WillByDefault(
            []()
            {
                return std::make_unique<mtd::StubDisplayConfig>();
            });
    ON_CALL(*display, create_gl_context())
        .WillByDefault(
            []()
            {
                return std::make_unique<mtd::NullGLContext>();
            });
    return display;
}
}

TEST(MultiplexingDisplay, forwards_for_each_display_sync_group)
{
    std::vector<std::unique_ptr<mg::Display>> displays;

    for (int i = 0u; i < 2; ++i)
    {
        auto mock_display = make_safe_mock_display();
        EXPECT_CALL(*mock_display, for_each_display_sync_group(_)).Times(1);
        displays.push_back(std::move(mock_display));
    }

    mtd::NullDisplayConfigurationPolicy policy;
    mg::MultiplexingDisplay display{std::move(displays), policy};

    display.for_each_display_sync_group([](auto const&) {});
}


TEST(MultiplexingDisplay, each_display_in_configuration_has_unique_id)
{
    std::vector<mg::DisplayConfigurationOutput> first_display_conf, second_display_conf, third_display_conf;

    DisplayConfigurationOutputGenerator gen;
    mg::DisplayConfigurationCardId card_one{1}, card_two{3}, card_three{42};

    for (auto i = 0u; i < 3; ++i)
    {
        first_display_conf.push_back(gen.generate_output(card_one));
    }
    for (auto i = 0u; i < 4; ++i)
    {
        second_display_conf.push_back(gen.generate_output(card_two));
    }
    for (auto i = 0u; i < 2; ++i)
    {
        third_display_conf.push_back(gen.generate_output(card_three));
    }

    std::vector<std::unique_ptr<mg::Display>> displays;

    for (auto const& conf : {first_display_conf, second_display_conf, third_display_conf})
    {
        auto display = std::make_unique<NiceMock<mtd::MockDisplay>>();
        ON_CALL(*display, configuration())
            .WillByDefault(
                Invoke(
                    [conf]()
                    {
                        return std::make_unique<mtd::StubDisplayConfig>(conf);
                    }));

        displays.push_back(std::move(display));
    }

    mtd::NullDisplayConfigurationPolicy policy;
    mg::MultiplexingDisplay display{std::move(displays), policy};

    auto conf = display.configuration();

    std::vector<mg::DisplayConfigurationOutputId> seen_ids;
    conf->for_each_output(
        [&seen_ids](mg::UserDisplayConfigurationOutput const& conf)
        {
            EXPECT_THAT(seen_ids, Not(Contains(conf.id)));
            seen_ids.push_back(conf.id);
        });
}

TEST(MultiplexingDisplay, configuration_is_union_of_all_displays)
{
    std::vector<mg::DisplayConfigurationOutput> first_display_conf, second_display_conf, third_display_conf;

    DisplayConfigurationOutputGenerator gen;
    mg::DisplayConfigurationCardId card_one{1}, card_two{3}, card_three{42};

    for (auto i = 0u; i < 3; ++i)
    {
        first_display_conf.push_back(gen.generate_output(card_one));
    }
    for (auto i = 0u; i < 4; ++i)
    {
        second_display_conf.push_back(gen.generate_output(card_two));
    }
    for (auto i = 0u; i < 2; ++i)
    {
        third_display_conf.push_back(gen.generate_output(card_three));
    }

    std::vector<std::unique_ptr<mg::Display>> displays;

    for (auto const& conf : {first_display_conf, second_display_conf, third_display_conf})
    {
        auto display = std::make_unique<NiceMock<mtd::MockDisplay>>();
        ON_CALL(*display, configuration())
            .WillByDefault(
                Invoke(
                    [conf]()
                    {
                        return std::make_unique<mtd::StubDisplayConfig>(conf);
                    }));

        displays.push_back(std::move(display));
    }

    mtd::NullDisplayConfigurationPolicy policy;
    mg::MultiplexingDisplay display{std::move(displays), policy};

    auto conf = display.configuration();

    std::vector<mg::DisplayConfigurationOutput> visible_conf;
    conf->for_each_output(
        [&visible_conf](mg::DisplayConfigurationOutput const& conf)
        {
            visible_conf.push_back(conf);
        });

    std::vector<Matcher<mg::DisplayConfigurationOutput>> expected_outputs;
    // Deliberately insert these out of order, to ensure the test doesn't mistakenly enforce ordering
    std::transform(
        second_display_conf.begin(), second_display_conf.end(),
        std::back_inserter(expected_outputs),
        [](auto const& output) { return OutputConfigurationEqualIgnoringId(output); });
    std::transform(
        first_display_conf.begin(), first_display_conf.end(),
        std::back_inserter(expected_outputs),
        [](auto const& output) { return OutputConfigurationEqualIgnoringId(output); });
    std::transform(
        third_display_conf.begin(), third_display_conf.end(),
        std::back_inserter(expected_outputs),
        [](auto const& output) { return OutputConfigurationEqualIgnoringId(output); });

    EXPECT_THAT(visible_conf, UnorderedElementsAreArray(expected_outputs));
}

TEST(MultiplexingDisplay, dispatches_configure_to_associated_platform)
{
    DisplayConfigurationOutputGenerator gen;
    constexpr const mg::DisplayConfigurationCardId card1{5}, card2{42};

    auto d1 = std::make_unique<NiceMock<mtd::MockDisplay>>();
    auto d2 = std::make_unique<NiceMock<mtd::MockDisplay>>();

    // Each Display should get a configure() call with only its own configuration
    std::vector<mg::DisplayConfiguration*> d1_configurations, d2_configurations;
    EXPECT_CALL(*d1, configure(_))
        .WillRepeatedly(
            [&d1_configurations](mg::DisplayConfiguration const& conf)
            {
                EXPECT_THAT(d1_configurations, Contains(&conf));
            });
    EXPECT_CALL(*d2, configure(_))
        .WillRepeatedly(
            [&d2_configurations](mg::DisplayConfiguration const& conf)
            {
                EXPECT_THAT(d2_configurations, Contains(&conf));
            });

    auto const d1_conf = std::make_unique<mtd::StubDisplayConfig>(
        std::vector<mg::DisplayConfigurationOutput>{
            gen.generate_output(card1),
            gen.generate_output(card1)
        });
    EXPECT_CALL(*d1, configuration())
        .WillRepeatedly(
            [&d1_configurations, &d1_conf]()
            {
                auto conf = d1_conf->clone();
                d1_configurations.push_back(conf.get());
                return conf;
            });
    auto const d2_conf = std::make_unique<mtd::StubDisplayConfig>(
        std::vector<mg::DisplayConfigurationOutput>{
            gen.generate_output(card2),
            gen.generate_output(card2),
            gen.generate_output(card2)
        });
    EXPECT_CALL(*d2, configuration())
        .WillRepeatedly(
            [&d2_configurations, &d2_conf]()
            {
                auto conf = d2_conf->clone();
                d2_configurations.push_back(conf.get());
                return conf;
            });

    std::vector<std::unique_ptr<mg::Display>> displays;
    displays.push_back(std::move(d1));
    displays.push_back(std::move(d2));

    mtd::NullDisplayConfigurationPolicy policy;
    mg::MultiplexingDisplay display{std::move(displays), policy};
    auto conf = display.configuration();

    display.configure(*conf);
}

MATCHER_P(IsConfigurationOfCard, cardid, "")
{
    bool matches{true};
    arg.for_each_output(
        [&matches, this](auto const& output)
        {
            matches &= output.card_id == cardid;
        });
    return matches;
}

TEST(MultiplexingDisplay, apply_if_confguration_preserves_display_buffers_succeeds_if_all_succeed)
{
    DisplayConfigurationOutputGenerator gen;
    mg::DisplayConfigurationCardId const card1{5}, card2{54};

    auto d1 = std::make_unique<NiceMock<mtd::MockDisplay>>();
    auto d2 = std::make_unique<NiceMock<mtd::MockDisplay>>();

    ON_CALL(*d1, configuration())
        .WillByDefault(
            Invoke(
                [&gen, card1]()
                {
                    return std::make_unique<mtd::StubDisplayConfig>(
                        std::vector<mg::DisplayConfigurationOutput>{
                            gen.generate_output(card1),
                            gen.generate_output(card1)
                        });
                 }));
    ON_CALL(*d2, configuration())
        .WillByDefault(
            Invoke(
                [&gen, card2]()
                {
                    return std::make_unique<mtd::StubDisplayConfig>(
                        std::vector<mg::DisplayConfigurationOutput>{
                            gen.generate_output(card2)
                        });
                 }));

    // Each Display should get a configure() call with only its own configuration
    EXPECT_CALL(*d1, apply_if_configuration_preserves_display_buffers(IsConfigurationOfCard(card1)))
        .WillOnce(Return(true));
    EXPECT_CALL(*d2, apply_if_configuration_preserves_display_buffers(IsConfigurationOfCard(card2)))
        .WillOnce(Return(true));

    std::vector<std::unique_ptr<mg::Display>> displays;
    displays.push_back(std::move(d1));
    displays.push_back(std::move(d2));

    mtd::NullDisplayConfigurationPolicy policy;
    mg::MultiplexingDisplay display{std::move(displays), policy};
    auto conf = display.configuration();

    display.apply_if_configuration_preserves_display_buffers(*conf);
}

TEST(MultiplexingDisplay, apply_if_confguration_preserves_display_buffers_fails_if_any_fail)
{
    DisplayConfigurationOutputGenerator gen;
    mg::DisplayConfigurationCardId const card1{5}, card2{21};

    auto d1 = std::make_unique<NiceMock<mtd::MockDisplay>>();
    auto d2 = std::make_unique<NiceMock<mtd::MockDisplay>>();

    ON_CALL(*d1, configuration())
        .WillByDefault(
            Invoke(
                [&gen, card1]()
                {
                    return std::make_unique<mtd::StubDisplayConfig>(
                        std::vector<mg::DisplayConfigurationOutput>{
                            gen.generate_output(card1),
                            gen.generate_output(card1)
                        });
                 }));
    ON_CALL(*d2, configuration())
        .WillByDefault(
            Invoke(
                [&gen, card2]()
                {
                    return std::make_unique<mtd::StubDisplayConfig>(
                        std::vector<mg::DisplayConfigurationOutput>{
                            gen.generate_output(card2)
                        });
                 }));

    // Each Display should get a configure() call with only its own configuration
    EXPECT_CALL(*d1, apply_if_configuration_preserves_display_buffers(IsConfigurationOfCard(card1)))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*d2, apply_if_configuration_preserves_display_buffers(IsConfigurationOfCard(card2)))
        .WillOnce(Return(false));

    std::vector<std::unique_ptr<mg::Display>> displays;
    displays.push_back(std::move(d1));
    displays.push_back(std::move(d2));

    mtd::NullDisplayConfigurationPolicy policy;
    mg::MultiplexingDisplay display{std::move(displays), policy};
    auto conf = display.configuration();

    EXPECT_THAT(display.apply_if_configuration_preserves_display_buffers(*conf), Eq(false));
}

TEST(MultiplexingDisplay, apply_if_configuration_preserves_display_buffers_fails_to_previous_configuration)
{
    DisplayConfigurationOutputGenerator gen;
    mg::DisplayConfigurationCardId const card1{32}, card2{3};

    auto d1 = std::make_unique<NiceMock<mtd::MockDisplay>>();
    auto d2 = std::make_unique<NiceMock<mtd::MockDisplay>>();

    auto d1_conf = std::make_unique<mtd::StubDisplayConfig>(
        std::vector<mg::DisplayConfigurationOutput>{
            gen.generate_output(card1),
            gen.generate_output(card1)
        });

    auto d2_conf = std::make_unique<mtd::StubDisplayConfig>(
        std::vector<mg::DisplayConfigurationOutput>{
            gen.generate_output(card2),
            gen.generate_output(card2),
            gen.generate_output(card2)
        });

    ON_CALL(*d1, configuration())
        .WillByDefault(
            Invoke(
                [&d1_conf]()
                {
                    return d1_conf->clone();
                }));
    ON_CALL(*d2, configuration())
        .WillByDefault(
            Invoke(
                [&d2_conf]()
                {
                    return d2_conf->clone();
                }));

    // Each Display should get a configure() call with only its own configuration
    EXPECT_CALL(*d1, apply_if_configuration_preserves_display_buffers(IsConfigurationOfCard(card1)))
        .WillRepeatedly(
            Invoke(
                [&d1_conf](auto const& config)
                {
                    auto real_config = dynamic_cast<mtd::StubDisplayConfig const&>(config);
                    d1_conf->outputs = real_config.outputs;
                    return true;
                }));
    EXPECT_CALL(*d2, apply_if_configuration_preserves_display_buffers(IsConfigurationOfCard(card2)))
        .WillOnce(Return(false));

    std::vector<std::unique_ptr<mg::Display>> displays;
    displays.push_back(std::move(d1));
    displays.push_back(std::move(d2));

    mtd::NullDisplayConfigurationPolicy policy;
    mg::MultiplexingDisplay display{std::move(displays), policy};
    auto preexisting_conf = display.configuration();
    auto changed_conf = display.configuration();

    changed_conf->for_each_output(
        [](mg::UserDisplayConfigurationOutput& output)
        {
            output.orientation = mir_orientation_inverted;
        });

    ASSERT_THAT(display.apply_if_configuration_preserves_display_buffers(*changed_conf), Eq(false));
    auto new_conf = display.configuration();

    EXPECT_THAT(*new_conf, Eq(std::cref(*preexisting_conf)));    // We need std::cref() to stop GTest trying to treat
                                                                 // preexisting_conf as a(n impossible) value rather than reference
}

TEST(MultiplexingDisplay, apply_if_configuration_preserves_display_buffers_throws_on_unrecoverable_error)
{
    DisplayConfigurationOutputGenerator gen;
    mg::DisplayConfigurationCardId card1{5}, card2{12};

    auto d1 = std::make_unique<NiceMock<mtd::MockDisplay>>();
    auto d2 = std::make_unique<NiceMock<mtd::MockDisplay>>();

    auto d1_conf = std::make_unique<mtd::StubDisplayConfig>(
        std::vector<mg::DisplayConfigurationOutput>{
            gen.generate_output(card1),
            gen.generate_output(card1)
        });

    auto d2_conf = std::make_unique<mtd::StubDisplayConfig>(
        std::vector<mg::DisplayConfigurationOutput>{
            gen.generate_output(card2),
            gen.generate_output(card2),
            gen.generate_output(card2)
        });

    ON_CALL(*d1, configuration())
        .WillByDefault(
            Invoke(
                [&d1_conf]()
                {
                    return d1_conf->clone();
                }));
    ON_CALL(*d2, configuration())
        .WillByDefault(
            Invoke(
                [&d2_conf]()
                {
                    return d2_conf->clone();
                }));

    // The first display will get two configuration requests…
    EXPECT_CALL(*d1, apply_if_configuration_preserves_display_buffers(IsConfigurationOfCard(card1)))
        .WillOnce(
            Invoke(
                [&d1_conf](auto const& config)
                {
                    auto real_config = dynamic_cast<mtd::StubDisplayConfig const&>(config);
                    d1_conf->outputs = real_config.outputs;
                    return true;        // The first will succeed, changing state…
                }))
        .WillOnce(
            Return(false));            // The second (to return to initial configuration) will fail
    EXPECT_CALL(*d2, apply_if_configuration_preserves_display_buffers(IsConfigurationOfCard(card2)))
        .WillOnce(Return(false));

    std::vector<std::unique_ptr<mg::Display>> displays;
    displays.push_back(std::move(d1));
    displays.push_back(std::move(d2));

    mtd::NullDisplayConfigurationPolicy policy;
    mg::MultiplexingDisplay display{std::move(displays), policy};
    auto preexisting_conf = display.configuration();
    auto changed_conf = display.configuration();

    changed_conf->for_each_output(
        [](mg::UserDisplayConfigurationOutput& output)
        {
            output.orientation = mir_orientation_inverted;
        });

    EXPECT_THROW(
        { display.apply_if_configuration_preserves_display_buffers(*changed_conf); },
        mg::Display::IncompleteConfigurationApplied);
}

TEST(MultiplexingDisplay, delegates_registering_configuration_change_handlers)
{
    std::vector<std::unique_ptr<mg::Display>> mock_displays;

    for (auto i = 0; i < 5 ; ++i)
    {
        auto mock_display = make_safe_mock_display();
        EXPECT_CALL(*mock_display, register_configuration_change_handler(_,_));
        mock_displays.push_back(std::move(mock_display));
    }

    mtd::NullDisplayConfigurationPolicy policy;
    mg::MultiplexingDisplay display{std::move(mock_displays), policy};

    mtd::MockMainLoop ml;
    display.register_configuration_change_handler(ml, [](){});
}

TEST(MultiplexingDisplay, delegates_pause_resume_to_underlying_platforms)
{
    std::vector<std::unique_ptr<mg::Display>> mock_displays;

    for (auto i = 0; i < 5 ; ++i)
    {
        auto mock_display = make_safe_mock_display();
        InSequence seq;
        EXPECT_CALL(*mock_display, pause);
        EXPECT_CALL(*mock_display, resume);
        mock_displays.push_back(std::move(mock_display));
    }

    mtd::NullDisplayConfigurationPolicy policy;
    mg::MultiplexingDisplay display{std::move(mock_displays), policy};

    display.pause();
    display.resume();
}

TEST(MultiplexingDisplay, applies_initial_display_configuration)
{
    DisplayConfigurationOutputGenerator gen;
    mg::DisplayConfigurationCardId const card1{5}, card2{2};

    auto d1 = std::make_unique<NiceMock<mtd::MockDisplay>>();
    auto d2 = std::make_unique<NiceMock<mtd::MockDisplay>>();

    auto d1_conf = std::make_unique<mtd::StubDisplayConfig>(
        std::vector<mg::DisplayConfigurationOutput>{
            gen.generate_output(card1),
            gen.generate_output(card1),
            gen.generate_output(card1),
            gen.generate_output(card1)
        });

    auto d2_conf = std::make_unique<mtd::StubDisplayConfig>(
        std::vector<mg::DisplayConfigurationOutput>{
            gen.generate_output(card2),
            gen.generate_output(card2),
            gen.generate_output(card2)
        });

    ON_CALL(*d1, configuration())
        .WillByDefault(
            Invoke(
                [&d1_conf]()
                {
                    return d1_conf->clone();
                }));
    ON_CALL(*d1, configure(_))
        .WillByDefault(
            [&d1_conf](mg::DisplayConfiguration const& conf)
            {
                // Save the new config
                d1_conf = std::unique_ptr<mtd::StubDisplayConfig>{
                    dynamic_cast<mtd::StubDisplayConfig*>(conf.clone().release())};
            });

    ON_CALL(*d2, configuration())
        .WillByDefault(
            Invoke(
                [&d2_conf]()
                {
                    return d2_conf->clone();
                }));
    ON_CALL(*d2, configure(_))
        .WillByDefault(
            [&d2_conf](mg::DisplayConfiguration const& conf)
            {
                // Save the new config
                d2_conf = std::unique_ptr<mtd::StubDisplayConfig>{
                    dynamic_cast<mtd::StubDisplayConfig*>(conf.clone().release())};
            });

    class IdentifyingDisplayConfigurationPolicy : public mg::DisplayConfigurationPolicy
    {
    public:
        void apply_to(mg::DisplayConfiguration& conf) override
        {
            conf.for_each_output(
                [this](mg::UserDisplayConfigurationOutput& output)
                {
                    output.top_left = next_top_left;
                    locations.emplace_back(
                        output.pixel_formats,
                        output.top_left);
                    next_top_left += delta;
                });
        }

        auto expected_location_for_display(mg::DisplayConfigurationOutput const& display) -> geom::Point
        {
            auto it = std::find_if(
                locations.begin(),
                locations.end(),
                [&display](auto const& candidate)
                {
                    return candidate.first == display.pixel_formats;
                });

            if (it == locations.end())
            {
                BOOST_THROW_EXCEPTION((std::out_of_range{"Attempt to look up output that has not been layed out?"}));
            }
            return it->second;
        }

    private:
        geom::Point next_top_left{0,0};
        geom::Displacement const delta{5, 25};
        std::vector<std::pair<std::vector<MirPixelFormat>, geom::Point>> locations;
    };
    auto policy = std::make_shared<IdentifyingDisplayConfigurationPolicy>();

    std::vector<std::unique_ptr<mg::Display>> displays;
    displays.push_back(std::move(d1));
    displays.push_back(std::move(d2));

    mg::MultiplexingDisplay display{std::move(displays), *policy};

    display.configuration()->for_each_output(
        [policy](mg::DisplayConfigurationOutput const& output)
        {
            EXPECT_THAT(output.top_left, Eq(policy->expected_location_for_display(output)));
        });
}
