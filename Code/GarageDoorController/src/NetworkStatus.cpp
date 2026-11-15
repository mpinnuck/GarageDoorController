// NetworkStatus.cpp

#include "NetworkStatus.h"

#include <esp_netif.h>
#include <esp_wifi.h>

static bool getIpInfo(esp_netif_ip_info_t &info) {
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif == nullptr) return false;
  if (esp_netif_get_ip_info(netif, &info) != ESP_OK) return false;
  return info.ip.addr != 0;
}

bool NetworkStatus::hasIp() {
  esp_netif_ip_info_t info;
  return getIpInfo(info);
}

String NetworkStatus::ipAddress() {
  esp_netif_ip_info_t info;
  if (!getIpInfo(info)) return String();
  char buf[16];
  snprintf(buf, sizeof(buf), IPSTR, IP2STR(&info.ip));
  return String(buf);
}

String NetworkStatus::ssid() {
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return String();
  return String(reinterpret_cast<const char *>(ap.ssid));
}

int NetworkStatus::rssi() {
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
  return ap.rssi;
}
