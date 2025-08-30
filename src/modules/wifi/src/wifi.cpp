#include <luart.h>
#include <Windows.h>
#include <wlanapi.h>
#include <wininet.h>
#include <string>

int onLost = LUA_NOREF;
int onConnected = LUA_NOREF;

LUA_METHOD(wifi, scan) {
    HANDLE hClient;
    DWORD dwVersion;
    if (WlanOpenHandle(2, NULL, &dwVersion, &hClient) != ERROR_SUCCESS) {
        lua_pushnil(L);
        return 1;
    }

    PWLAN_INTERFACE_INFO_LIST pIfList;
    if (WlanEnumInterfaces(hClient, NULL, &pIfList) != ERROR_SUCCESS) {
        WlanCloseHandle(hClient, NULL);
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    for (DWORD i = 0; i < pIfList->dwNumberOfItems; i++) {
        GUID guid = pIfList->InterfaceInfo[i].InterfaceGuid;
        WlanScan(hClient, &guid, NULL, NULL, NULL);

        PWLAN_AVAILABLE_NETWORK_LIST pList;
        if (WlanGetAvailableNetworkList(hClient, &guid, 0, NULL, &pList) == ERROR_SUCCESS) {
            for (DWORD j = 0; j < pList->dwNumberOfItems; j++) {
                WLAN_AVAILABLE_NETWORK net = pList->Network[j];
                lua_newtable(L);
                lua_pushstring(L, (char*)net.dot11Ssid.ucSSID);
                lua_setfield(L, -2, "name");
                lua_pushinteger(L, net.wlanSignalQuality);
                lua_setfield(L, -2, "quality");
                lua_pushboolean(L, net.bSecurityEnabled);
                lua_setfield(L, -2, "secured");
                lua_pushboolean(L, net.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE); 
                lua_setfield(L, -2, "known");
                lua_rawseti(L, -2, luaL_len(L, -2) + 1);    
            }
            WlanFreeMemory(pList);
        }
    }

    WlanFreeMemory(pIfList);
    WlanCloseHandle(hClient, NULL);
    return 1;
}

LUA_METHOD(wifi, connect) {
    int nargs = lua_gettop(L);
    wchar_t* ssid       = lua_towstring(L, 1);
    wchar_t* password   = nargs > 1 ? lua_towstring(L, 2) : NULL;
    wchar_t* authType   = nargs > 2 ? lua_towstring(L, 3) : NULL;
    wchar_t* encryption = nargs > 3 ? lua_towstring(L, 4) : NULL;
    HANDLE hClient;
    DWORD dwVersion;
    BOOL success = FALSE;

    if (WlanOpenHandle(2, NULL, &dwVersion, &hClient) == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST pIfList;
        if (WlanEnumInterfaces(hClient, NULL, &pIfList) == ERROR_SUCCESS) {
            for (DWORD i = 0; i < pIfList->dwNumberOfItems; i++) {
                if (password && wcslen(password) > 0) {
                    // 🔐 Generate profile XML and set it
                    wchar_t xml[2048];
                    _snwprintf(xml, sizeof(xml),
                        L"<?xml version=\"1.0\"?>"
                        "<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
                        "<name>%s</name>"
                        "<SSIDConfig><SSID><name>%s</name></SSID></SSIDConfig>"
                        "<connectionType>ESS</connectionType>"
                        "<connectionMode>auto</connectionMode>"
                        "<MSM><security>"
                        "<authEncryption>"
                        "<authentication>%s</authentication>"
                        "<encryption>%s</encryption>"
                        "<useOneX>false</useOneX>"
                        "</authEncryption>"
                        "<sharedKey>"
                        "<keyType>passPhrase</keyType>"
                        "<protected>false</protected>"
                        "<keyMaterial>%s</keyMaterial>"
                        "</sharedKey>"
                        "</security></MSM>"
                        "</WLANProfile>",
                        ssid, ssid, authType ? authType : L"WPA2PSK", encryption ? encryption : L"AES", password
                    );

                    DWORD reasonCode;
                    if (WlanSetProfile(hClient, &pIfList->InterfaceInfo[i].InterfaceGuid,
                                       0, xml, NULL, TRUE, NULL, &reasonCode) != ERROR_SUCCESS) {
                        continue; // skip to next interface if profile setting fails
                    }
                }

                // 📡 Connect using existing or newly created profile
                WLAN_CONNECTION_PARAMETERS params = {};
                params.wlanConnectionMode = wlan_connection_mode_profile;
                params.strProfile         = ssid;
                params.dot11BssType       = dot11_BSS_type_any;
                params.dwFlags            = 0;

                success = WlanConnect(hClient, &pIfList->InterfaceInfo[i].InterfaceGuid, &params, NULL) == ERROR_SUCCESS;
            }
            WlanFreeMemory(pIfList);
        }
        WlanCloseHandle(hClient, NULL);
    }
    free(ssid);
    free(password);
    free(authType);
    free(encryption);
    lua_pushboolean(L, success);
    return 1;
}

LUA_METHOD(wifi, disconnect) {
    HANDLE hClient;
    DWORD dwVersion;
    BOOL success = FALSE;

    if (WlanOpenHandle(2, NULL, &dwVersion, &hClient) == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST pIfList;
        if (WlanEnumInterfaces(hClient, NULL, &pIfList) == ERROR_SUCCESS) {
            for (DWORD i = 0; i < pIfList->dwNumberOfItems; i++) {
                if (WlanDisconnect(hClient, &pIfList->InterfaceInfo[i].InterfaceGuid, NULL) == ERROR_SUCCESS)
                    success = TRUE;
            }
            WlanFreeMemory(pIfList);
        }
        WlanCloseHandle(hClient, NULL);
    }
    lua_pushboolean(L, success);
    return 1;
}

LUA_PROPERTY_GET(wifi, channel) {    
    HANDLE hClient = NULL;
    DWORD dwMaxClient = 2;
    DWORD dwCurVersion = 0;
    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
    PWLAN_BSS_LIST pBssList = NULL;
    DWORD channel = -1;

    if (WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient) != ERROR_SUCCESS)
        return luaL_error(L, "Failed to open WLAN handle");

    if (WlanEnumInterfaces(hClient, NULL, &pIfList) != ERROR_SUCCESS)
        goto cleanup;

    for (int i = 0; i < (int)pIfList->dwNumberOfItems; ++i) {
        GUID guid = pIfList->InterfaceInfo[i].InterfaceGuid;

        if (WlanGetNetworkBssList(hClient, &guid, NULL, dot11_BSS_type_infrastructure,
                                    FALSE, NULL, &pBssList) != ERROR_SUCCESS)
            continue;

        for (int j = 0; j < (int)pBssList->dwNumberOfItems; ++j) {
            WLAN_BSS_ENTRY entry = pBssList->wlanBssEntries[j];
            ULONG freq = entry.ulChCenterFrequency;

            if (freq >= 2412000 && freq <= 2484000)
                channel = (freq - 2407000) / 500;
            else if (freq >= 5180000 && freq <= 5825000)
                channel = (freq - 5000000) / 500;

            break;
        }

        WlanFreeMemory(pBssList);
        pBssList = NULL;
        break;
    }

cleanup:
    if (pIfList)
        WlanFreeMemory(pIfList);
    if (hClient)
        WlanCloseHandle(hClient, NULL);
    
    if (channel == -1)
        lua_pushnil(L);
    else
        lua_pushinteger(L, channel);
    return 1;
}

