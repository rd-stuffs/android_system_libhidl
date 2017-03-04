/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_HARDWARE_BINDER_STATUS_H
#define ANDROID_HARDWARE_BINDER_STATUS_H

#include <cstdint>
#include <sstream>

#include <android-base/macros.h>
#include <utils/Errors.h>
#include <utils/StrongPointer.h>

namespace android {
namespace hardware {

// An object similar in function to a status_t except that it understands
// how exceptions are encoded in the prefix of a Parcel. Used like:
//
//     Parcel data;
//     Parcel reply;
//     status_t status;
//     binder::Status remote_exception;
//     if ((status = data.writeInterfaceToken(interface_descriptor)) != OK ||
//         (status = data.writeInt32(function_input)) != OK) {
//         // We failed to write into the memory of our local parcel?
//     }
//     if ((status = remote()->transact(transaction, data, &reply)) != OK) {
//        // Something has gone wrong in the binder driver or libbinder.
//     }
//     if ((status = remote_exception.readFromParcel(reply)) != OK) {
//         // The remote didn't correctly write the exception header to the
//         // reply.
//     }
//     if (!remote_exception.isOk()) {
//         // The transaction went through correctly, but the remote reported an
//         // exception during handling.
//     }
//
class Status final {
public:
    // Keep the exception codes in sync with android/os/Parcel.java.
    enum Exception {
        EX_NONE = 0,
        EX_SECURITY = -1,
        EX_BAD_PARCELABLE = -2,
        EX_ILLEGAL_ARGUMENT = -3,
        EX_NULL_POINTER = -4,
        EX_ILLEGAL_STATE = -5,
        EX_NETWORK_MAIN_THREAD = -6,
        EX_UNSUPPORTED_OPERATION = -7,
        EX_SERVICE_SPECIFIC = -8,

        // This is special and Java specific; see Parcel.java.
        EX_HAS_REPLY_HEADER = -128,
        // This is special, and indicates to C++ binder proxies that the
        // transaction has failed at a low level.
        EX_TRANSACTION_FAILED = -129,
    };

    // A more readable alias for the default constructor.
    static Status ok();
    // Authors should explicitly pick whether their integer is:
    //  - an exception code (EX_* above)
    //  - service specific error code
    //  - status_t
    //
    //  Prefer a generic exception code when possible, then a service specific
    //  code, and finally a status_t for low level failures or legacy support.
    //  Exception codes and service specific errors map to nicer exceptions for
    //  Java clients.
    static Status fromExceptionCode(int32_t exceptionCode);
    static Status fromExceptionCode(int32_t exceptionCode,
                                    const char *message);
    static Status fromServiceSpecificError(int32_t serviceSpecificErrorCode);
    static Status fromServiceSpecificError(int32_t serviceSpecificErrorCode,
                                           const char *message);
    static Status fromStatusT(status_t status);

    Status() = default;
    ~Status() = default;

    // Status objects are copyable and contain just simple data.
    Status(const Status& status) = default;
    Status(Status&& status) = default;
    Status& operator=(const Status& status) = default;

    // Set one of the pre-defined exception types defined above.
    void setException(int32_t ex, const char *message);
    // Set a service specific exception with error code.
    void setServiceSpecificError(int32_t errorCode, const char *message);
    // Setting a |status| != OK causes generated code to return |status|
    // from Binder transactions, rather than writing an exception into the
    // reply Parcel.  This is the least preferable way of reporting errors.
    void setFromStatusT(status_t status);

    // Get information about an exception.
    int32_t exceptionCode() const  { return mException; }
    const char *exceptionMessage() const { return mMessage.c_str(); }
    status_t transactionError() const {
        return mException == EX_TRANSACTION_FAILED ? mErrorCode : OK;
    }
    int32_t serviceSpecificErrorCode() const {
        return mException == EX_SERVICE_SPECIFIC ? mErrorCode : 0;
    }

    bool isOk() const { return mException == EX_NONE; }

    // For debugging purposes only
    std::string description() const;

private:
    Status(int32_t exceptionCode, int32_t errorCode);
    Status(int32_t exceptionCode, int32_t errorCode, const char *message);

    // If |mException| == EX_TRANSACTION_FAILED, generated code will return
    // |mErrorCode| as the result of the transaction rather than write an
    // exception to the reply parcel.
    //
    // Otherwise, we always write |mException| to the parcel.
    // If |mException| !=  EX_NONE, we write |mMessage| as well.
    // If |mException| == EX_SERVICE_SPECIFIC we write |mErrorCode| as well.
    int32_t mException = EX_NONE;
    int32_t mErrorCode = 0;
    std::string mMessage;
};  // class Status

// For gtest output logging
std::ostream& operator<< (std::ostream& stream, const Status& s);

namespace details {
    class return_status {
    private:
        Status mStatus {};
        mutable bool mCheckedStatus = false;
    protected:
        void assertOk() const;
    public:
        return_status() {}
        return_status(Status s) : mStatus(s) {}

        return_status(const return_status &) = delete;
        return_status &operator=(const return_status &) = delete;

        return_status(return_status &&other) {
            *this = std::move(other);
        }
        return_status &operator=(return_status &&other);

        ~return_status();

        bool isOk() const {
            mCheckedStatus = true;
            return mStatus.isOk();
        }

        // For debugging purposes only
        std::string description() const {
            // Doesn't consider checked.
            return mStatus.description();
        }
    };
}  // namespace details

template<typename T> class Return : public details::return_status {
private:
    T mVal {};
public:
    Return(T v) : details::return_status(), mVal{v} {}
    Return(Status s) : details::return_status(s) {}

    // move-able.
    // precondition: "this" has checked status
    // postcondition: other is safe to destroy after moving to *this.
    Return(Return &&other) = default;
    Return &operator=(Return &&) = default;

    ~Return() = default;

    operator T() const {
        assertOk();
        return mVal;
    }

};

template<typename T> class Return<sp<T>> : public details::return_status {
private:
    sp<T> mVal {};
public:
    Return(sp<T> v) : details::return_status(), mVal{v} {}
    Return(T* v) : details::return_status(), mVal{v} {}
    // Constructors matching a different type (that is related by inheritance)
    template<typename U> Return(sp<U> v) : details::return_status(), mVal{v} {}
    template<typename U> Return(U* v) : details::return_status(), mVal{v} {}
    Return(Status s) : details::return_status(s) {}

    // move-able.
    // precondition: "this" has checked status
    // postcondition: other is safe to destroy after moving to *this.
    Return(Return &&other) = default;
    Return &operator=(Return &&) = default;

    ~Return() = default;

    operator sp<T>() const {
        assertOk();
        return mVal;
    }
};


template<> class Return<void> : public details::return_status {
public:
    Return() : details::return_status() {}
    Return(Status s) : details::return_status(s) {}

    // move-able.
    // precondition: "this" has checked status
    // postcondition: other is safe to destroy after moving to *this.
    Return(Return &&) = default;
    Return &operator=(Return &&) = default;

    ~Return() = default;
};

static inline Return<void> Void() {
    return Return<void>();
}

}  // namespace hardware
}  // namespace android

#endif // ANDROID_HARDWARE_BINDER_STATUS_H
