#pragma once

void statusScreenPortal();
void statusScreenConnectFailed();
void statusScreenWifiReset();
/** Current STA IP address, or a "not connected" notice when ip is null/empty. */
void statusScreenIpAddress(const char* ip);

/** Saved-network connect animation (call Tick until connect finishes). */
void statusScreenConnectingBegin(const char* ssid);
void statusScreenConnectingTick();