LUA_PROPERTY_GET(wifi, mac) {
    HANDLE hClient;
    DWORD dwVersion;
    std::string bssid;

    if (WlanOpenHandle(2, NULL, &dwVersion, &hClient) == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST pList;
        if (WlanEnumInterfaces(hClient, NULL, &pList) == ERROR_SUCCESS) {
            for (DWORD i = 0; i < pList->dwNumberOfItems; i++) {
                PWLAN_CONNECTION_ATTRIBUTES conn;
                DWORD sz;
                WLAN_OPCODE_VALUE_TYPE type;

                if (WlanQueryInterface(hClient, &pList->InterfaceInfo[i].InterfaceGuid,
                    wlan_intf_opcode_current_connection, NULL, &sz, (PVOID*)&conn, &type) == ERROR_SUCCESS) {
                    BYTE* mac = conn->wlanAssociationAttributes.dot11Bssid;
                    char buf[18];
                    sprintf_s(buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                    bssid = buf;
                    WlanFreeMemory(conn);
                }
            }
            WlanFreeMemory(pList);
        }
        WlanCloseHandle(hClient, NULL);
    }

    if (bssid.empty())
        lua_pushnil(L);
    else
        lua_pushstring(L, bssid.c_str());
    return 1;
}

LUA_PROPERTY_GET(wifi, name) {
    HANDLE hClient = NULL;
    DWORD dwMaxClient = 2;
    DWORD dwCurVersion = 0;
    DWORD dwResult = WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);
    std::string ssid;

    if (dwResult == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
        if (WlanEnumInterfaces(hClient, NULL, &pIfList) == ERROR_SUCCESS) {
            for (int i = 0; i < (int)pIfList->dwNumberOfItems; i++) {
                PWLAN_INTERFACE_INFO pIfInfo = &pIfList->InterfaceInfo[i];
                PWLAN_CONNECTION_ATTRIBUTES pConn = NULL;
                DWORD dwSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
                WLAN_OPCODE_VALUE_TYPE opCode;

                if (WlanQueryInterface(hClient, &pIfInfo->InterfaceGuid,
                    wlan_intf_opcode_current_connection, NULL,
                    &dwSize, (PVOID*)&pConn, &opCode) == ERROR_SUCCESS) {
                    ssid.assign((char*)pConn->wlanAssociationAttributes.dot11Ssid.ucSSID,
                                pConn->wlanAssociationAttributes.dot11Ssid.uSSIDLength);
                    WlanFreeMemory(pConn);
                }
            }
            WlanFreeMemory(pIfList);
        }
        WlanCloseHandle(hClient, NULL);
    }
    if (!ssid.empty())
        lua_pushstring(L, ssid.c_str());
    else
        lua_pushnil(L);
    return 1;
}

