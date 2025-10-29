/*
	Copyright (C) 2025 DeSmuME team

	This file is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with the this software.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CLIENT_FIRMWARE_CONTROL_H_
#define _CLIENT_FIRMWARE_CONTROL_H_

#include <stdio.h>
#include <stdint.h>


struct FirmwareConfig;

enum FirmwareCfgAPID
{
	FirmwareCfgAPID_AP1 = 0,
	FirmwareCfgAPID_AP2 = 1,
	FirmwareCfgAPID_AP3 = 2
};

enum FirmwareCfgMACAddrSetID
{
	FirmwareCfgMACAddrSetID_Firmware = 0,
	FirmwareCfgMACAddrSetID_Custom1 = 1,
	FirmwareCfgMACAddrSetID_Custom2 = 2,
	FirmwareCfgMACAddrSetID_Custom3 = 3,
	FirmwareCfgMACAddrSetID_Custom4 = 4,
	FirmwareCfgMACAddrSetID_Custom5 = 5,
	FirmwareCfgMACAddrSetID_Custom6 = 6,
	FirmwareCfgMACAddrSetID_Custom7 = 7,
	FirmwareCfgMACAddrSetID_Custom8 = 8
};

class ClientFirmwareControl
{
protected:
	FirmwareConfig *_internalData;
	
	FirmwareCfgMACAddrSetID _macAddressSelection;
	uint32_t _firmwareMACAddressValue;
	uint32_t _customMACAddressValue;
	char _macAddressString[(1 + 8) * 18];
	char _wfcUserIDString[20];
	char _subnetMaskString[3 * 16];
	
public:
	ClientFirmwareControl();
	~ClientFirmwareControl();
	
	static void WriteMACAddressStringToBuffer(const uint8_t mac4, const uint8_t mac5, const uint8_t mac6, char *stringBuffer);
	static void WriteMACAddressStringToBuffer(const uint32_t macAddressValue, char *stringBuffer);
	static void WriteWFCUserIDStringToBuffer(const uint64_t wfcUserIDValue, char *stringBuffer);
	
	const FirmwareConfig& GetFirmwareConfig();
	uint32_t GenerateRandomMACValue();
	
	FirmwareCfgMACAddrSetID GetMACAddressSelection();
	void SetMACAddressSelection(FirmwareCfgMACAddrSetID addressSetID);
	
	uint32_t GetMACAddressValue(FirmwareCfgMACAddrSetID addressSetID);
	const char* GetMACAddressString(FirmwareCfgMACAddrSetID addressSetID);
	uint32_t GetSelectedMACAddressValue();
	uint64_t GetSelectedWFCUserID64();
	const char* GetSelectedMACAddressString();
	
	void SetFirmwareMACAddressValue(uint32_t macAddressValue);
	void SetFirmwareMACAddressValue(uint8_t mac4, uint8_t mac5, uint8_t mac6);
	void SetCustomMACAddressValue(uint32_t macAddressValue);
	void SetCustomMACAddressValue(uint8_t mac4, uint8_t mac5, uint8_t mac6);
	
	uint8_t* GetWFCUserID();
	void SetWFCUserID(uint8_t *wfcUserID);
	
	uint64_t GetWFCUserID64();
	void SetWFCUserID64(uint64_t wfcUserIDValue);
	
	const char* GetWFCUserIDString();
	
	uint8_t GetIPv4AddressPart(FirmwareCfgAPID apid, uint8_t addrPart);
	void SetIPv4AddressPart(FirmwareCfgAPID apid, uint8_t addrPart, uint8_t addrValue);
	
	uint8_t GetIPv4GatewayPart(FirmwareCfgAPID apid, uint8_t addrPart);
	void SetIPv4GatewayPart(FirmwareCfgAPID apid, uint8_t addrPart, uint8_t addrValue);
	
	uint8_t GetIPv4PrimaryDNSPart(FirmwareCfgAPID apid, uint8_t addrPart);
	void SetIPv4PrimaryDNSPart(FirmwareCfgAPID apid, uint8_t addrPart, uint8_t addrValue);
	
	uint8_t GetIPv4SecondaryDNSPart(FirmwareCfgAPID apid, uint8_t addrPart);
	void SetIPv4SecondaryDNSPart(FirmwareCfgAPID apid, uint8_t addrPart, uint8_t addrValue);
	
	uint8_t GetSubnetMask(FirmwareCfgAPID apid);
	const char* GetSubnetMaskString(FirmwareCfgAPID apid);
	void SetSubnetMask(FirmwareCfgAPID apid, uint8_t subnetMaskShift);
	
	int GetConsoleType();
	void SetConsoleType(int type);
	
	uint16_t* GetNicknameStringBuffer();
	void SetNicknameWithStringBuffer(uint16_t *buffer, size_t charLength);
	
	size_t GetNicknameStringLength();
	void SetNicknameStringLength(size_t charLength);
	
	uint16_t* GetMessageStringBuffer();
	void SetMessageWithStringBuffer(uint16_t *buffer, size_t charLength);
	
	size_t GetMessageStringLength();
	void SetMessageStringLength(size_t charLength);
	
	int GetFavoriteColorByID();
	void SetFavoriteColorByID(int colorID);
	
	int GetBirthdayDay();
	void SetBirthdayDay(int day);
	
	int GetBirthdayMonth();
	void SetBirthdayMonth(int month);
	
	int GetLanguageByID();
	void SetLanguageByID(int languageID);
	
	int GetBacklightLevel();
	void SetBacklightLevel(int level);
};

#endif // _CLIENT_FIRMWARE_CONTROL_H_
