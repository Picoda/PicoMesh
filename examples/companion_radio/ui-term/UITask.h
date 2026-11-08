#pragma once

// Terminal-style UI for the plain Pico + ST7789 (+ optional CardKB and one push button).
// contacts, radio presets, quick replies) but are drawn as text pages by TermDisplay, with all
// state in statically allocated buffers (no heap use after start-up).

#include "../AbstractUITask.h"
#include <helpers/SensorManager.h>

class UITask : public AbstractUITask {
public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial) { }

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // AbstractUITask
  void notify(UIEventType t = UIEventType::none) override { }
  void loop() override;

  // MyMesh::Listener
  void onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel_details, const char* text) override;
  void onQueueSizeChanged(int msgcount) override { }
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onACKRecv(uint32_t ack_crc) override;
};
