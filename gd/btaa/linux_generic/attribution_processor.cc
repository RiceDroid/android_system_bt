/*
 * Copyright 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "btaa/attribution_processor.h"

#include "os/log.h"

namespace bluetooth {
namespace activity_attribution {

void AttributionProcessor::OnBtaaPackets(std::vector<BtaaHciPacket> btaa_packets) {
  AddressActivityKey key;

  for (auto& btaa_packet : btaa_packets) {
    key.address = btaa_packet.address;
    key.activity = btaa_packet.activity;

    if (wakelock_duration_aggregator_.find(key) == wakelock_duration_aggregator_.end()) {
      wakelock_duration_aggregator_[key] = {};
    }
    wakelock_duration_aggregator_[key].byte_count += btaa_packet.byte_count;

    if (wakeup_) {
      wakelock_duration_aggregator_[key].wakeup_count += 1;
      wakeup_aggregator_.Push(std::move(WakeupDescriptor(btaa_packet.activity, btaa_packet.address)));
    }
  }
  wakeup_ = false;
}

void AttributionProcessor::OnWakelockReleased(uint32_t duration_ms) {
  uint32_t total_byte_count = 0;
  uint32_t ms_per_byte = 0;

  for (auto& it : wakelock_duration_aggregator_) {
    total_byte_count += it.second.byte_count;
  }

  if (total_byte_count == 0) {
    return;
  }

  ms_per_byte = duration_ms / total_byte_count;
  for (auto& it : wakelock_duration_aggregator_) {
    it.second.wakelock_duration = ms_per_byte * it.second.byte_count;
    if (btaa_aggregator_.find(it.first) == btaa_aggregator_.end()) {
      btaa_aggregator_[it.first] = {};
    }

    btaa_aggregator_[it.first].wakeup_count += it.second.wakeup_count;
    btaa_aggregator_[it.first].byte_count += it.second.byte_count;
    btaa_aggregator_[it.first].wakelock_duration += it.second.wakelock_duration;
  }
  wakelock_duration_aggregator_.clear();
}

void AttributionProcessor::OnWakeup() {
  if (wakeup_) {
    LOG_INFO("Previous wakeup notification is not consumed.");
  }
  wakeup_ = true;
}

void AttributionProcessor::Dump(
    std::promise<flatbuffers::Offset<ActivityAttributionData>> promise, flatbuffers::FlatBufferBuilder* fb_builder) {
  auto title = fb_builder->CreateString("----- BTAA Dumpsys -----");
  ActivityAttributionDataBuilder builder(*fb_builder);
  builder.add_title(title);

  WakeupAttributionDataBuilder wakeup_attribution_builder(*fb_builder);
  auto wakeup_title = fb_builder->CreateString("----- Wakeup Attribution Dumpsys -----");
  wakeup_attribution_builder.add_title(wakeup_title);

  std::vector<common::TimestampedEntry<WakeupDescriptor>> wakeup_aggregator = wakeup_aggregator_.Pull();
  wakeup_attribution_builder.add_num_wakeup(wakeup_aggregator.size());

  std::vector<flatbuffers::Offset<WakeupEntry>> wakeup_entry_offsets;
  for (auto wakeup : wakeup_aggregator) {
    WakeupEntryBuilder wakeup_entry_builder(*fb_builder);
    wakeup_entry_builder.add_wakeup_time(wakeup.timestamp);
    wakeup_entry_builder.add_activity(fb_builder->CreateString((CONVERT_ACTIVITY_TO_STR(wakeup.entry.activity_))));
    wakeup_entry_builder.add_address(fb_builder->CreateString(wakeup.entry.address_.ToString()));
    wakeup_entry_offsets.push_back(wakeup_entry_builder.Finish());
  }
  auto wakeup_entries = fb_builder->CreateVector(wakeup_entry_offsets);
  wakeup_attribution_builder.add_wakeup_attribution(wakeup_entries);

  builder.add_wakeup_attribution_data(wakeup_attribution_builder.Finish());

  flatbuffers::Offset<ActivityAttributionData> dumpsys_data = builder.Finish();
  promise.set_value(dumpsys_data);
}

}  // namespace activity_attribution
}  // namespace bluetooth
