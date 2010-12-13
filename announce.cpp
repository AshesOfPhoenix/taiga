/*
** Taiga, a lightweight client for MyAnimeList
** Copyright (C) 2010, Eren Okka
** 
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "std.h"
#include "animelist.h"
#include "announce.h"
#include "common.h"
#include "dlg/dlg_main.h"
#include "dde.h"
#include "http.h"
#include "settings.h"
#include "string.h"
#include "win32/win_taskdialog.h"

CHTTP HTTPClient, TwitterClient;
CSkype Skype;

// =============================================================================

/* HTTP */

void AnnounceToHTTP(wstring url, wstring data) {
  if (url.empty() || data.empty()) return;

  wstring server, object;
  CrackURL(url, server, object);

  HTTPClient.Post(server, object, data, L"", HTTP_Silent);
}

// =============================================================================

/* Messenger */

void AnnounceToMessenger(wstring artist, wstring album, wstring title, BOOL show) {
  if (title.empty() && show) return;
  
  COPYDATASTRUCT cds;
  WCHAR buffer[256];

  wstring wstr = L"\\0Music\\0" + ToWSTR(show) + L"\\0{1}\\0" + 
    artist + L"\\0" + title + L"\\0" + album + L"\\0\\0";
  wcscpy_s(buffer, 256, wstr.c_str());

  cds.dwData = 0x547;
  cds.lpData = &buffer;
  cds.cbData = (lstrlenW(buffer) * 2) + 2;

  HWND hMessenger = NULL;
  while (hMessenger = FindWindowEx(NULL, hMessenger, L"MsnMsgrUIManager", NULL)) {
    if (hMessenger > 0) {
      SendMessage(hMessenger, WM_COPYDATA, NULL, (LPARAM)&cds);
    }
  }
}

// =============================================================================

/* mIRC */

BOOL AnnounceToMIRC(wstring service, wstring channels, wstring data, int mode, BOOL use_action, BOOL multi_server) {
  if (!FindWindow(L"mIRC", NULL)) return FALSE;
  if (service.empty() || channels.empty() || data.empty()) return FALSE;

  // Initialize
  CDDE DDE;
  if (!DDE.Initialize(/*APPCLASS_STANDARD | APPCMD_CLIENTONLY, TRUE*/)) {
    CTaskDialog dlg;
    dlg.SetWindowTitle(L"Announce to mIRC");
    dlg.SetMainIcon(TD_ICON_ERROR);
    dlg.SetMainInstruction(L"DDE initialization failed.");
    dlg.AddButton(L"OK", IDOK);
    dlg.Show(g_hMain);
    return FALSE;
  }

  // List channels
  if (mode != MIRC_CHANNELMODE_CUSTOM) {
    if (DDE.Connect(service, L"CHANNELS")) {
      DDE.ClientTransaction(L" ", L"", &channels, XTYP_REQUEST);
      DDE.Disconnect();
    }
  }
  vector<wstring> channel_list;
  Tokenize(channels, L" ,;", channel_list);
  for (size_t i = 0; i < channel_list.size(); i++) {
    Trim(channel_list[i]);
    if (channel_list[i].empty()) {
      continue;
    }
    if (channel_list[i].at(0) == '*') {
      channel_list[i] = channel_list[i].substr(1);
      if (mode == MIRC_CHANNELMODE_ACTIVE) {
        wstring temp = channel_list[i];
        channel_list.clear();
        channel_list.push_back(temp);
        break;
      }
    }
    if (channel_list[i].at(0) != '#') {
      channel_list[i].insert(channel_list[i].begin(), '#');
    }
  }

  // Connect
  if (!DDE.Connect(service, L"COMMAND")) {
    CTaskDialog dlg;
    dlg.SetWindowTitle(L"Announce to mIRC");
    dlg.SetMainIcon(TD_ICON_ERROR);
    dlg.SetMainInstruction(L"DDE connection failed.");
    dlg.SetContent(L"Please enable DDE server from mIRC Options > Other > DDE.");
    dlg.AddButton(L"OK", IDOK);
    dlg.Show(g_hMain);
    DDE.UnInitialize();
    return FALSE;
  }
  
  // Send message to channels
  for (size_t i = 0; i < channel_list.size(); i++) {
    wstring message;
    message += multi_server ? L"/scon -a " : L"";
    message += use_action ? L"/describe " : L"/msg ";
    message += channel_list[i] + L" " + data;
    DDE.ClientTransaction(L" ", message, NULL, XTYP_POKE);
  }

  // Clean up
  DDE.Disconnect();
  DDE.UnInitialize();
  return TRUE;
}

