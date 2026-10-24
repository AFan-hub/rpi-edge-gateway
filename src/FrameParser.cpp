#include "FrameParser.hpp"

#include "Crc16.hpp"

FrameParser::FrameParser(Callback onFrame)
    : onFrame_(std::move(onFrame))
{
}

void FrameParser::feed(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        processByte(data[i]);
    }
}

void FrameParser::processByte(uint8_t byte)
{
    switch (state_)
    {
    case State::SearchHead1:
        if (byte == protocol::HEAD1)
        {
            state_ = State::SearchHead2;
        }
        break;

    case State::SearchHead2:
        if (byte == protocol::HEAD2)
        {
            state_ = State::ReadHeader;
            headerIdx_ = 0;
        }
        else if (byte != protocol::HEAD1)
        {
            // Neither the second head byte nor a new first head byte.
            state_ = State::SearchHead1;
        }
        break;

    case State::ReadHeader:
        header_[headerIdx_++] = byte;

        if (headerIdx_ == sizeof(header_))
        {
            version_    = header_[0];
            command_    = header_[1];
            payloadLen_ = static_cast<uint16_t>(header_[2] | (header_[3] << 8));

            // A corrupt LEN must not make us swallow up to 64 KiB of stream
            // before resyncing: cap it and drop back to head search now.
            if (version_ != protocol::VERSION ||
                payloadLen_ > protocol::MAX_PAYLOAD)
            {
                ++stats_.resyncs;
                state_ = State::SearchHead1;
                break;
            }

            payload_.assign(payloadLen_, 0);
            payloadIdx_ = 0;
            crcIdx_ = 0;
            state_ = (payloadLen_ > 0) ? State::ReadPayload : State::ReadCrc;
        }
        break;

    case State::ReadPayload:
        payload_[payloadIdx_++] = byte;

        if (payloadIdx_ == payloadLen_)
        {
            state_ = State::ReadCrc;
        }
        break;

    case State::ReadCrc:
        crcBuf_[crcIdx_++] = byte;

        if (crcIdx_ == sizeof(crcBuf_))
        {
            const uint16_t received =
                static_cast<uint16_t>(crcBuf_[0] | (crcBuf_[1] << 8));

            std::vector<uint8_t> crcInput;
            crcInput.reserve(sizeof(header_) + payloadLen_);
            crcInput.insert(crcInput.end(), header_, header_ + sizeof(header_));
            crcInput.insert(crcInput.end(), payload_.begin(), payload_.end());

            if (received == crc::ccittFalse(crcInput.data(), crcInput.size()))
            {
                protocol::Frame frame;
                frame.version = version_;
                frame.cmd = static_cast<protocol::Command>(command_);
                frame.payload = std::move(payload_);

                ++stats_.framesOk;

                if (onFrame_)
                {
                    onFrame_(frame);
                }
            }
            else
            {
                ++stats_.crcErrors;
            }

            // Good or bad: the next frame starts from a fresh head search.
            state_ = State::SearchHead1;
        }
        break;
    }
}
