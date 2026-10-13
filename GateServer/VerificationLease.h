#pragma once
#include "RedisMgr.h"
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>

// Serializes register/reset operations for one email across Gate processes.
// Failed account/update operations leave the code usable; success deletes it
// before releasing the lease. Late cleanup cannot remove a new owner's lease.
class VerificationLease final
{
  public:
    explicit VerificationLease(const std::string &codeKey)
        : key_("verification_lock_" + codeKey),
          owner_(boost::uuids::to_string(boost::uuids::random_generator()()))
    {
        result_ = RedisMgr::GetInstance()->AcquireVerification(key_, owner_);
    }
    ~VerificationLease()
    {
        if (result_ == VerificationLockResult::Acquired)
            RedisMgr::GetInstance()->ReleaseVerification(key_, owner_);
    }
    VerificationLease(const VerificationLease &) = delete;
    VerificationLease &operator=(const VerificationLease &) = delete;
    VerificationLockResult result() const { return result_; }

  private:
    std::string key_, owner_;
    VerificationLockResult result_;
};
