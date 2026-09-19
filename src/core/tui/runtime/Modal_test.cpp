// SPDX-License-Identifier: Apache-2.0
#include <core/tui/Canvas.hpp>
#include <core/tui/InputEvent.hpp>
#include <core/tui/runtime/Modal.hpp>
#include <core/tui/runtime/TuiRuntime.hpp>
#include <core/tui/runtime/testing/MockEventSource.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <variant>

using core::tui::Canvas;
using core::tui::InputEvent;
using core::tui::KeyEvent;
using core::tui::runtime::ModalComponent;
using core::tui::runtime::runModal;
using core::tui::runtime::TuiRuntime;
using core::tui::runtime::testing::MockEventSource;

namespace
{

/// A trivial modal that completes with the codepoint of the first key it sees.
class KeyCaptureModal: public ModalComponent<char32_t>
{
  public:
    void render(Canvas& /*canvas*/) override {}

    [[nodiscard]] std::optional<char32_t> step(InputEvent const& event) override
    {
        if (auto const* key = std::get_if<KeyEvent>(&event))
            return key->codepoint;
        return std::nullopt;
    }
};

} // namespace

TEST_CASE("runModal resolves with the modal's result", "[Modal]")
{
    auto source = MockEventSource {};
    source.pushEvents({ InputEvent { KeyEvent { .codepoint = U'q' } } });
    auto runtime = TuiRuntime { source };
    auto modal = KeyCaptureModal {};

    auto const result = runtime.blockOn(runModal(&runtime, &modal));

    REQUIRE(result.has_value());
    REQUIRE(*result == U'q');
}

TEST_CASE("runModal returns nullopt when cancelled", "[Modal]")
{
    auto source = MockEventSource {};
    source.pushInterrupt();
    auto runtime = TuiRuntime { source };
    auto modal = KeyCaptureModal {};

    auto const result = runtime.blockOn(runModal(&runtime, &modal));

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("runModal keeps stepping until the modal completes", "[Modal]")
{
    auto source = MockEventSource {};
    // First a mouse move (ignored by the modal), then the key that resolves it.
    source.pushEvents({ InputEvent { core::tui::MouseEvent { .type = core::tui::MouseEvent::Type::Move } } });
    source.pushEvents({ InputEvent { KeyEvent { .codepoint = U'k' } } });
    auto runtime = TuiRuntime { source };
    auto modal = KeyCaptureModal {};

    auto const result = runtime.blockOn(runModal(&runtime, &modal));

    REQUIRE(result.has_value());
    REQUIRE(*result == U'k');
}
