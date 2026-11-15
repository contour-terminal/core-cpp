// SPDX-License-Identifier: Apache-2.0
#include <core/tui/Component.hpp>

#include <core/Assert.hpp>
#include <core/tui/Canvas.hpp>
#include <core/tui/Screen.hpp>
#include <core/tui/layout/Solve.hpp>

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <utility>

namespace core::tui
{

Component::Component(layout::Style style): _style(style)
{
}

Component::~Component()
{
    // A component the screen still knows when it dies -- an overlay never hidden, say -- tells the
    // screen first, so that neither the pointer capture nor the overlay list outlives it.
    if (_screen)
        _screen->componentDestroyed(*this);

    // Remove from parent if attached
    if (_parent)
        _parent->removeChild(*this);

    // Clear children (sets their parent to nullptr)
    clearChildren();
}

EventResult Component::onEvent([[maybe_unused]] InputEvent const& event)
{
    return EventResult::Ignored;
}

std::optional<HoverResult> Component::onHover(int /*x*/, int /*y*/)
{
    return std::nullopt;
}

void Component::setVisible(bool visible)
{
    if (_layout.visible != visible)
    {
        _layout.visible = visible;
        invalidate();
        relayoutParent();
    }
}

void Component::setArea(Rect area)
{
    if (_layout.area == area)
        return;

    auto const previous = _layout.area;
    _layout.area = area;
    invalidate();
    areaChanged(previous);
}

void Component::areaChanged(Rect previous)
{
    if (previous.size() == _layout.area.size())
        return;
    onResize(_layout.area.size());
    layoutChildren();
}

void Component::hintsChanged()
{
    if (!_parent || !_layout.flex)
        return;
    _parent->_layoutState = LayoutState::HintsChanged;
    if (_parent->_screen)
        _parent->_screen->layoutRequested();
}

void Component::layoutChildren()
{
    // One level of the tree at a time: a child's place in this component depends on its style and its
    // hints, never on its own children, so solving this level alone gives what solving the whole
    // subtree would. Each child that changes size then lays out its own children through setArea().
    _layoutState = LayoutState::Clean;
    auto placed = std::vector<Component*> {};
    auto root = layout::Node { .style = _style, .children = {}, .preferred = {} };
    for (Component* child: _children)
    {
        if (!child->_layout.flex || !child->visible())
            continue;
        auto item = *child->_layout.flex;
        auto const hint = child->minSize();
        item.minSize = { .width = std::max(item.minSize.width, hint.width),
                         .height = std::max(item.minSize.height, hint.height) };
        root.children.push_back(
            layout::Node { .style = item, .children = {}, .preferred = child->preferredSize() });
        placed.push_back(child);
    }
    if (placed.empty())
        return;

    // solve() answers in depth-first pre-order, the root first: child i is at i + 1.
    auto const rects = layout::solve(
        root, Rect { .x = 0, .y = 0, .width = _layout.area.width, .height = _layout.area.height });
    for (auto const index: std::views::iota(std::size_t { 0 }, placed.size()))
        placed[index]->setArea(rects[index + 1]);
}

void Component::relayoutParent()
{
    if (_parent && _layout.flex)
        _parent->layoutChildren();
}

void Component::setZIndex(int z)
{
    if (_layout.zIndex != z)
    {
        _layout.zIndex = z;
        invalidate();
    }
}

void Component::addChild(Component& child, LayoutParams params)
{
    // Remove from previous parent if any
    if (child._parent)
        child._parent->removeChild(child);

    // Set layout parameters; a new size runs what setArea() runs for one.
    auto const previousArea = child._layout.area;
    child._layout = std::move(params);

    // Add to our children
    _children.push_back(&child);
    child.setParent(this);

    // Propagate screen attachment
    if (_screen)
        child.setScreen(_screen);

    invalidate();
    child.areaChanged(previousArea);
    child.relayoutParent();
}

void Component::removeChild(Component& child)
{
    auto it = std::ranges::find(_children, &child);
    if (it != _children.end())
    {
        _children.erase(it);
        child.setParent(nullptr);
        child.setScreen(nullptr);
        invalidate();
        if (child._layout.flex)
            layoutChildren();
    }
}

void Component::clearChildren()
{
    for (Component* child: _children)
    {
        child->setParent(nullptr);
        child->setScreen(nullptr);
    }
    _children.clear();
    invalidate();
}

std::span<Component* const> Component::children() const noexcept
{
    return _children;
}

void Component::setFocusGroup(FocusGroupId group)
{
    _layout.focusGroup = std::move(group);
}

void Component::invalidate()
{
    if (_screen)
        _screen->invalidate(*this);
}

void Component::setParent(Component* parent)
{
    _parent = parent;
}

void Component::setScreen(Screen* screen)
{
    // Leaving a screen -- removed from its tree, directly or with an ancestor, or hidden as an overlay --
    // ends that screen's pointer capture on this component.
    if (_screen && _screen != screen)
        _screen->componentDetached(*this);

    _screen = screen;

    // Propagate to children
    for (Component* child: _children)
        child->setScreen(screen);
}

void Component::setFocused(bool focused)
{
    if (_focused != focused)
    {
        _focused = focused;
        if (focused)
            onFocus();
        else
            onBlur();
    }
}

void Component::setScreenBounds(Rect bounds)
{
    _screenBounds = bounds;
}

// --- RootComponent ---

void RootComponent::render([[maybe_unused]] Canvas& canvas)
{
    // RootComponent doesn't render anything itself.
    // Children are rendered by Screen::renderTree().
}

Size RootComponent::preferredSize() const
{
    // Root takes full screen, so preferred size is not meaningful.
    return { .width = 0, .height = 0 };
}

// --- FlexBox ---

FlexBox::FlexBox(layout::Style style): Component(style)
{
}

FlexBox::~FlexBox()
{
    // Detach every child first, so the owned ones do not each lay out the rest as they go.
    clearChildren();
}

Component& FlexBox::add(std::unique_ptr<Component> child, layout::Style item)
{
    Require(child != nullptr);
    auto& added = *_owned.emplace_back(std::move(child));
    addChild(added, { .flex = item });
    return added;
}

void FlexBox::render([[maybe_unused]] Canvas& canvas)
{
    // A FlexBox only places its children; Screen::renderTree() renders them.
}

} // namespace core::tui
