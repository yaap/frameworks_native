/*
 * Copyright 2025 The Android Open Source Project
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

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <android-base/expected.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include "test_framework/core/BufferId.h"
#include "test_framework/core/ConstevaledTypeName.h"
#include "test_framework/core/DisplayConfiguration.h"
#include "test_framework/core/ScenarioEventRecorder.h"
#include "test_framework/core/ScenarioEventValidation.h"
#include "test_framework/hwc3/events/BufferPendingDisplay.h"
#include "test_framework/hwc3/events/BufferPendingRelease.h"
#include "test_framework/hwc3/events/DisplayPresented.h"
#include "test_framework/surfaceflinger/events/BufferReleased.h"
#include "test_framework/surfaceflinger/events/TransactionCommitted.h"
#include "test_framework/surfaceflinger/events/TransactionCompleted.h"
#include "test_framework/surfaceflinger/events/TransactionInitiated.h"

template <typename U, typename V>
struct std::hash<std::pair<U, V>> {
    constexpr auto operator()(const std::pair<U, V>& value) const -> size_t {
        return std::hash<U>()(value.first) ^ (std::hash<V>()(value.second) << 1);
    }
};

namespace android::surfaceflinger::tests::end2end::test_framework::core {
namespace {

// True if the first type `U` is in the remaining `Ts...`
template <typename U, typename... Ts>
constexpr bool parameterPackContainsType = (... || std::is_same_v<U, Ts>);

static_assert(!parameterPackContainsType<void*, int, void>);
static_assert(parameterPackContainsType<int, int, void>);
static_assert(parameterPackContainsType<void, int, void>);

// True if the value of a variant is type `U`.
// Returns false if the variant isn't capable of holding a value of type `U`, unlike
// `std::holds_alternative`.
//
// The variant is not expected to be able to hold a value of type `std::nullopt_t`. This function
// will automatically return false for that type.
//
// Usage:
//
//   std::variant<Ts...> value;
//   return is<int>(value);
//
template <typename U, typename... Vs>
[[nodiscard]] constexpr auto is(const std::variant<Vs...>& value) -> bool {
    // Explicitly disallow std::nullopt_t
    if constexpr (std::is_same_v<U, std::nullopt_t>) {
        // Raise a compile error if the variant happens to hold that type (unexpected)
        static_assert(!parameterPackContainsType<std::nullopt_t, Vs...>);
        return false;
    }

    // holds_alternative cannot be used if `U` is not in `Vs...`
    if constexpr (parameterPackContainsType<U, Vs...>) {
        return std::holds_alternative<U>(value);
    }
    return false;
}

// True if the value of an optional variant is type `U`.
//
// If `U` is `std::nullopt_t`, it tests if the outer optional is nullopt. Otherwise it tests if the
// inner variant holds a value of type `U`. If the inner variant isn't capable of holding a value of
// type `U`, this returns false.
//
// Usage:
//
//   std::optional<std::variant<Ts...>> value;
//   return is<int>(value);
//
template <typename U, typename... Vs>
[[nodiscard]] constexpr auto is(const std::optional<std::variant<Vs...>>& value) -> bool {
    // Handle std::nullopt_t up front, as it only applies to the outer optional.
    if constexpr (std::is_same_v<U, std::nullopt_t>) {
        return !value;
    }
    return value && is<U>(*value);
}

static_assert(!is<int>(std::optional<std::variant<int>>()));
static_assert(is<std::nullopt_t>(std::optional<std::variant<int>>()));
static_assert(is<int>(std::optional<std::variant<int>>(1)));
static_assert(!is<int*>(std::optional<std::variant<int>>()));
static_assert(!is<int*>(std::optional<std::variant<int>>(1)));

// Like is the `is<U>(variant)` above, except returns true if the variant value is any
// one of a list of types `Us...`
//
// Usage:
//
//   std::variant<Ts...> value;
//   // Equivalent to `is<int>(value) || is<int*>(value)`
//   return isAnyOf<int, int*>(value);
template <typename... Us, typename... Vs>
[[nodiscard]] constexpr auto isAnyOf(const std::variant<Vs...>& value) -> bool {
    return (... || is<Us>(value));
}

// Like is the `is<U>(optional<variant>)` above, except returns true if the optional variant is any
// one of a list of types `Us...`
//
// Usage:
//
//   std::optional<std::variant<Ts...>> value;
//   // Equivalent to `is<int>(value) || is<int*>(value) || is<std::nullopt_t>(value)`
//   return isAnyOf<int, int*, std::nullopt>(value);
template <typename... Us, typename... Vs>
[[nodiscard]] constexpr auto isAnyOf(const std::optional<std::variant<Vs...>>& value) -> bool {
    // Handle std::nullopt_t up front, as it only applies to the outer optional.
    if constexpr (parameterPackContainsType<std::nullopt_t, Us...>) {
        if (!value) {
            return true;
        }
    }

    return value && (... || is<Us>(*value));
}

static_assert(!isAnyOf<int, void*>(std::optional<std::variant<int, void*>>()));
static_assert(isAnyOf<int, std::nullopt_t>(std::optional<std::variant<int, void*>>()));
static_assert(isAnyOf<int, std::nullopt_t>(std::optional<std::variant<int, void*>>(1)));
static_assert(!isAnyOf<void*, std::nullopt_t>(std::optional<std::variant<int, void*>>(1)));
static_assert(isAnyOf<void*, std::nullopt_t>(std::optional<std::variant<int, void*>>(nullptr)));
static_assert(!isAnyOf<int*>(std::optional<std::variant<int, void*>>()));
static_assert(!isAnyOf<int*>(std::optional<std::variant<int, void*>>(1)));
static_assert(!isAnyOf<int*>(std::optional<std::variant<int, void*>>(nullptr)));

// Returns a pointer to the contained value of the variant, if the value has type `U`.
//
// If `U` is `std::nullopt_t`, the return value is `&std::nullopt` if the outer optional is empty,
// otherwise it will return nullptr.
//
// For any other type, the return value will be a pointer to the value in the variant if the current
// type it store matches `U`, otherwise it will be nullptr. Like `std::get_if<U>` however, the type
// `U` should only occur in the typelist for the variant once, as otherwise this is ill-formed.
//
// If the inner variant
//
// Usage:
//
//   std::optional<std::variant<Ts...>> value;
//   // nullptr if `!is<int>(value)`, otherwise a pointer to the contained int.
//   const int* r = as<int>(value);
//
template <typename U, typename... Vs>
[[nodiscard]] constexpr auto as(const std::optional<std::variant<Vs...>>& value) -> const U* {
    if constexpr (std::is_same_v<U, std::nullopt_t>) {
        return !value ? &std::nullopt : nullptr;
    }

    // get_if<U> cannot be used if `U` is not in `Vs...`
    if constexpr (parameterPackContainsType<U, Vs...>) {
        if (value) {
            return std::get_if<U>(&*value);
        }
    }

    return nullptr;
}

template <typename U, typename... Vs>
[[nodiscard]] constexpr auto as(const std::variant<Vs...>* value) -> const U* {
    // Explicitly disallow std::nullopt_t
    if constexpr (std::is_same_v<U, std::nullopt_t>) {
        // Raise a compile error if the variant happens to hold that type (unexpected)
        static_assert(!parameterPackContainsType<std::nullopt_t, Vs...>);
        return nullptr;
    }

    // get_if<U> cannot be used if `U` is not in `Vs...`
    if constexpr (parameterPackContainsType<U, Vs...>) {
        if (value) {
            return std::get_if<U>(value);
        }
    }

    return nullptr;
}

// Compile time pointer checks are limited, so this is only a partial test
static_assert(as<std::nullopt_t>(std::optional<std::variant<int>>{}) == &std::nullopt);
static_assert(as<std::nullopt_t>(std::optional<std::variant<int>>{1}) == nullptr);
static_assert(as<int*>(std::optional<std::variant<int>>{1}) == nullptr);

template <typename... Vs>
[[nodiscard]] constexpr auto toString(const std::variant<Vs...>& value) -> std::string {
    return std::visit([](const auto& value) { return toString(value); }, value);
}

template <typename... Vs>
[[nodiscard]] constexpr auto toString(const std::optional<std::variant<Vs...>>& value)
        -> std::string {
    return value ? toString(*value) : "(none)";
}

using namespace std::string_view_literals;

using InputEvent = ScenarioEventRecorder::Event;
using ResultType = base::expected<void, std::string>;

using DisplayId = DisplayConfiguration::Id;

using SfTransactionInitiated = test_framework::surfaceflinger::events::TransactionInitiated;
using SfTransactionCommitted = test_framework::surfaceflinger::events::TransactionCommitted;
using SfTransactionCompleted = test_framework::surfaceflinger::events::TransactionCompleted;
using SfBufferReleased = test_framework::surfaceflinger::events::BufferReleased;

using HwcBufferPendingDisplay = test_framework::hwc3::events::BufferPendingDisplay;
using HwcBufferPendingRelease = test_framework::hwc3::events::BufferPendingRelease;
using HwcDisplayPresented = test_framework::hwc3::events::DisplayPresented;

// Extracts the (surfaceId, frameNumber) data from events that have them.
struct ExtractSurfaceIdAndFrameNumber {
    using Type = std::pair<intptr_t, int64_t>;
    constexpr auto operator()(const auto& event) -> Type {
        return std::make_pair(event.surfaceId, event.frameNumber);
    }
};

// Extracts the bufferId data from events that have it.
struct ExtractBufferId {
    using Type = BufferId;
    constexpr auto operator()(const auto& event) -> Type { return event.bufferId; }
};

// Extracts the latchTime data from events that have it.
struct ExtractLatchTime {
    using Type = std::chrono::steady_clock::time_point;
    constexpr auto operator()(const auto& event) -> Type { return event.latchTime; }
};

class ResultGenerator {
  protected:
    constexpr auto makeResult(this const auto& self, const auto& lastEvent, const auto& nextEvent,
                              std::vector<std::string> mismatches) -> ResultType {
        ResultType result;
        if (!mismatches.empty()) {
            result = base::unexpected(
                    fmt::format("{} -> {} : [{}] {}"sv, toString(lastEvent), toString(nextEvent),
                                shortTypeNameOf<std::remove_cvref_t<decltype(self)>>(),
                                fmt::join(mismatches, ", "sv)));
        }
        return result;
    }
};

template <typename R>
class RecordStorage {
  public:
    using Record = R;
    using KeyMapper = typename R::Key;
    using ValueMapper = typename R::Value;
    using Key = typename R::Key::Type;
    using InputEvent = typename R::InputEvent;

    constexpr auto operator()(const auto& event) -> ResultType {
        if constexpr (std::is_constructible_v<InputEvent, decltype(event)>) {
            const auto key = KeyMapper{}(event);
            auto& record = mMap[key];

            if constexpr (!std::is_same_v<ValueMapper, void>) {
                return record(InputEvent{event}, ValueMapper{}(event));
            } else {
                return record(InputEvent{event});
            }
        }
        return {};
    }

    constexpr auto operator()() -> ResultType {
        if constexpr (std::is_invocable_v<R>) {
            for (const auto& [key, record] : mMap) {
                ResultType result = record();
                if (!result) {
                    return result;
                }
            }
        }
        return {};
    }

  private:
    std::unordered_map<Key, Record> mMap;
};

template <typename... Checks>
class CompositionalValidator : RecordStorage<Checks>... {
    template <typename U, typename... Us>
    auto base(const auto& event) -> ResultType {
        ResultType result = static_cast<RecordStorage<U>*>(this)->operator()(event);
        if (!result) {
            return result;
        }
        if constexpr (sizeof...(Us) > 0) {
            result = base<Us...>(event);
        }
        return result;
    }

    template <typename U, typename... Us>
    auto base() -> ResultType {
        ResultType result = static_cast<RecordStorage<U>*>(this)->operator()();
        if (!result) {
            return result;
        }
        if constexpr (sizeof...(Us) > 0) {
            result = base<Us...>();
        }
        return result;
    }

  public:
    auto next(const InputEvent& event) -> ResultType {
        return std::visit(
                [this](const auto& event) -> ResultType {
                    return this->template base<Checks...>(event);
                },
                event);
    }

    auto finalize() -> ResultType { return base<Checks...>(); }
};

class SurfaceTransactionSequenceOrder : ResultGenerator {
  public:
    using InputEvent =
            std::variant<SfTransactionInitiated, SfTransactionCommitted, SfTransactionCompleted>;
    using Key = ExtractSurfaceIdAndFrameNumber;
    using Value = void;

    constexpr auto operator()(const InputEvent& nextEvent) -> ResultType {
        const auto lastEvent = std::exchange(mLastEvent, nextEvent);

        std::vector<std::string> mismatches;

        if (lastEvent && is<SfTransactionInitiated>(nextEvent)) {
            mismatches.emplace_back("unexpected initiate"sv);
        }

        if (!lastEvent && !is<SfTransactionInitiated>(nextEvent)) {
            mismatches.emplace_back("expected initiate"sv);
        }

        if (is<SfTransactionInitiated>(lastEvent) && !is<SfTransactionCommitted>(nextEvent)) {
            mismatches.emplace_back("expected commit"sv);
        }

        if (is<SfTransactionCommitted>(lastEvent) && !is<SfTransactionCompleted>(nextEvent)) {
            mismatches.emplace_back("expected complete"sv);
        }

        return makeResult(lastEvent, nextEvent, mismatches);
    }

  private:
    std::optional<InputEvent> mLastEvent;
};

class SurfaceTransactionConsistentBufferId : ResultGenerator {
  public:
    using InputEvent =
            std::variant<SfTransactionInitiated, SfTransactionCommitted, SfTransactionCompleted>;
    using Key = ExtractSurfaceIdAndFrameNumber;
    using Value = ExtractBufferId;

    constexpr auto operator()(const InputEvent& nextEvent, Value::Type nextBufferId) -> ResultType {
        const auto lastEvent = std::exchange(mLastEvent, nextEvent);
        const auto lastBufferId = std::exchange(mLastBufferId, nextBufferId);

        std::vector<std::string> mismatches;

        if (lastBufferId && lastBufferId != nextBufferId) {
            mismatches.emplace_back("bufferId mismatch"sv);
        }

        return makeResult(lastEvent, nextEvent, mismatches);
    }

  private:
    std::optional<InputEvent> mLastEvent;
    std::optional<BufferId> mLastBufferId;
};

class SurfaceTransactionConsistentLatchTime : ResultGenerator {
  public:
    using InputEvent = std::variant<SfTransactionCommitted, SfTransactionCompleted>;
    using Key = ExtractSurfaceIdAndFrameNumber;
    using Value = ExtractLatchTime;

    constexpr auto operator()(const InputEvent& nextEvent, Value::Type nextLatchTime)
            -> ResultType {
        const auto lastEvent = std::exchange(mLastEvent, nextEvent);
        const auto lastLatchTime = std::exchange(mLastLatchTime, nextLatchTime);

        std::vector<std::string> mismatches;

        if (lastLatchTime && lastLatchTime != nextLatchTime) {
            mismatches.emplace_back("latchTime mismatch"sv);
        }

        return makeResult(lastEvent, nextEvent, mismatches);
    }

  private:
    std::optional<InputEvent> mLastEvent;
    std::optional<Value::Type> mLastLatchTime;
};

class BufferDisplayAndReleaseLifecycle : ResultGenerator {
  public:
    using InputEvent =
            std::variant<SfBufferReleased, HwcBufferPendingDisplay, HwcBufferPendingRelease>;
    using Key = ExtractBufferId;
    using Value = ExtractBufferId;

    constexpr auto operator()(const InputEvent& nextEvent, Value::Type nextBufferId) -> ResultType {
        const auto lastEvent = std::exchange(mLastEvent, nextEvent);

        if (const auto* display = as<HwcBufferPendingDisplay>(&nextEvent)) {
            mDisplayedOn.insert(display->displayId);
        }
        if (const auto* release = as<HwcBufferPendingRelease>(&nextEvent)) {
            mDisplayedOn.erase(release->displayId);

            mAwaitingSfRelease.insert(nextBufferId);
        }

        if (is<SfBufferReleased>(nextEvent)) {
            mAwaitingSfRelease.erase(nextBufferId);
        }

        std::vector<std::string> mismatches;

        if (is<SfBufferReleased>(nextEvent) && !mDisplayedOn.empty()) {
            mismatches.emplace_back(fmt::format("sf release while still displayed on [{}]s"sv,
                                                fmt::join(mDisplayedOn, ", "sv)));
        }

        return makeResult(lastEvent, nextEvent, mismatches);
    }

    constexpr auto operator()() const -> ResultType {
        ResultType result;
        std::vector<std::string> mismatches;
        mismatches.reserve(mAwaitingSfRelease.size());

        for (const auto bufferId : mAwaitingSfRelease) {
            mismatches.emplace_back(
                    fmt::format("missing sf buffer release for {}"sv, toString(bufferId)));
        }

        return makeResult(mLastEvent, std::optional<InputEvent>(), mismatches);
    }

  private:
    std::optional<InputEvent> mLastEvent;

    std::unordered_set<DisplayId> mDisplayedOn;
    std::unordered_set<BufferId> mAwaitingSfRelease;
};

using ValidationImpl = CompositionalValidator<
        SurfaceTransactionSequenceOrder, SurfaceTransactionConsistentBufferId,
        SurfaceTransactionConsistentLatchTime, BufferDisplayAndReleaseLifecycle>;

}  // namespace

[[nodiscard]] auto BasicValidationCheck(const std::vector<ScenarioEventRecorder::Event>& events)
        -> base::expected<void, std::string> {
    ValidationImpl validator;

    for (const auto& event : events) {
        if (auto result = validator.next(event); !result) {
            return result;
        }
    }
    return validator.finalize();
}

}  // namespace android::surfaceflinger::tests::end2end::test_framework::core
