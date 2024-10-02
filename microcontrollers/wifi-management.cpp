#include <Arduino.h>
#include <esp_wifi.h>
#include <string>

#include "log.h"
#include "main.h"
#include "wifi.h"


void WiFiEvent(WiFiEvent_t event)
{
	write_led(led_red, HIGH);

	std::string msg = "WiFi event: ";

	switch(event) {
		case WIFI_REASON_UNSPECIFIED:
			msg += "WIFI_REASON_UNSPECIFIED"; break;
		case WIFI_REASON_AUTH_EXPIRE:
			msg += "WIFI_REASON_AUTH_EXPIRE"; break;
		case WIFI_REASON_AUTH_LEAVE:
			msg += "WIFI_REASON_AUTH_LEAVE"; break;
		case WIFI_REASON_ASSOC_EXPIRE:
			msg += "WIFI_REASON_ASSOC_EXPIRE"; break;
		case WIFI_REASON_ASSOC_TOOMANY:
			msg += "WIFI_REASON_ASSOC_TOOMANY"; break;
		case WIFI_REASON_NOT_AUTHED:
			msg += "WIFI_REASON_NOT_AUTHED"; break;
		case WIFI_REASON_NOT_ASSOCED:
			msg += "WIFI_REASON_NOT_ASSOCED"; break;
		case WIFI_REASON_ASSOC_LEAVE:
			msg += "WIFI_REASON_ASSOC_LEAVE"; break;
		case WIFI_REASON_ASSOC_NOT_AUTHED:
			msg += "WIFI_REASON_ASSOC_NOT_AUTHED"; break;
		case WIFI_REASON_DISASSOC_PWRCAP_BAD:
			msg += "WIFI_REASON_DISASSOC_PWRCAP_BAD"; break;
		case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
			msg += "WIFI_REASON_DISASSOC_SUPCHAN_BAD"; break;
		case WIFI_REASON_IE_INVALID:
			msg += "WIFI_REASON_IE_INVALID"; break;
		case WIFI_REASON_MIC_FAILURE:
			msg += "WIFI_REASON_MIC_FAILURE"; break;
		case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
			msg += "WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT"; break;
		case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
			msg += "WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT"; break;
		case WIFI_REASON_IE_IN_4WAY_DIFFERS:
			msg += "WIFI_REASON_IE_IN_4WAY_DIFFERS"; break;
//		case WIFI_REASON_GROUP_CIPHER_INVALID:
//			msg += "WIFI_REASON_GROUP_CIPHER_INVALID"; break;
//		case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
//			msg += "WIFI_REASON_PAIRWISE_CIPHER_INVALID"; break;
//		case WIFI_REASON_AKMP_INVALID:
//			msg += "WIFI_REASON_AKMP_INVALID"; break;
//		case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
//			msg += "WIFI_REASON_UNSUPP_RSN_IE_VERSION"; break;
//		case WIFI_REASON_INVALID_RSN_IE_CAP:
//			msg += "WIFI_REASON_INVALID_RSN_IE_CAP"; break;
		case WIFI_REASON_802_1X_AUTH_FAILED:
			msg += "WIFI_REASON_802_1X_AUTH_FAILED"; break;
		case WIFI_REASON_CIPHER_SUITE_REJECTED:
			msg += "WIFI_REASON_CIPHER_SUITE_REJECTED"; break;
		case WIFI_REASON_BEACON_TIMEOUT:
			msg += "WIFI_REASON_BEACON_TIMEOUT"; break;
		case WIFI_REASON_NO_AP_FOUND:
			msg += "WIFI_REASON_NO_AP_FOUND"; break;
		case WIFI_REASON_AUTH_FAIL:
			msg += "WIFI_REASON_AUTH_FAIL"; break;
		case WIFI_REASON_ASSOC_FAIL:
			msg += "WIFI_REASON_ASSOC_FAIL"; break;
		case WIFI_REASON_HANDSHAKE_TIMEOUT:
			msg += "WIFI_REASON_HANDSHAKE_TIMEOUT"; break;
#if !defined(WEMOS32)
		case ARDUINO_EVENT_ETH_START:
			msg += "ETH Started";
			//set eth hostname here
			ETH.setHostname(name);
			break;
		case ARDUINO_EVENT_ETH_CONNECTED:
			msg += "ETH Connected";
			break;
		case ARDUINO_EVENT_ETH_GOT_IP: {
			auto ip = ETH.localIP();
			msg += "ETH IPv4: ";
			msg += myformat("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
			if (ETH.fullDuplex())
				msg += ", FULL_DUPLEX";
			msg += ", ";
			msg += myformat("%d", ETH.linkSpeed());
			msg += "Mb";
			eth_connected = true;
			break;
		}
		case ARDUINO_EVENT_ETH_DISCONNECTED:
			msg += "ETH Disconnected";
			eth_connected = false;
			break;
		case ARDUINO_EVENT_ETH_STOP:
			msg += "ETH Stopped";
			eth_connected = false;
			break;
#endif
		default:
			Serial.printf("Unknown/unexpected ETH event %d\r\n", event);
			break;
	}

	DOLOG(logging::ll_info, "WiFiEvent", "-", "%s", msg.c_str());
	write_led(led_red, LOW);
}

bool progress_indicator(const int nr, const int mx, const std::string & which)
{
#ifdef LED_BUILTIN
	digitalWrite(LED_BUILTIN, HIGH);
#endif
	printf("%3.2f%%: %s\r\n", nr * 100. / mx, which.c_str());
#ifdef LED_BUILTIN
	digitalWrite(LED_BUILTIN, LOW);
#endif

	return true;
}

void setup_wifi()
{
	write_led(led_green,  HIGH);
	write_led(led_yellow, HIGH);

	WiFi.onEvent(WiFiEvent);

	draw_status(20);
	enable_wifi_debug();

	draw_status(21);
	WiFi.onEvent(WiFiEvent);

	connect_status_t cs = CS_IDLE;
	draw_status(22);
	start_wifi({ });

	Serial.print(F("Scanning for accesspoints"));
	draw_status(23);
	scan_access_points_start();

	draw_status(24);
	while(scan_access_points_wait() == false) {
#ifdef LED_BUILTIN
		digitalWrite(LED_BUILTIN, HIGH);
#endif
		Serial.print(F("."));
#ifdef LED_BUILTIN
		digitalWrite(LED_BUILTIN, LOW);
#endif
		delay(100);
	}

	draw_status(25);
	auto available_access_points = scan_access_points_get();
	Serial.printf("Found %zu accesspoints\r\n", available_access_points.size());

	draw_status(26);
	auto state = try_connect_init(wifi_targets, available_access_points, 300, progress_indicator);

	Serial.println(F("Connecting"));
	draw_status(27);
	for(;;) {
		cs = try_connect_tick(state);

		if (cs != CS_IDLE)
			break;

		Serial.print(F("."));
		delay(100);
	}

	write_led(led_green,  LOW);
	write_led(led_yellow, LOW);

	// could not connect
	if (cs == CS_CONNECTED)
		draw_status(29);
	else
		draw_status(28);

	draw_status(25);
	esp_wifi_set_ps(WIFI_PS_NONE);
}