BOOL TestMIRCConnection(wstring service) {
  wstring content;
  CTaskDialog dlg;
  dlg.SetWindowTitle(L"Test DDE connection");
  dlg.SetMainIcon(TD_ICON_ERROR);
  dlg.AddButton(L"OK", IDOK);
  
  // Search for mIRC window
  if (!FindWindow(L"mIRC", NULL)) {
    dlg.SetMainInstruction(L"mIRC is not running.");
    dlg.Show(g_hMain);
    return FALSE;
  }

  // Initialize
  CDDE DDE;
  if (!DDE.Initialize(/*APPCLASS_STANDARD | APPCMD_CLIENTONLY, TRUE*/)) {
    dlg.SetMainInstruction(L"DDE initialization failed.");
    dlg.Show(g_hMain);
    return FALSE;
  }

  // Try to connect
  if (!DDE.Connect(service, L"CHANNELS")) {
    dlg.SetMainInstruction(L"DDE connection failed.");
    dlg.SetContent(L"Please enable DDE server from mIRC Options > Other > DDE.");
    dlg.Show(g_hMain);
    DDE.UnInitialize();
    return FALSE;
  } else {
    wstring channels;
    DDE.ClientTransaction(L" ", L"", &channels, XTYP_REQUEST);
    if (!channels.empty()) content = L"Current channels: " + channels;
  }

  // Success
  dlg.SetMainIcon(TD_ICON_INFORMATION);
  dlg.SetMainInstruction(L"Successfuly connected to DDE server!");
  dlg.SetContent(content.c_str());
  dlg.Show(g_hMain);
  DDE.Disconnect();
  DDE.UnInitialize();
  return TRUE;
}

// =============================================================================

/* Skype */

CSkype::CSkype() {
  m_APIWindowHandle = NULL;
  m_uControlAPIAttach = ::RegisterWindowMessage(L"SkypeControlAPIAttach");
  m_uControlAPIDiscover = ::RegisterWindowMessage(L"SkypeControlAPIDiscover");
}

BOOL CSkype::Attach() {
  PDWORD_PTR sendMessageResult = NULL;
  return SendMessageTimeout(HWND_BROADCAST, m_uControlAPIDiscover, reinterpret_cast<WPARAM>(g_hMain), 
    0, SMTO_NORMAL, 1000, sendMessageResult);
}

BOOL CSkype::ChangeMood() {
  m_Mood = L"SET PROFILE RICH_MOOD_TEXT " + m_Mood;
  const char* buffer = ToANSI(m_Mood);
  m_Mood.clear();

  COPYDATASTRUCT cds;
  cds.dwData = 0;
  cds.lpData = (void*)buffer;
  cds.cbData = strlen(buffer) + 1;

  if (SendMessage(m_APIWindowHandle, WM_COPYDATA, reinterpret_cast<WPARAM>(g_hMain), 
    reinterpret_cast<LPARAM>(&cds)) == FALSE) {
      m_APIWindowHandle = NULL;
      return FALSE;
  } else {
    return TRUE;
  }
}

void AnnounceToSkype(wstring mood) {
  Skype.m_Mood = mood;

  if (Skype.m_APIWindowHandle == NULL) {
    Skype.Attach();
  } else {
    Skype.ChangeMood();
  }
}

// =============================================================================

/* Twitter */

void AnnounceToTwitter(wstring status_text) {
  return; // TODO: Re-enable after Twitter announcements are fixed.
  
  if (Settings.Announce.Twitter.User.empty() || 
    Settings.Announce.Twitter.Password.empty() ||
    status_text.empty()) {
      return;
  }

  /*TwitterClient.Connect(L"twitter.com", 
    L"/statuses/update.xml", 
    L"status=" + EncodeURL(status_text), 
    L"POST", 
    L"Authorization: Basic " + 
    GetUserPassEncoded(Settings.Announce.Twitter.User, Settings.Announce.Twitter.Password), 
    L"myanimelist.net", 
    L"", HTTP_Twitter);*/
  HTTPParameters postParams;
  postParams[L"status"] = UrlEncode(status_text);
  OAuthWebRequestSubmit(L"http://twitter.com/statuses/update.xml", L"POST", &postParams, L"9GZsCbqzjOrsPWlIlysvg", L"ebjXyymbuLtjDvoxle9Ldj8YYIMoleORapIOoqBrjRw", Settings.Announce.Twitter.oAuthKey, Settings.Announce.Twitter.oAuthSecret);
}