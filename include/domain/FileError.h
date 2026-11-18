#pragma once

enum class FileError {
    NoSuchFile,
    PermissionDenied,
    FileExpired,
    FileDeleted,
    FileNotReady,
    LookupFailed
};
