#pragma once

#include <Arduino.h>

struct MqttOutboxItem {
  String topic;
  String payload;
  uint32_t queuedMs = 0;
};

struct MqttOutboxConfig {
  size_t capacity = 32;          // logical cap (bounded by MAX_ITEMS)
  bool dropOldestWhenFull = true;
};

class MqttOutbox {
public:
  static constexpr size_t MAX_ITEMS = 64;

  void configure(const MqttOutboxConfig& cfg) {
    cfg_ = cfg;
    if (cfg_.capacity == 0) cfg_.capacity = 1;
    if (cfg_.capacity > MAX_ITEMS) cfg_.capacity = MAX_ITEMS;
    trimToCapacity_();
  }

  void reset() {
    head_ = 0;
    count_ = 0;
    drops_ = 0;
    sent_ = 0;
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
      items_[i] = MqttOutboxItem{};
    }
  }

  bool enqueue(const String& topic, const String& payload, uint32_t nowMs) {
    if (count_ >= cfg_.capacity) {
      if (!cfg_.dropOldestWhenFull) {
        drops_++;
        return false;
      }
      popFront_();
      drops_++;
    }

    size_t tail = (head_ + count_) % MAX_ITEMS;
    items_[tail].topic = topic;
    items_[tail].payload = payload;
    items_[tail].queuedMs = nowMs;
    count_++;
    return true;
  }

  bool hasItems() const { return count_ > 0; }
  size_t size() const { return count_; }
  size_t drops() const { return drops_; }
  size_t sent() const { return sent_; }
  size_t capacity() const { return cfg_.capacity; }

  const MqttOutboxItem* front() const {
    if (!count_) return nullptr;
    return &items_[head_];
  }

  void popFront() {
    if (!count_) return;
    popFront_();
    sent_++;
  }

private:
  void popFront_() {
    items_[head_] = MqttOutboxItem{};
    head_ = (head_ + 1) % MAX_ITEMS;
    count_--;
  }

  void trimToCapacity_() {
    while (count_ > cfg_.capacity) {
      popFront_();
      drops_++;
    }
  }

  MqttOutboxConfig cfg_{};
  MqttOutboxItem items_[MAX_ITEMS];
  size_t head_ = 0;
  size_t count_ = 0;
  size_t drops_ = 0;
  size_t sent_ = 0;
};
