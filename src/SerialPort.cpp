#include "SerialPort.hpp"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace
{

speed_t baudrateToSpeed(int baudrate)
{
    switch (baudrate)
    {
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default:     return B115200;
    }
}

} // namespace

SerialPort::SerialPort(const std::string &device, int baudrate)
    : device_(device),
      baudrate_(baudrate)
{
}

SerialPort::~SerialPort()
{
    closePort();
}

bool SerialPort::openPort()
{
    fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (fd_ < 0)
    {
        return false;
    }

    termios tty{};

    if (::tcgetattr(fd_, &tty) != 0)
    {
        closePort();
        return false;
    }

    const speed_t speed = baudrateToSpeed(baudrate_);
    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;   // no parity
    tty.c_cflag &= ~CSTOPB;   // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;       // 8 data bits

    // Raw mode: no echo, no canonical line buffering, no ISIG/IXON.
    // Forgetting ICANON is the classic "my binary frames arrive mangled" bug.
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_iflag = 0;

    // Harmless under O_NONBLOCK, but states the intent explicitly.
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (::tcsetattr(fd_, TCSANOW, &tty) != 0)
    {
        closePort();
        return false;
    }

    ::tcflush(fd_, TCIOFLUSH);

    return true;
}

void SerialPort::closePort()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

ssize_t SerialPort::readData(uint8_t *buf, size_t len)
{
    return ::read(fd_, buf, len);
}

ssize_t SerialPort::writeData(const uint8_t *buf, size_t len)
{
    return ::write(fd_, buf, len);
}
