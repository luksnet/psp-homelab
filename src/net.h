#pragma once

// Ładuje moduły sieciowe PSP i łączy z siecią Wi-Fi zapisaną w profilu `profile`
// (Ustawienia sieci na PSP; w PPSSPP wystarczy włączona sieć).
// Zwraca 0 po uzyskaniu adresu IP, <0 przy błędzie.
int net_connect(int profile, int timeout_ms, char *ip_out, int ip_len);
void net_shutdown(void);
