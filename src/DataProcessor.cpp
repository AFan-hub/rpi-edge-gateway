#include "DataProcessor.hpp"

#include <cstdio>

#include "Logger.hpp"

void DataProcessor::handle(const protocol::Frame &frame)
{
    if (frame.cmd != protocol::Command::SensorData)
    {
        return;
    }

    protocol::SensorPayload data{};

    if (!protocol::decodeSensor(frame, data))
    {
        logging::warn("sensor frame with unexpected payload length dropped");
        return;
    }

    last_ = data;
    ++processed_;

    if (sink_)
    {
        sink_(data);
    }

    char line[128];
    std::snprintf(line, sizeof(line),
                  "ts=%u s1=%.2f s2=%.2f status=0x%04X",
                  static_cast<unsigned>(data.timestamp_ms),
                  static_cast<double>(data.sensor_1),
                  static_cast<double>(data.sensor_2),
                  static_cast<unsigned>(data.status));
    logging::info(line);
}
