#include <list>
#include <unordered_map>


#if defined(ESP32)
template <typename T>
struct PSRAMListAllocator {
	using value_type = T;

	PSRAMListAllocator() = default;

	template <typename U>
	PSRAMListAllocator(const PSRAMListAllocator<U>&) {
	}

	T* allocate(std::size_t n) {
		auto *temp = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
		return static_cast<T*>(temp);
	}

	void deallocate(T* p, std::size_t) {
		free(p);
	}

	bool operator==(const PSRAMListAllocator&) const noexcept {
		return true;
	}

	bool operator!=(const PSRAMListAllocator&) const noexcept {
		return false;
	}
};

template<typename T>
class PSRAMUnorderedMapAllocator {
    public:
        using value_type = T;

        PSRAMUnorderedMapAllocator() noexcept = default;
    
        template<typename U>
        PSRAMUnorderedMapAllocator(const PSRAMUnorderedMapAllocator<U>& other) noexcept {
	}

	T* allocate(size_t size) {
		auto *temp = heap_caps_malloc(size * sizeof(T), MALLOC_CAP_SPIRAM);
		return static_cast<T*>(temp);
	}

	void deallocate(T* ptr, size_t size) noexcept {
		free(ptr);
	}   

        template<typename U>
        bool operator ==(const PSRAMUnorderedMapAllocator<U>& other) const noexcept {
            return true;
        }

        template<typename U>
        bool operator !=(const PSRAMUnorderedMapAllocator<U>& other) const noexcept {
            return !(*this == other);
        }
};
#endif

class cache  // LRU
{
	const size_t capacity { };
	using cache_kv = std::pair<uint64_t, const uint8_t *>;
#if defined(ESP32)
	std::list<cache_kv, PSRAMListAllocator<cache_kv> > lru_list;
	using cache_map_value = std::pair<const uint64_t, std::list<cache_kv>::iterator>;
	std::unordered_map<uint64_t, std::list<cache_kv>::iterator,
			std::hash<uint64_t>,
			std::equal_to<uint64_t>,
			PSRAMUnorderedMapAllocator<cache_map_value>
		> store;
#else
	std::list<cache_kv> lru_list;
	std::unordered_map<uint64_t, std::list<cache_kv>::iterator> store;
#endif

public:
	cache(const size_t capacity) :
		capacity(capacity)
	{
	}

	virtual ~cache()
	{
		store.clear();
		for(auto & item: lru_list)
			free(const_cast<uint8_t *>(item.second));
		lru_list.clear();
	}

	void invalidate(const uint64_t block_nr)
	{
		auto it = store.find(block_nr);
		if (it == store.end())
			return;
		free(const_cast<uint8_t *>(it->second->second));
		lru_list.erase(it->second);
		store.erase(it);
	}

	const uint8_t * get(const uint64_t block_nr)
	{
		auto it = store.find(block_nr);
		if (it == store.end())
			return nullptr;
		lru_list.splice(lru_list.begin(), lru_list, it->second);
		return it->second->second;
	}

	void put(uint64_t block_nr, const uint8_t *const data)
	{
		auto it = store.find(block_nr);
		if (it != store.end()) {
			free(const_cast<uint8_t *>(it->second->second));
			it->second->second = data;
			lru_list.splice(lru_list.begin(), lru_list, it->second);
			return;
		}
		lru_list.push_front({ block_nr, data });

		store[block_nr] = lru_list.begin();
		if (store.size() > capacity) {
			store.erase(lru_list.back().first);
			free(const_cast<uint8_t *>(lru_list.back().second));
			lru_list.pop_back();
		}
	}
};
