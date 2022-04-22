#pragma once

template <typename Key, typename Value>
class ConcurrentMap {
public:
    static_assert(std::is_integral_v<Key>, "ConcurrentMap supports only integer keys");

    struct Access {
        std::lock_guard<std::mutex> guard;
        Value& ref_to_value;
    };

    explicit ConcurrentMap(size_t bucket_count):
            bucket_cnt_(bucket_count),
            devided_maps_(bucket_count),
            vector_mutexes_(bucket_count)
    {}

    Access operator[](const Key& key){
        uint64_t submap_number = static_cast<uint64_t>(key) % bucket_cnt_;
        return Access{
                std::lock_guard(vector_mutexes_[submap_number]),
                devided_maps_[submap_number][key]
        };
    }


    std::map<Key, Value> BuildOrdinaryMap(){
        std::map<Key, Value> ordinary_map;
        for(size_t i = 0; i < bucket_cnt_; ++i){
            std::lock_guard guard(vector_mutexes_[i]);
            ordinary_map.insert(
                    std::make_move_iterator(devided_maps_[i].begin()),
                    std::make_move_iterator(devided_maps_[i].end())
            );
        }
        return ordinary_map;
    }

private:
    size_t bucket_cnt_;
    std::vector<std::map<Key, Value>> devided_maps_;
    std::vector<std::mutex> vector_mutexes_;
};