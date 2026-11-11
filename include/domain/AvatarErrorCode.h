#pragma once

enum class AvatarErrorCode {
    InvalidSize,
    ParseFailed,
    DataBaseError,
    DatabaseUnavailable,
    DecoderUnavailable,
    StorageFailed,
    UserUnavailable
};
