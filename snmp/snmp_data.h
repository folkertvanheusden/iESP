// (C) 2022-2025 by folkert van heusden <mail@vanheusden.com>, released under Apache License v2.0
#pragma once

#if !defined(TEENSY4_1)  // does not do threading
#include <atomic>
#include <mutex>
#endif
#include <functional>
#include <optional>
#include <stdint.h>
#include <vector>

#include "snmp_elem.h"


class snmp_data_type
{
protected:
	int         oid_idx { 0 };
	std::string oid;

	std::vector<snmp_data_type *> data;

public:
	snmp_data_type();
	virtual ~snmp_data_type();

	virtual snmp_elem * get_data();

	std::vector<snmp_data_type *> * get_children();
	void        set_tree_data(const std::string & oid);
	int         get_oid_idx() const;
	std::string get_oid() const;
};

class snmp_data_type_static : public snmp_data_type
{
private:
	const bool        is_string { false };
	const std::string data;
	const snmp_integer::snmp_integer_type type { snmp_integer::si_integer };
	const int         data_int  { 0 };

public:
	snmp_data_type_static(const std::string & content);
	snmp_data_type_static(const snmp_integer::snmp_integer_type type, const int content);
	virtual ~snmp_data_type_static();

	snmp_elem * get_data() override;
};

class snmp_data_type_stats : public snmp_data_type
{
private:
	uint64_t *const counter;

protected:
	const snmp_integer::snmp_integer_type type;

public:
	snmp_data_type_stats(const snmp_integer::snmp_integer_type type, uint64_t *const counter);
	virtual ~snmp_data_type_stats();

	snmp_elem * get_data() override;
};

#if !defined(TEENSY4_1)
class snmp_data_type_stats_atomic : public snmp_data_type
{
private:
	std::atomic_uint64_t *const counter;

protected:
	const snmp_integer::snmp_integer_type type;

public:
	snmp_data_type_stats_atomic(const snmp_integer::snmp_integer_type type, std::atomic_uint64_t *const counter);
	virtual ~snmp_data_type_stats_atomic();

	snmp_elem * get_data() override;
};
#endif

class snmp_data_type_stats_int: public snmp_data_type
{
private:
	int *const counter;

public:
	snmp_data_type_stats_int(int *const counter);
	virtual ~snmp_data_type_stats_int();

	snmp_elem * get_data() override;
};

class snmp_data_type_stats_int_callback: public snmp_data_type
{
private:
        std::function<int(void *)> cb;
        void *const context;

public:
        snmp_data_type_stats_int_callback(std::function<int(void *)> & function, void *const context);
        virtual ~snmp_data_type_stats_int_callback();

        snmp_elem * get_data() override;
};

class snmp_data_type_stats_atomic_int: public snmp_data_type
{
private:
#if defined(TEENSY4_1)
	int *const counter;
#else
	std::atomic_int *const counter;
#endif

public:
#if defined(TEENSY4_1)
	snmp_data_type_stats_atomic_int(int *const counter);
#else
	snmp_data_type_stats_atomic_int(std::atomic_int *const counter);
#endif
	virtual ~snmp_data_type_stats_atomic_int();

	snmp_elem * get_data() override;
};

#if !defined(TEENSY4_1)
class snmp_data_type_stats_uint32_t: public snmp_data_type
{
private:
	uint32_t *const counter;

public:
	snmp_data_type_stats_uint32_t(uint32_t *const counter);
	virtual ~snmp_data_type_stats_uint32_t();

	snmp_elem * get_data() override;
};

class snmp_data_type_stats_atomic_uint32_t: public snmp_data_type
{
private:
	std::atomic_uint32_t *const counter;

public:
	snmp_data_type_stats_atomic_uint32_t(std::atomic_uint32_t *const counter);
	virtual ~snmp_data_type_stats_atomic_uint32_t();

	snmp_elem * get_data() override;
};
#endif

class snmp_data_type_running_since : public snmp_data_type
{
private:
	const uint64_t running_since;

public:
	snmp_data_type_running_since();
	virtual ~snmp_data_type_running_since();

	snmp_elem * get_data() override;
};

class snmp_data_type_oid : public snmp_data_type
{
private:
	const std::string oid;

public:
	snmp_data_type_oid(const std::string & oid) : oid(oid) {
	}

	virtual ~snmp_data_type_oid() {
	}

	snmp_elem * get_data() override { return new snmp_oid(oid); }
};

class snmp_data
{
private:
	std::vector<snmp_data_type *> data;
#if !defined(TEENSY4_1)
	std::mutex lock;
#endif

	void walk_tree(snmp_data_type & node);

public:
	snmp_data();
	virtual ~snmp_data();

	void register_oid(const std::string & oid, const std::string & static_data);
	void register_oid(const std::string & oid, const snmp_integer::snmp_integer_type type, const int static_data);
	void register_oid(const std::string & oid, snmp_data_type *const e);

	std::optional<snmp_elem *> find_by_oid(const std::string & oid);
	std::string find_next_oid(const std::string & oid);

	void dump_tree();
};
