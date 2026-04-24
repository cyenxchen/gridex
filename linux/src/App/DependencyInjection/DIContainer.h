#pragma once

#include <any>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <typeindex>
#include <unordered_map>

#include "Core/Errors/GridexError.h"

namespace gridex {

class DIContainer {
public:
    DIContainer() = default;
    DIContainer(const DIContainer&) = delete;
    DIContainer& operator=(const DIContainer&) = delete;

    template <typename T>
    void registerSingleton(std::shared_ptr<T> instance) {
        std::unique_lock lock(mutex_);
        singletons_[std::type_index(typeid(T))] = std::move(instance);
    }

    template <typename T>
    void registerFactory(std::function<std::shared_ptr<T>()> factory) {
        std::unique_lock lock(mutex_);
        factories_[std::type_index(typeid(T))] = [f = std::move(factory)]() -> std::any {
            return f();
        };
    }

    template <typename T>
    std::shared_ptr<T> resolve() {
        std::shared_lock lock(mutex_);
        const auto key = std::type_index(typeid(T));

        if (auto it = singletons_.find(key); it != singletons_.end()) {
            return std::any_cast<std::shared_ptr<T>>(it->second);
        }

        if (auto it = factories_.find(key); it != factories_.end()) {
            auto produced = it->second();
            return std::any_cast<std::shared_ptr<T>>(produced);
        }

        throw InternalError(std::string("DIContainer: no binding for ") + typeid(T).name());
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::type_index, std::any> singletons_;
    std::unordered_map<std::type_index, std::function<std::any()>> factories_;
};

}
