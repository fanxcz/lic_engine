#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lic::world {

struct Entity final {
    static constexpr std::uint32_t invalid_index = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index{invalid_index};
    std::uint32_t generation{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != invalid_index && generation != 0;
    }

    friend constexpr bool operator==(Entity, Entity) noexcept = default;
};

class EntityRegistry final {
public:
    [[nodiscard]] Entity create() {
        std::uint32_t index{};
        if (!free_indices_.empty()) {
            index = free_indices_.back();
            free_indices_.pop_back();
        } else {
            if (slots_.size() >= static_cast<std::size_t>(Entity::invalid_index)) {
                throw std::length_error("Entity registry exhausted its 32-bit index space");
            }
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back(Slot{});
        }

        Slot& slot = slots_[index];
        slot.alive = true;
        ++alive_count_;
        return Entity{index, slot.generation};
    }

    [[nodiscard]] bool destroy(Entity entity) {
        if (!alive(entity)) {
            return false;
        }

        // Grow the free list before mutating the slot so allocation failure is harmless.
        free_indices_.push_back(entity.index);
        Slot& slot = slots_[entity.index];
        slot.alive = false;
        ++slot.generation;
        if (slot.generation == 0) {
            ++slot.generation;
        }
        --alive_count_;
        return true;
    }

    [[nodiscard]] bool alive(Entity entity) const noexcept {
        if (!entity.valid() || entity.index >= slots_.size()) {
            return false;
        }
        const Slot& slot = slots_[entity.index];
        return slot.alive && slot.generation == entity.generation;
    }

    [[nodiscard]] std::size_t size() const noexcept { return alive_count_; }

private:
    struct Slot final {
        std::uint32_t generation{1};
        bool alive{};
    };

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_indices_;
    std::size_t alive_count_{};
};

class World final {
    struct IStorage {
        virtual ~IStorage() = default;
        virtual void erase(Entity entity) noexcept = 0;
    };

    template <typename T>
    class Storage final : public IStorage {
        static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>,
                      "ECS components must be nothrow move constructible and assignable");

    public:
        template <typename... Args>
        T& emplace(Entity entity, Args&&... args) {
            const std::size_t sparse_index = static_cast<std::size_t>(entity.index);
            if (sparse_.size() <= sparse_index) {
                sparse_.resize(sparse_index + 1, 0);
            }

            const std::size_t encoded = sparse_[sparse_index];
            if (encoded != 0) {
                const std::size_t existing_index = encoded - 1;
                if (entities_[existing_index] == entity) {
                    throw std::logic_error("Entity already has this component type");
                }
                erase(entities_[existing_index]);
            }

            entities_.push_back(entity);
            try {
                components_.emplace_back(std::forward<Args>(args)...);
            } catch (...) {
                entities_.pop_back();
                throw;
            }
            sparse_[sparse_index] = entities_.size();
            return components_.back();
        }

        [[nodiscard]] T* find(Entity entity) noexcept {
            const auto index = dense_index(entity);
            return index ? &components_[*index] : nullptr;
        }

        [[nodiscard]] const T* find(Entity entity) const noexcept {
            const auto index = dense_index(entity);
            return index ? &components_[*index] : nullptr;
        }

        bool remove(Entity entity) noexcept {
            const auto index = dense_index(entity);
            if (!index) {
                return false;
            }
            erase_at(*index);
            return true;
        }

        void erase(Entity entity) noexcept override {
            const auto index = dense_index(entity);
            if (index) {
                erase_at(*index);
            }
        }

        template <typename Function>
        void each(Function&& function) {
            // Structural changes to this component pool are not allowed during iteration.
            for (std::size_t index = 0; index < entities_.size(); ++index) {
                function(entities_[index], components_[index]);
            }
        }

        template <typename Function>
        void each(Function&& function) const {
            for (std::size_t index = 0; index < entities_.size(); ++index) {
                function(entities_[index], components_[index]);
            }
        }

    private:
        [[nodiscard]] std::optional<std::size_t> dense_index(Entity entity) const noexcept {
            const std::size_t sparse_index = static_cast<std::size_t>(entity.index);
            if (sparse_index >= sparse_.size()) {
                return std::nullopt;
            }
            const std::size_t encoded = sparse_[sparse_index];
            if (encoded == 0) {
                return std::nullopt;
            }
            const std::size_t index = encoded - 1;
            if (index >= entities_.size() || entities_[index] != entity) {
                return std::nullopt;
            }
            return index;
        }

