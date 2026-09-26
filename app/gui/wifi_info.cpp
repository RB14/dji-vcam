#include "wifi_info.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wlanapi.h>
#endif

QString currentWifiSsid() {
#ifdef Q_OS_WIN
    DWORD version = 0;
    HANDLE client = nullptr;
    if (WlanOpenHandle(2, nullptr, &version, &client) != ERROR_SUCCESS) {
        return {};
    }
    QString ssid;
    PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
    if (WlanEnumInterfaces(client, nullptr, &interfaces) == ERROR_SUCCESS) {
        for (DWORD i = 0; i < interfaces->dwNumberOfItems && ssid.isEmpty(); ++i) {
            const WLAN_INTERFACE_INFO& info = interfaces->InterfaceInfo[i];
            if (info.isState != wlan_interface_state_connected) {
                continue;
            }
            DWORD size = 0;
            PWLAN_CONNECTION_ATTRIBUTES connection = nullptr;
            if (WlanQueryInterface(client, &info.InterfaceGuid, wlan_intf_opcode_current_connection, nullptr, &size,
                                   reinterpret_cast<PVOID*>(&connection), nullptr) == ERROR_SUCCESS) {
                const DOT11_SSID& raw = connection->wlanAssociationAttributes.dot11Ssid;
                ssid = QString::fromUtf8(reinterpret_cast<const char*>(raw.ucSSID), static_cast<qsizetype>(raw.uSSIDLength));
                WlanFreeMemory(connection);
            }
        }
        WlanFreeMemory(interfaces);
    }
    WlanCloseHandle(client, nullptr);
    return ssid;
#else
    return {};  // Linux support is in progress
#endif
}
