#pragma once

#include <map>
#include <optional>
#include <string>

#if defined(ESP32)
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif


typedef enum {
	CS_IDLE,      // not connected yet
	CS_CONNECTED,
	CS_FAILURE    // not connected any more or can't connect
} connect_status_t;


// helper functions
bool                       connect_to_access_point     (const std::string ssid, const std::string password);

connect_status_t           check_wifi_connection_status();

std::optional<std::string> select_best_access_point    (const std::map<std::string, int> & list);

//// main functions
void                       enable_wifi_debug           ();

void                       start_wifi                  (const std::optional<std::string> & listen_ssid);

/// call this before invoking any other wifi function
bool                       set_hostname                (const std::string & hostname);

// ssid, signal-strength (in dB) pairs / encryption-type / channel
void                       scan_access_points_start    ();

bool                       scan_access_points_wait     ();

std::map<std::string, std::tuple<int, uint8_t, int> >
                           scan_access_points_get      ();

// targets: ssid, password
// timeout in number of 100ms intervals per target
// callback: interval_nr, max_nr_set, can return false to abort
typedef struct {
	std::vector<std::tuple<std::string, std::string, int> > use;

	int      timeout;

	size_t   nr;

	int      waiting_nr;

	bool     connecting_state;

	uint32_t last_tick;

	std::optional<std::function<bool(const int, const int, const std::string &)> > progress_indicator;
} connect_state_t;

// start trying to connect
connect_state_t            try_connect_init            (const std::vector<std::pair<std::string, std::string> > & targets,
							const std::map<std::string, std::tuple<int, uint8_t, int> > & scan_results,
							const int timeout,
							std::optional<std::function<bool(const int, const int, const std::string & )> > progress_indicator);

// non-blocking check to see if we're connected (CS_CONNECTED or CS_FAILURE) or still connection (CS_IDLE)
connect_status_t           try_connect_tick            (connect_state_t & cs);

bool                       wifi_disconnect             ();