        void erase_at(std::size_t index) noexcept {
            const Entity removed = entities_[index];
            const std::size_t last = entities_.size() - 1;
            if (index != last) {
                entities_[index] = entities_[last];
                components_[index] = std::move(components_[last]);
                sparse_[entities_[index].index] = index + 1;
            }
            entities_.pop_back();
            components_.pop_back();
            sparse_[removed.index] = 0;
        }

        // Sparse indices map entity.index to dense position + 1; zero means absent.
        std::vector<std::size_t> sparse_;
        std::vector<Entity> entities_;
        std::vector<T> components_;
    };

public:
    [[nodiscard]] Entity create_entity() { return entities_.create(); }

    [[nodiscard]] bool destroy_entity(Entity entity) {
        if (!entities_.alive(entity)) {
            return false;
        }
        if (!entities_.destroy(entity)) {
            return false;
        }
        for (auto& [type, storage] : storages_) {
            (void)type;
            storage->erase(entity);
        }
        return true;
    }

    [[nodiscard]] bool alive(Entity entity) const noexcept { return entities_.alive(entity); }
    [[nodiscard]] std::size_t entity_count() const noexcept { return entities_.size(); }

    template <typename T, typename... Args>
    T& emplace(Entity entity, Args&&... args) {
        require_alive(entity);
        return get_or_create_storage<T>().emplace(entity, std::forward<Args>(args)...);
    }

    template <typename T>
    [[nodiscard]] bool has(Entity entity) const noexcept {
        if (!entities_.alive(entity)) {
            return false;
        }
        const auto it = storages_.find(std::type_index(typeid(T)));
        if (it == storages_.end()) {
            return false;
        }
        return static_cast<const Storage<T>*>(it->second.get())->find(entity) != nullptr;
    }

    template <typename T>
    T& get(Entity entity) {
        require_alive(entity);
        auto* component_storage = find_storage<T>();
        if (component_storage == nullptr) {
            throw std::out_of_range("Entity does not have the requested component");
        }
        auto* component = component_storage->find(entity);
        if (component == nullptr) {
            throw std::out_of_range("Entity does not have the requested component");
        }
        return *component;
    }

    template <typename T>
    const T& get(Entity entity) const {
        require_alive(entity);
        const auto* component_storage = find_storage<T>();
        if (component_storage == nullptr) {
            throw std::out_of_range("Entity does not have the requested component");
        }
        const auto* component = component_storage->find(entity);
        if (component == nullptr) {
            throw std::out_of_range("Entity does not have the requested component");
        }
        return *component;
    }

    template <typename T>
    bool remove(Entity entity) noexcept {
        if (!entities_.alive(entity)) {
            return false;
        }
        auto* component_storage = find_storage<T>();
        return component_storage != nullptr && component_storage->remove(entity);
    }

    // Do not create/destroy entities or add/remove T while iterating T components.
    template <typename T, typename Function>
    void each(Function&& function) {
        auto* component_storage = find_storage<T>();
        if (component_storage != nullptr) {
            component_storage->each(std::forward<Function>(function));
        }
    }

    template <typename T, typename Function>
    void each(Function&& function) const {
        const auto* component_storage = find_storage<T>();
        if (component_storage != nullptr) {
            component_storage->each(std::forward<Function>(function));
        }
    }

private:
    void require_alive(Entity entity) const {
        if (!entities_.alive(entity)) {
            throw std::invalid_argument("Entity handle is invalid or no longer alive");
        }
    }

    template <typename T>
    Storage<T>* find_storage() noexcept {
        const auto it = storages_.find(std::type_index(typeid(T)));
        return it == storages_.end() ? nullptr : static_cast<Storage<T>*>(it->second.get());
    }

    template <typename T>
    const Storage<T>* find_storage() const noexcept {
        const auto it = storages_.find(std::type_index(typeid(T)));
        return it == storages_.end() ? nullptr : static_cast<const Storage<T>*>(it->second.get());
    }

    template <typename T>
    Storage<T>& get_or_create_storage() {
        const auto key = std::type_index(typeid(T));
        const auto existing = storages_.find(key);
        if (existing != storages_.end()) {
            return *static_cast<Storage<T>*>(existing->second.get());
        }
        auto storage = std::make_unique<Storage<T>>();
        auto* pointer = storage.get();
        storages_.emplace(key, std::move(storage));
        return *pointer;
    }

    EntityRegistry entities_;
    std::unordered_map<std::type_index, std::unique_ptr<IStorage>> storages_;
};

} // namespace lic::world
