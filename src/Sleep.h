#pragma once

// Reference-counted deep-sleep gate.
//
// Anything that has work in progress calls inhibit() before the work becomes
// pending and release() when it's finished. The task that decides to sleep
// calls waitUntilAllowed(), which blocks until the count is zero.
//
// inhibit() and release() are safe from tasks and ISRs. Only the task that
// called init() may call waitUntilAllowed().
namespace Sleep {
void init();
void inhibit();
void release();
void waitUntilAllowed();
}  // namespace Sleep
