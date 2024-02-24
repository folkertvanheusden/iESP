#include <map>
#include <string>
#include <vector>
#include <utility>

#include "wifi.h"


bool debug = false;

bool set_hostname(const std::string & hostname)
{
	WiFi.hostname(hostname.c_str());

	return true;
}

void start_wifi(const std::optional<std::string> & listen_ssid)
{
	WiFi.persistent(false);

	if (listen_ssid.has_value()) {
		WiFi.mode(WIFI_AP_STA);

		WiFi.softAP(listen_ssid.value().c_str());
	}
	else {
		WiFi.mode(WIFI_STA);
	}
}

void enable_wifi_debug()
{
	debug = true;
}

void scan_access_points_start()
{
	WiFi.scanNetworks(true);
}

void scan_access_points_cleanup()
{
	WiFi.scanDelete();
}

bool scan_access_points_wait()
{
	return WiFi.scanComplete() >= 0;
}

std::map<std::string, std::tuple<int, uint8_t, int> > scan_access_points_get()
{
	std::map<std::string, std::tuple<int, uint8_t, int> > out;

	int n_networks = WiFi.scanComplete();

	for(int i=0; i<n_networks; i++)
		out.insert({ WiFi.SSID(i).c_str(), { WiFi.RSSI(i), WiFi.encryptionType(i), WiFi.channel(i) } });

	scan_access_points_cleanup();

	return out;
}

bool connect_to_access_point(const std::string ssid, const std::string password)
{
	auto status = WiFi.begin(ssid.c_str(), password.c_str());

	// also WL_DISCONNECTED?
	return status != WL_NO_SHIELD && status != WL_NO_SSID_AVAIL && status != WL_CONNECT_FAILED && status != WL_CONNECTION_LOST;
}

connect_status_t check_wifi_connection_status()
{
	auto status = WiFi.status();

	if (debug)
		printf("wifi status: %d\r\n", status);

	if (status == WL_CONNECTED)
		return CS_CONNECTED;

	if (status == WL_IDLE_STATUS || status == WL_DISCONNECTED)
		return CS_IDLE;

	return CS_FAILURE;
}

std::optional<std::string> select_best_access_point(const std::map<std::string, int> & list)
{
	std::string best_ssid;
	int         best_ss   = -32767;

	for(auto & entry : list) {
		if (entry.second > best_ss) {
			best_ssid = entry.first;
			best_ss   = entry.second;
		}
	}

	if (best_ssid.empty())
		return { };

	return best_ssid;
}

connect_state_t try_connect_init(const std::vector<std::pair<std::string, std::string> > & targets,
	 	 const std::map<std::string, std::tuple<int, uint8_t, int> > & scan_results,
		 const int timeout,
		 std::optional<std::function<bool(const int, const int, const std::string &)> > progress_indicator)
{
	connect_state_t cs;
	cs.timeout            = timeout;
	cs.nr                 = 0;
	cs.waiting_nr         = 0;
	cs.connecting_state   = 0;
	cs.last_tick          = 0;
	cs.progress_indicator = progress_indicator;

	// scan through selected & scan-results and order by signal strength
	for(auto & target : targets) {
		auto scan_result = scan_results.find(target.first);

		if (scan_result != scan_results.end())
			cs.use.push_back({ target.first, target.second, std::get<0>(scan_result->second) });
	}

	std::sort(cs.use.begin(), cs.use.end(), [](auto & a, auto & b) { return std::get<2>(b) < std::get<2>(a); });

	return cs;
}

connect_status_t try_connect_tick(connect_state_t & cs)
{
	connect_status_t rc { CS_IDLE };

	if (cs.nr < cs.use.size()) {
		if (cs.connecting_state == false) {
			if (debug)
				printf("Connecting to %s\r\n", std::get<0>(cs.use.at(cs.nr)).c_str());

			if (connect_to_access_point(std::get<0>(cs.use.at(cs.nr)), std::get<1>(cs.use.at(cs.nr)))) {
				cs.connecting_state = true;

				cs.waiting_nr       = 0;
			}
			else {
				cs.nr++;
			}
		}
		else {
			auto status = check_wifi_connection_status();

			if (status == CS_CONNECTED) {
				if (debug)
					printf("Connected\r\n");

				if (cs.progress_indicator.has_value())
					cs.progress_indicator.value()(100, 100, std::get<0>(cs.use.at(cs.nr)));

				return CS_CONNECTED;
			}

			if (cs.waiting_nr >= cs.timeout) {
				if (debug)
					printf("timeout\r\n");

				cs.nr++;

				cs.connecting_state = false;

				return CS_IDLE;
			}

			if (status == CS_FAILURE) {
				if (debug)
					printf("Connection failed\r\n");

				cs.nr++;

				wifi_disconnect();

				cs.connecting_state = false;
			}
		}

		if (cs.nr < cs.use.size() && 
			cs.progress_indicator.has_value() &&
			cs.progress_indicator.value()(cs.nr * cs.timeout + cs.waiting_nr, cs.use.size() * cs.timeout, std::get<0>(cs.use.at(cs.nr))) == false)
			rc = CS_FAILURE;

		if (rc != CS_FAILURE) {
			uint32_t now = millis();

			if (now - cs.last_tick >= 100) {
				cs.last_tick = now;

				cs.waiting_nr++;
			}
		}
	}
	else {
		rc = CS_FAILURE;
	}
	
	if (rc == CS_FAILURE && cs.progress_indicator.has_value())
		cs.progress_indicator.value()(0, 100, "");

	return rc;
}

bool wifi_disconnect()
{
	WiFi.disconnect();

	return true;
}
