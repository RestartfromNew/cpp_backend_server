//
// Created by yangb on 2026/9/8.
//

#include "handler/Router.h"
#include "http/HttpResponse.h"
#include <utility>
Router::Router(const AuthMiddleWare &authMiddleWare):authMiddleWare_(authMiddleWare) {}
void Router::addRoute(HttpMethod method,std::string path,Handler handler)
{
    routes_.push_back(Route{
            .method = method,
            .path = std::move(path),
            .handler = std::move(handler)
        }
    );
}
void Router::addProtectedRoute(HttpMethod method,std::string path,Protected_Handler handler) {
    protected_routes_.push_back(Protected_Route{
        .method = method,
        .path = std::move(path),
        .protected_handler = std::move(handler)
    });
}
HttpResponse Router::route(const HttpRequest& request) const
{
    for (const auto& route : routes_) {
        if (route.method == request.method &&route.path == request.path) {
            return route.handler(request);
        }
    }

    return protected_route(request);
}

HttpResponse Router::protected_route(const HttpRequest& request) const {
    for (const auto& route : protected_routes_) {
        if (route.method == request.method &&route.path == request.path) {
           const auto authResult=authMiddleWare_.authenticate(request);
            if (std::holds_alternative<AuthError>(authResult.result)) {
                const auto& authError=std::get<AuthError>(authResult.result);
                if (authError==AuthError::UnAuthorized)
                    return ErrorResponseMaker(HttpStatus::Unauthorized,"invalid_access_token","Invalid access token");
                if (authError==AuthError::Expired)
                    return ErrorResponseMaker(HttpStatus::Unauthorized,"access_token_expired","Access token expired");
            }
            const boost::uuids::uuid authId=std::get<boost::uuids::uuid>(authResult.result);
            return route.protected_handler(request,authId);

        }
    }
    return ErrorResponseMaker(HttpStatus::Not_Found,"route_not_found","Route not found");

}
