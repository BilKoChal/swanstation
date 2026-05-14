#include "sio.h"
#include "common/state_wrapper.h"
#include "controller.h"
#include "host_interface.h"
#include "interrupt_controller.h"
#include "memory_card.h"

SIO g_sio;

SIO::SIO() = default;

SIO::~SIO() = default;

void SIO::Initialize()
{
  Reset();
}

void SIO::Shutdown() {}

void SIO::Reset()
{
  SoftReset();
}

bool SIO::DoState(StateWrapper& sw)
{
  sw.Do(&m_SIO_CTRL.bits);
  sw.Do(&m_SIO_STAT.bits);
  sw.Do(&m_SIO_MODE.bits);
  sw.Do(&m_SIO_BAUD);

  return !sw.HasError();
}

u32 SIO::ReadRegister(u32 offset)
{
  switch (offset)
  {
    case 0x00: // SIO_DATA
    {
      const u8 value = 0xFF;
      return (static_cast<u32>(value) | (static_cast<u32>(value) << 8) | (static_cast<u32>(value) << 16) |
              (static_cast<u32>(value) << 24));
    }

    case 0x04: // SIO_STAT
    {
      const u32 bits = m_SIO_STAT.bits;
      return bits;
    }

    case 0x08: // SIO_MODE
      return static_cast<u32>(m_SIO_MODE.bits);

    case 0x0A: // SIO_CTRL
      return static_cast<u32>(m_SIO_CTRL.bits);

    case 0x0E: // SIO_BAUD
      return static_cast<u32>(m_SIO_BAUD);

    default:
      return UINT32_C(0xFFFFFFFF);
  }
}

void SIO::WriteRegister(u32 offset, u32 value)
{
  switch (offset)
  {
    case 0x0A: // SIO_CTRL
      m_SIO_CTRL.bits = static_cast<u16>(value);
      if (m_SIO_CTRL.RESET)
        SoftReset();

      break;

    case 0x08: // SIO_MODE
      m_SIO_MODE.bits = static_cast<u16>(value);
      break;

    case 0x0E:
      m_SIO_BAUD = static_cast<u16>(value);
      break;

    case 0x00: // SIO_DATA
    default:
      break;
  }
}

void SIO::SoftReset()
{
  m_SIO_CTRL.bits = 0;
  m_SIO_STAT.bits = 0;
  m_SIO_STAT.DSRINPUTLEVEL = true;
  m_SIO_STAT.CTSINPUTLEVEL = true;
  m_SIO_STAT.TXDONE = true;
  m_SIO_STAT.TXRDY = true;
  m_SIO_MODE.bits = 0;
  m_SIO_BAUD = 0xDC;
}
