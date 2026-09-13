//
// Created by yangb on 2026/9/8.
//

#include "handler/Router.h"
#include "http/HttpResponse.h"
#include <utility>

void Router::addRoute(HttpMethod method,std::string path,Handler handler)
{
    routes_.push_back(Route{
            .method = method,
            .path = std::move(path),
            .handler = std::move(handler)
        }
    );
}

HttpResponse Router::route(const HttpRequest& request) const
{
    for (const auto& route : routes_) {
        if (route.method == request.method &&route.path == request.path) {
            return route.handler(request);
        }
    }

    return ErrorResponseMaker(HttpStatus::Not_Found,"route_not_found","Route not found");
}
