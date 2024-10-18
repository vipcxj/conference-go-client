#include "cfgo/uvc/dev.hpp"
#include "cfgo/uvc/err.hpp"

namespace cfgo
{
    namespace uvc
    {
        Device::Device(uvc_device_t * raw_dev) noexcept: m_raw_dev(raw_dev)
        {
            if (m_raw_dev)
            {
                uvc_ref_device(m_raw_dev);
            }
        }
        Device::Device(const Device & other) noexcept: Device(other.m_raw_dev) {}
        Device & Device::operator= (const Device & other) noexcept
        {
            m_raw_dev = other.m_raw_dev;
            if (m_raw_dev)
            {
                uvc_ref_device(m_raw_dev);
            }
            return *this;
        }
        Device::Device(Device && other) noexcept: m_raw_dev(other.m_raw_dev)
        {
            other.m_raw_dev = nullptr;
        }
        Device & Device::operator= (Device && other) noexcept
        {
            m_raw_dev = other.m_raw_dev;
            other.m_raw_dev = nullptr;
            return *this;
        }
        Device::~Device()
        {
            if (m_raw_dev)
            {
                uvc_unref_device(m_raw_dev);
            }
        }
        DeviceHandle Device::open()
        {
            uvc_device_handle_t * raw_dev = nullptr;
            check_err(uvc_open(m_raw_dev, &raw_dev), "unable to open the device, ");
            return { raw_dev };
        }

        DeviceHandle::DeviceHandle(uvc_device_handle_t * raw_handle) noexcept: m_raw_handle(raw_handle) {}
        DeviceHandle::DeviceHandle(DeviceHandle && other) noexcept: m_raw_handle(other.m_raw_handle) {
            other.m_raw_handle = nullptr;
        }
        DeviceHandle & DeviceHandle::operator= (DeviceHandle && other) noexcept
        {
            m_raw_handle = other.m_raw_handle;
            other.m_raw_handle = nullptr;
            return *this;
        }
        DeviceHandle::~DeviceHandle()
        {
            if (m_raw_handle)
            {
                uvc_close(m_raw_handle);
            }
        }
        void DeviceHandle::check_valid() const
        {
            if (!m_raw_handle)
            {
                throw cpptrace::invalid_argument("invalid device handle");
            }
        }
    } // namespace uvc
    
} // namespace cfgo
