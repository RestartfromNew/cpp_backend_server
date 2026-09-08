//
// Created by yangb on 2026/9/8.
//

#include "handler//Router.h"

#include <utility>

void Router::addRoute(
    HttpMethod method,
    std::string path,
    Handler handler
)
{
    routes_.push_back(
        Route{
            .method = method,
            .path = std::move(path),
            .handler = std::move(handler)
        }
    );
}

bool Router::route(const HttpRequest& request) const
{
    for (const auto& route : routes_) {
        if (
            route.method == request.method &&
            route.path == request.path
        ) {
            route.handler(request);
            return true;
        }
    }

    return false;
}
