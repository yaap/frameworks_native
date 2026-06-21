/*
 * Copyright (C) 2008 The Android Open Source Project
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

#pragma once

#include <binder/Common.h>
#include <binder/IBinder.h>
#include <stdint.h>
#include <atomic>
#include <optional>

// ---------------------------------------------------------------------------
namespace android {

namespace internal {
class Stability;
}

struct TransactionCodeData {
    // Total size of the struct
    uint32_t totalSize;
    // caller backend type (cpp, ndk, rust, java)
    const char* backendType;

    // function names and count
    const char* const* names;
    uint32_t count;

    // Add fields below and check the totalSize before reading them
};

class BBinder : public IBinder {
public:
    LIBBINDER_EXPORTED BBinder();

    LIBBINDER_EXPORTED virtual const String16& getInterfaceDescriptor() const;
    LIBBINDER_EXPORTED virtual bool isBinderAlive() const;
    LIBBINDER_EXPORTED virtual status_t pingBinder();
    LIBBINDER_EXPORTED virtual status_t dump(int fd, const Vector<String16>& args);

    // NOLINTNEXTLINE(google-default-arguments)
    LIBBINDER_EXPORTED virtual status_t transact(uint32_t code, const Parcel& data, Parcel* reply,
                                                 uint32_t flags = 0) final;

    // NOLINTNEXTLINE(google-default-arguments)
    LIBBINDER_EXPORTED virtual status_t linkToDeath(const sp<DeathRecipient>& recipient,
                                                    void* cookie = nullptr, uint32_t flags = 0);

    // NOLINTNEXTLINE(google-default-arguments)
    LIBBINDER_EXPORTED virtual status_t unlinkToDeath(const wp<DeathRecipient>& recipient,
                                                      void* cookie = nullptr, uint32_t flags = 0,
                                                      wp<DeathRecipient>* outRecipient = nullptr);

    LIBBINDER_EXPORTED virtual void* attachObject(const void* objectID, void* object,
                                                  void* cleanupCookie,
                                                  object_cleanup_func func) final;
    LIBBINDER_EXPORTED virtual void* findObject(const void* objectID) const final;
    LIBBINDER_EXPORTED virtual void* detachObject(const void* objectID) final;
    LIBBINDER_EXPORTED void withLock(const std::function<void()>& doWithLock);
    LIBBINDER_EXPORTED sp<IBinder> lookupOrCreateWeak(const void* objectID,
                                                      IBinder::object_make_func make,
                                                      const void* makeArgs);

    LIBBINDER_EXPORTED virtual BBinder* localBinder();

    LIBBINDER_EXPORTED bool isRequestingSid();
    // This must be called before the object is sent to another process. Not thread safe.
    LIBBINDER_EXPORTED void setRequestingSid(bool requestSid);

    LIBBINDER_EXPORTED sp<IBinder> getExtension();
    // This must be called before the object is sent to another process. Not thread safe.
    LIBBINDER_EXPORTED void setExtension(const sp<IBinder>& extension);

    // This must be called before the object is sent to another process. Not thread safe.
    //
    // This function will abort if improper parameters are set. This is like
    // sched_setscheduler. However, it sets the minimum scheduling policy
    // only for the duration that this specific binder object is handling the
    // call in a threadpool. By default, this API is set to SCHED_NORMAL/0. In
    // this case, the scheduling priority will not actually be modified from
    // binder defaults. See also IPCThreadState::disableBackgroundScheduling.
    //
    // Appropriate values are:
    // SCHED_NORMAL: -20 <= priority <= 19
    // SCHED_RR/SCHED_FIFO: 1 <= priority <= 99
    LIBBINDER_EXPORTED void setMinSchedulerPolicy(int policy, int priority);
    LIBBINDER_EXPORTED int getMinSchedulerPolicy();
    LIBBINDER_EXPORTED int getMinSchedulerPriority();

    // Whether realtime scheduling policies are inherited.
    LIBBINDER_EXPORTED bool isInheritRt();
    // This must be called before the object is sent to another process. Not thread safe.
    LIBBINDER_EXPORTED void setInheritRt(bool inheritRt);

    // Default is 1. This is configured for each RpcSession attached to each
    // BBinder object in an RpcServer.
    LIBBINDER_EXPORTED void setMinRpcThreads(uint16_t min);
    LIBBINDER_EXPORTED uint16_t getMinRpcThreads() const;

    // Set default, overridden by setInheritRt. You must set this default early.
    // Any binder objects sent out of the process before this is called will
    // not use the updated value.
    LIBBINDER_EXPORTED static void setGlobalInheritRt(bool enabled);

    LIBBINDER_EXPORTED pid_t getDebugPid();

    // Whether this binder has been sent to another process.
    LIBBINDER_EXPORTED bool wasParceled();
    // Consider this binder as parceled (setup/init-related calls should no
    // longer by called. This is automatically set by when this binder is sent
    // to another process.
    LIBBINDER_EXPORTED void setParceled();

    [[nodiscard]] LIBBINDER_EXPORTED status_t setRpcClientDebug(binder::unique_fd clientFd,
                                                                const sp<IBinder>& keepAliveBinder);

    LIBBINDER_EXPORTED void setTransactionCodeMap(const TransactionCodeData* data);
    // Returns the function name OR "#<transactionCode>"
    // Example "foo" with transaction code 12
    // When we have the function name it returns "foo"
    // When we don't have the function name it returns "#12"
    LIBBINDER_EXPORTED std::string getFunctionName(size_t transactionCode);
    // Returns the function name and the code number. Useful for debugging and
    // logs.
    // Example "foo" with transaction code 12
    // returns "foo, code: 12"
    // or "UNKNOWN_FUNCTION_NAME, code: 12"
    LIBBINDER_EXPORTED std::string getFunctionNameAndCode(size_t transactionCode);

    class PrivateAccessor {
    public:
        friend class BinderTest;
        friend class BBinder;
        explicit PrivateAccessor(BBinder* binder) : mBinder(binder) {}
        void setStability(int16_t level) { mBinder->setStability(level); }
        int16_t getStability() const { return mBinder->getStability(); }
        BBinder* mBinder;
    };

    LIBBINDER_EXPORTED PrivateAccessor getPrivateAccessor() { return PrivateAccessor(this); }

protected:
    LIBBINDER_EXPORTED virtual ~BBinder();

    // NOLINTNEXTLINE(google-default-arguments)
    LIBBINDER_EXPORTED virtual status_t onTransact(uint32_t code, const Parcel& data, Parcel* reply,
                                                   uint32_t flags = 0);

private:
    friend class PrivateAccessor;
    LIBBINDER_EXPORTED void setStability(int16_t level);
    LIBBINDER_EXPORTED int16_t getStability() const;
    class PackedData {
    public:
        void setTransactionCodeMap(const TransactionCodeData* data);
        const TransactionCodeData* getTransactionCodeMap() const;

        void setStability(uintptr_t stability);
        uintptr_t getStability() const;

        void setParceled();
        bool isParceled() const;

    private:
        static constexpr size_t POINTER_ALIGNMENT = 16;
        static constexpr uintptr_t POINTER_MASK = ~(POINTER_ALIGNMENT - 1);

        static constexpr uintptr_t PARCELED_BIT = 1UL << 0;
        static constexpr int STABILITY_SHIFT = 1;
        static constexpr uintptr_t STABILITY_MASK = 0b11UL << STABILITY_SHIFT;
        /*
         * A packed pointer storing an address to TransactionCodeData and status flags.
         *
         * The data is 16-byte aligned, leaving the 4 least-significant bits (LSBs)
         * free to be used for other flags.
         *
         * 64-Bit Layout:
         * MSB                           LSB
         * 63                3   2   1   0
         * +-------------------+---+---+---+
         * |      Address   | R | S | S | P |
         * +-------------------+---+---+---+
         *
         * P (Bit 0)    : Parceled flag
         * S (Bits 2:1) : Stability (2 bits)
         * R (Bit 3     : Reserved for future use
         */
        std::atomic<uintptr_t> mPackedData{0};
    };

                        BBinder(const BBinder& o);
            BBinder&    operator=(const BBinder& o);

    class RpcServerLink;
    class Extras;

    Extras*             getOrCreateExtras();

    [[nodiscard]] status_t setRpcClientDebug(const Parcel& data);
    void removeRpcServerLink(const sp<RpcServerLink>& link);
    [[nodiscard]] status_t startRecordingTransactions(const Parcel& data);
    [[nodiscard]] status_t stopRecordingTransactions();
    [[nodiscard]] std::optional<std::string> tryGetFunctionName(size_t transactionCode);
    status_t getTraceName(uint32_t code, char* buffer, size_t bufferSize);
    [[nodiscard]] bool startTrace(uint32_t code);

    static std::atomic<bool> sGlobalInheritRt;

    std::atomic<Extras*> mExtras;

    friend ::android::internal::Stability;

    PackedData mPackedData;
};

// ---------------------------------------------------------------------------

class BpRefBase : public virtual RefBase {
protected:
    LIBBINDER_EXPORTED explicit BpRefBase(const sp<IBinder>& o);
    LIBBINDER_EXPORTED virtual ~BpRefBase();
    LIBBINDER_EXPORTED virtual void onFirstRef();
    LIBBINDER_EXPORTED virtual void onLastStrongRef(const void* id);
    LIBBINDER_EXPORTED virtual bool onIncStrongAttempted(uint32_t flags, const void* id);

    LIBBINDER_EXPORTED inline IBinder* remote() const { return mRemote; }
    LIBBINDER_EXPORTED inline sp<IBinder> remoteStrong() const {
        return sp<IBinder>::fromExisting(mRemote);
    }

private:
                            BpRefBase(const BpRefBase& o);
    BpRefBase&              operator=(const BpRefBase& o);

    IBinder* const          mRemote;
    RefBase::weakref_type*  mRefs;
    std::atomic<int32_t>    mState;
};

} // namespace android

// ---------------------------------------------------------------------------
