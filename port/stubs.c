/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Evgeny Boger
 */
/*
 * DM40: no BLE multiplayer and (for now) no sound. Stubs for the MG24
 * port's network and audio drivers.
 */
#include <stdint.h>
#include "doom_ble.h"
#include "audio.h"

void bleFreeHostData(void) {}
void bleAllocateHostData(void) {}
uint8_t bleGetGameMode(void) { return 0; }
uint8_t bleMustStartGame(void) { return 0; }
void bleResetMustStartGame(void) {}
uint8_t bleGetClientStatus(void) { return 0; }
uint8_t bleGetAdvertisingSetHandle(void) { return 0; }
uint8_t bleReadOtherPlayerTics(bleDoomOtherPlayerTics_t *t) { (void) t; return 0; }
uint8_t bleGetPlayerData(uint8_t n, uint8_t *name, uint8_t *addr, uint8_t *handle) { (void) n; (void) name; (void) addr; (void) handle; return 0; }
void bleUpdateAdvertisingData(uint8_t mode, multiplayerGameSettings_t *s) { (void) mode; (void) s; }
uint8_t bleReadClientTics(bleDoomClientTics_t *cmd, int n) { (void) cmd; (void) n; return 0; }
void bleRequestClientsStartGame(void) {}
void bleInit(void) {}
uint8_t bleUpdateTicsToServer(int n) { (void) n; return 0; }
void bleUpdateEvents(void) {}
uint8_t bleHostSendTicsToClient(int n, int a, int b) { (void) n; (void) a; (void) b; return 0; }
void bleStartScanningForHost(void) {}
void bleStopScanAndAdvertising(void) {}
void bleCloseNetwork(void) {}
void bleConnectionClose(uint32_t n) { (void) n; }
void bleStartScanningForClients(uint32_t id) { (void) id; }

soundChannel_t soundChannels[MAX_CHANNELS];
void initAudio(void) {}
void updateSound(void) {}
void muteSound(void) {}
char localPlayerName[MAX_HOST_NAME_LENGTH] = "DM40";
void NetUpdate(void) {}
