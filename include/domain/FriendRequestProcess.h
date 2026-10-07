#pragma once

enum class FriendRequestAction { Accept, Reject, Cancel };

enum class FriendRequestProcessResult {
    Success,
    AlreadyProcessed,
    NotFound,
    Forbidden,
    Conflict,
    InvalidAction
};
