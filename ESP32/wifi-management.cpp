void wifievent(wifievent_t event)
{
	write_led(led_red, high);

	std::string msg = "wifi event: ";

	switch(event) {
		case wifi_reason_unspecified:
			msg += "wifi_reason_unspecified"; break;
		case wifi_reason_auth_expire:
			msg += "wifi_reason_auth_expire"; break;
		case wifi_reason_auth_leave:
			msg += "wifi_reason_auth_leave"; break;
		case wifi_reason_assoc_expire:
			msg += "wifi_reason_assoc_expire"; break;
		case wifi_reason_assoc_toomany:
			msg += "wifi_reason_assoc_toomany"; break;
		case wifi_reason_not_authed:
			msg += "wifi_reason_not_authed"; break;
		case wifi_reason_not_assoced:
			msg += "wifi_reason_not_assoced"; break;
		case wifi_reason_assoc_leave:
			msg += "wifi_reason_assoc_leave"; break;
		case wifi_reason_assoc_not_authed:
			msg += "wifi_reason_assoc_not_authed"; break;
		case wifi_reason_disassoc_pwrcap_bad:
			msg += "wifi_reason_disassoc_pwrcap_bad"; break;
		case wifi_reason_disassoc_supchan_bad:
			msg += "wifi_reason_disassoc_supchan_bad"; break;
		case wifi_reason_ie_invalid:
			msg += "wifi_reason_ie_invalid"; break;
		case wifi_reason_mic_failure:
			msg += "wifi_reason_mic_failure"; break;
		case wifi_reason_4way_handshake_timeout:
			msg += "wifi_reason_4way_handshake_timeout"; break;
		case wifi_reason_group_key_update_timeout:
			msg += "wifi_reason_group_key_update_timeout"; break;
		case wifi_reason_ie_in_4way_differs:
			msg += "wifi_reason_ie_in_4way_differs"; break;
//		case wifi_reason_group_cipher_invalid:
//			msg += "wifi_reason_group_cipher_invalid"; break;
//		case wifi_reason_pairwise_cipher_invalid:
//			msg += "wifi_reason_pairwise_cipher_invalid"; break;
//		case wifi_reason_akmp_invalid:
//			msg += "wifi_reason_akmp_invalid"; break;
//		case wifi_reason_unsupp_rsn_ie_version:
//			msg += "wifi_reason_unsupp_rsn_ie_version"; break;
//		case wifi_reason_invalid_rsn_ie_cap:
//			msg += "wifi_reason_invalid_rsn_ie_cap"; break;
		case wifi_reason_802_1x_auth_failed:
			msg += "wifi_reason_802_1x_auth_failed"; break;
		case wifi_reason_cipher_suite_rejected:
			msg += "wifi_reason_cipher_suite_rejected"; break;
		case wifi_reason_beacon_timeout:
			msg += "wifi_reason_beacon_timeout"; break;
		case wifi_reason_no_ap_found:
			msg += "wifi_reason_no_ap_found"; break;
		case wifi_reason_auth_fail:
			msg += "wifi_reason_auth_fail"; break;
		case wifi_reason_assoc_fail:
			msg += "wifi_reason_assoc_fail"; break;
		case wifi_reason_handshake_timeout:
			msg += "wifi_reason_handshake_timeout"; break;
#if !defined(wemos32)
		case arduino_event_eth_start:
			msg += "eth started";
			//set eth hostname here
			eth.sethostname(name);
			break;
		case arduino_event_eth_connected:
			msg += "eth connected";
			break;
		case arduino_event_eth_got_ip: {
			auto ip = eth.localip();
			msg += "eth ipv4: ";
			msg += myformat("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
			if (eth.fullduplex())
				msg += ", full_duplex";
			msg += ", ";
			msg += myformat("%d", eth.linkspeed());
			msg += "mb";
			eth_connected = true;
			break;
		}
		case arduino_event_eth_disconnected:
			msg += "eth disconnected";
			eth_connected = false;
			break;
		case arduino_event_eth_stop:
			msg += "eth stopped";
			eth_connected = false;
			break;
#endif
		default:
			serial.printf("unknown/unexpected eth event %d\r\n", event);
			break;
	}
	errlog(msg.c_str());
	write_led(led_red, low);
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

