#include "processors/pcap_buffer.h"

namespace task::processors::mt_buffer {

void PCAPBuffer::start_buffering() {
  producer_thread_ = std::thread([this]() {
    std::cout << "# Start buffering " << std::endl;
    is_started_.store(true, std::memory_order_release);

    size_t packet_nr{1};
    while (current_offset_ < file_size_ &&
           is_started_.load(std::memory_order_acquire)) {
      size_t bytes_to_read = BATCH_SIZE;
      if (current_offset_ + BATCH_SIZE > file_size_) {
        bytes_to_read = file_size_ - current_offset_;
      }

      if constexpr (ENABLE_DEBUGGING) {
        std::cout << "Remaining bytes to process... " << std::dec
                  << bytes_to_read << std::endl;
      }

      if (!file_handle_.read((char *)local_buffer_.data(), bytes_to_read)) {
        throw std::runtime_error("Cannot read from the PCAP file.");
      }

      // buffer as many packets as possible that fit the BATCH SIZE
      BufferedPackets buffered_packets{};
      size_t offset{0};
      buffered_packets.start_packet_number = packet_nr;
      while (current_offset_ < file_size_ && offset < BATCH_SIZE) {
        pcap::types::pcaprec_hdr_s packet_header;
        std::span<std::byte> buffer_2{local_buffer_.data() + offset,
                                      sizeof(packet_header)};

        std::memcpy(&packet_header, buffer_2.data(), buffer_2.size());

        if constexpr (ENABLE_DEBUGGING) {
          std::cout << "usec: " << std::hex << packet_header.ts_usec
                    << std::endl;
          std::cout << "captured length: " << std::hex
                    << packet_header.captured_length << std::endl;
          std::cout << "original length: " << std::hex << packet_header.orig_len
                    << std::endl;
        }

        offset += sizeof(packet_header);

        if (offset + packet_header.captured_length >= BATCH_SIZE) {
          break;
        }

        if constexpr (ENABLE_DEBUGGING) {
          utility::hex_dump(local_buffer_.data() + offset,
                            packet_header.captured_length, std::cout);
        }
        buffered_packets.packets.emplace_back(
            local_buffer_.begin() + offset,
            local_buffer_.begin() + offset + packet_header.captured_length);
        buffered_packets.number_packets++;

        offset += packet_header.captured_length;
        current_offset_ +=
            sizeof(packet_header) + packet_header.captured_length;
        packet_nr++;
      }

      // Critical section
      {
        std::scoped_lock file_lock(chuncks_mutex);
        pcap_data_chunks_.push_back(buffered_packets);
      }

      double processed_percentage =
          100 *
          static_cast<double>((double)current_offset_ / (double)file_size_);
      std::cout << "Processed percentage> : " << processed_percentage
                << std::endl;
      file_handle_.seekg(current_offset_);
    }

    is_started_.store(false, std::memory_order_release);
  });

  return;
}

void PCAPBuffer::stop() {
  std::cout << "# stop buffering " << std::endl;
  is_started_.store(false, std::memory_order_release);
  std::cout << "# closing pcap file " << std::endl;
  file_handle_.close();
}
} // namespace task::processors::mt_buffer