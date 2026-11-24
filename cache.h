#include <cstdint>
#include <list>
#include <unordered_map>


class cache  // LRU
{
	const size_t capacity { };
	using cache_kv = std::pair<uint64_t, const uint8_t *>;
	std::list<cache_kv> lru_list;
	std::unordered_map<uint64_t, std::list<cache_kv>::iterator> store;

public:
	cache(const size_t capacity) :
		capacity(capacity)
	{
	}

	virtual ~cache()
	{
		store.clear();
		for(auto & item: lru_list)
			delete [] item.second;
		lru_list.clear();
	}

	void invalidate(const uint64_t block_nr)
	{
		auto it = store.find(block_nr);
		if (it == store.end())
			return;
		delete [] it->second->second;
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
			delete [] it->second->second;
			it->second->second = data;
			lru_list.splice(lru_list.begin(), lru_list, it->second);
			return;
		}
		lru_list.push_front({ block_nr, data });

		store[block_nr] = lru_list.begin();
		if (store.size() > capacity) {
			store.erase(lru_list.back().first);
			delete [] lru_list.back().second;
			lru_list.pop_back();
		}
	}
};