static bool check_connection() {
    HANDLE hClient = NULL;
    DWORD dwVersion;
    BOOL isConnected = FALSE;

    if (WlanOpenHandle(2, NULL, &dwVersion, &hClient) == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
        if (WlanEnumInterfaces(hClient, NULL, &pIfList) == ERROR_SUCCESS) {
            for (DWORD i = 0; i < pIfList->dwNumberOfItems; i++) {
                PWLAN_INTERFACE_INFO pInfo = &pIfList->InterfaceInfo[i];
                if (pInfo->isState == wlan_interface_state_connected) {
                    isConnected = TRUE;
                    break;
                }
            }
            WlanFreeMemory(pIfList);
        }
        WlanCloseHandle(hClient, NULL);
    }
    return isConnected;
}

LUA_PROPERTY_GET(wifi, connected) {
    lua_pushboolean(L, check_connection());
    return 1;
}

LUA_PROPERTY_GET(wifi, quality) {
    HANDLE hClient = NULL;
    DWORD dwMaxClient = 2;
    DWORD dwCurVersion = 0;
    DWORD dwResult = WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);
    int percent = 0;

    if (dwResult == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
        if (WlanEnumInterfaces(hClient, NULL, &pIfList) == ERROR_SUCCESS) {
            for (int i = 0; i < (int)pIfList->dwNumberOfItems; i++) {
                PWLAN_INTERFACE_INFO pIfInfo = &pIfList->InterfaceInfo[i];
                PWLAN_CONNECTION_ATTRIBUTES pConn = NULL;
                DWORD dwSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
                WLAN_OPCODE_VALUE_TYPE opCode;

                if (WlanQueryInterface(hClient, &pIfInfo->InterfaceGuid,
                    wlan_intf_opcode_current_connection, NULL,
                    &dwSize, (PVOID*)&pConn, &opCode) == ERROR_SUCCESS) {
                    percent = (int)pConn->wlanAssociationAttributes.wlanSignalQuality;
                    WlanFreeMemory(pConn);
                }
            }
            WlanFreeMemory(pIfList);
        }
        WlanCloseHandle(hClient, NULL);
    }

    lua_pushinteger(L, percent);
    return 1;
}

static int checkConnectionContinue(lua_State* L, int status, lua_KContext ctx) {
    static bool wasConnected = check_connection();
    bool connected = check_connection();
    if (onLost != LUA_NOREF && !connected && wasConnected) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, onLost);
        if (lua_pcall(L, 0, 0, 0))
            lua_error(L);
    } else if (onConnected != LUA_NOREF && connected && !wasConnected) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, onConnected);
        if (lua_pcall(L, 0, 0, 0))
            lua_error(L);
    }
    wasConnected = connected;
    return lua_yieldk(L, 0, ctx, checkConnectionContinue);
}

LUA_PROPERTY_SET(wifi, onDisconnected) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    if (onLost != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, onLost);
    onLost = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

LUA_PROPERTY_GET(wifi, onDisconnected) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, onLost);
    return 1;
}

LUA_PROPERTY_SET(wifi, onConnected) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    if (onConnected != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, onConnected);
    onConnected = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

LUA_PROPERTY_GET(wifi, onConnected) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, onConnected);
    return 1;
}

MODULE_PROPERTIES(wifi)
    READONLY_PROPERTY(wifi, name)
    READONLY_PROPERTY(wifi, connected)
    READONLY_PROPERTY(wifi, quality)
    READONLY_PROPERTY(wifi, mac)
    READONLY_PROPERTY(wifi, channel)
    READWRITE_PROPERTY(wifi, onDisconnected)
    READWRITE_PROPERTY(wifi, onConnected)
END

MODULE_FUNCTIONS(wifi)
    METHOD(wifi, connect)
    METHOD(wifi, disconnect)
    METHOD(wifi, scan)
END

extern "C" int __declspec(dllexport) luaopen_wifi(lua_State* L) {
    lua_pushtask(L, checkConnectionContinue, NULL, 0);
    lua_regmodule(L, wifi);
    return 1;
}