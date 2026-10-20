#pragma once

// 好友请求状态变更动作；Repository 负责检查操作者身份与请求当前状态。
enum class FriendRequestAction { Accept, Reject, Cancel };

enum class FriendRequestProcessResult {
    Success,
    AlreadyProcessed,
    NotFound,
    Forbidden,
    Conflict,
    InvalidAction
};
