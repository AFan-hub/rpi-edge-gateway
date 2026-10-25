#ifndef SERIAL_PORT_HPP
#define SERIAL_PORT_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include <sys/types.h>

// RAII wrapper over a termios-configured, non-blocking serial device.
// The destructor closes the fd, so a forgotten close() cannot leak it.
class SerialPort
{
public:
    SerialPort(const std::string &device, int baudrate);
    ~SerialPort();

    SerialPort(const SerialPort &) = delete;
    SerialPort &operator=(const SerialPort &) = delete;

    bool openPort();
    void closePort();

    // Returns bytes read, 0, or -1 with errno == EAGAIN when no data is
    // pending (the fd is O_NONBLOCK).
    ssize_t readData(uint8_t *buf, size_t len);
    ssize_t writeData(const uint8_t *buf, size_t len);

    int fd() const { return fd_; }

private:
    int fd_ = -1;
    std::string device_;
    int baudrate_;
};

#endif
