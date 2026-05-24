#include "cget/core/task_state.h"

#include "cget/core/errors.h"

namespace cget {

bool isValidTransition(TaskStatus current, TaskStatus next) {
    if (current == next) {
        return true;
    }

    switch (current) {
        case TaskStatus::Created:
            return next == TaskStatus::Pending || next == TaskStatus::Queued || next == TaskStatus::Failed;
        case TaskStatus::Pending:
            return next == TaskStatus::Queued || next == TaskStatus::Paused || next == TaskStatus::Failed;
        case TaskStatus::Queued:
            return next == TaskStatus::Downloading || next == TaskStatus::Paused || next == TaskStatus::Cancelled ||
                   next == TaskStatus::Failed;
        case TaskStatus::Downloading:
            return next == TaskStatus::Paused || next == TaskStatus::Retrying || next == TaskStatus::Failed ||
                   next == TaskStatus::Completed || next == TaskStatus::Cancelled ||
                   next == TaskStatus::PendingRecovery || next == TaskStatus::MetadataMismatch;
        case TaskStatus::Retrying:
            return next == TaskStatus::Queued || next == TaskStatus::Downloading || next == TaskStatus::Failed ||
                   next == TaskStatus::Paused || next == TaskStatus::PendingRecovery;
        case TaskStatus::Paused:
            return next == TaskStatus::Queued || next == TaskStatus::Downloading || next == TaskStatus::Removed ||
                   next == TaskStatus::Failed;
        case TaskStatus::Failed:
            return next == TaskStatus::Queued || next == TaskStatus::Removed;
        case TaskStatus::Completed:
            return next == TaskStatus::Removed;
        case TaskStatus::Cancelled:
            return next == TaskStatus::Removed;
        case TaskStatus::Corrupted:
            return next == TaskStatus::Removed;
        case TaskStatus::PendingRecovery:
            return next == TaskStatus::Paused || next == TaskStatus::Queued || next == TaskStatus::Removed ||
                   next == TaskStatus::MetadataMismatch || next == TaskStatus::Failed;
        case TaskStatus::MetadataMismatch:
            return next == TaskStatus::Removed || next == TaskStatus::Failed;
        case TaskStatus::Removed:
            return false;
    }
    return false;
}

void requireValidTransition(TaskStatus current, TaskStatus next) {
    if (!isValidTransition(current, next)) {
        throw CgetError(ErrorCode::InvalidStateTransitionError,
                       "invalid task state transition from " + toString(current) + " to " + toString(next));
    }
}

}  // namespace cget
