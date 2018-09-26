/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "cybertron/record/record_reader.h"

namespace apollo {
namespace cybertron {
namespace record {

RecordReader::~RecordReader() {
}

RecordReader::RecordReader(const std::shared_ptr<RecordFileReader>& file, 
                           uint64_t begin_time,
                           uint64_t end_time,
                           const std::set<std::string>& channels) {
  begin_time_ = begin_time;
  end_time_ = end_time;
  channels_ = channels;
  file_reader_ = file;

  header_ = file_reader_->GetHeader();
  file_reader_->Reset();
  if (begin_time_ < header_.begin_time()) {
    begin_time_ = header_.begin_time();
  }

  if (end_time_ > header_.end_time()) {
    end_time_ = header_.end_time();
  }

  if (begin_time_ > end_time_) {
    AERROR << "Begin time must be earlier than end time"
           << ", begin_time_=" << begin_time_ << ", end_time_=" << end_time_;
  } 
}

bool RecordReader::ReadMessage(RecordMessage* message) {
  while (index_ < chunk_.messages_size()) {
    const auto& next_message = chunk_.messages(index_++);
    uint64_t time = next_message.time();
    if (time > end_time_) {
      return false;
    }
    if (time < begin_time_) {
      continue;
    }

    const std::string& channel_name = next_message.channel_name();
    if (!channels_.empty() && channels_.count(channel_name) == 0) {
      continue;
    }
    OnNewMessage(channel_name);
    message->channel_name = channel_name;
    message->content = next_message.content();
    message->time = time;
    return true;
  }

  if (ReadNextChunk()) {
    index_ = 0;
    return ReadMessage(message);
  }
  return false;
}

bool RecordReader::ReadNextChunk() {
  bool skip_next_chunk_body = false;
  Section section;
  while (file_reader_->ReadSection(&section)) {
    switch (section.type) {
      case SectionType::SECTION_INDEX: {
        file_reader_->SkipSection(section.size);
        break;
      }
      case SectionType::SECTION_CHANNEL: {
        ADEBUG << "Read channel section of size: " << section.size;
        Channel channel;
        if (!file_reader_->ReadSection<Channel>(section.size, &channel)) {
          AERROR << "Failed to read channel section.";
          return false;
        }
        OnNewChannel(channel.name(), channel.message_type(), channel.proto_desc());
        break;
      }
      case SectionType::SECTION_CHUNK_HEADER: {
        ADEBUG << "Read chunk header section of size: " << section.size;
        ChunkHeader header;
        if (!file_reader_->ReadSection<ChunkHeader>(section.size, &header)) {
          AERROR << "Failed to read chunk header section.";
          return false;
        }
        if (begin_time_ > header.end_time()) {
          return false;
        }
        if (end_time_ < header.begin_time()) {
          skip_next_chunk_body = true;
        }
        break;
      }
      case SectionType::SECTION_CHUNK_BODY: {
        if (skip_next_chunk_body) {
          file_reader_->SkipSection(section.size);
          skip_next_chunk_body = false;
          break;
        }
        if (!file_reader_->ReadSection<ChunkBody>(section.size, &chunk_)) {
          AERROR << "Failed to read chunk body section.";
          return false;
        }
        return true;
      }
      default: {
        AERROR << "Invalid section type: " << section.type;
        return false;
      }
    }
  }
  return false;
}

}  // namespace record
}  // namespace cybertron
}  // namespace apollo
