#pragma once
#include <functional>
#include <variant>
#include <vector>

namespace core::mvc
{

template <typename EventVariant>
class Controller
{
    using HandlerMap = std::vector<std::function<bool(EventVariant const&)>>;
    HandlerMap handlers;

  public:
    virtual ~Controller() = default;

    template <typename SpecificEvent, typename Func>
    void on(Func&& handler)
    {
        handlers.push_back([h = std::forward<Func>(handler)](EventVariant const& e) {
            if (auto* p = std::get_if<SpecificEvent>(&e))
            {
                h(*p);
                return true;
            }
            return false;
        });
    }

    void dispatch(EventVariant const& e)
    {
        for (auto& h: handlers)
        {
            h(e);
        }
    }
};

} // namespace core::mvc
