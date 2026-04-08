// moria_dualsense.h — PS5 DualSense reader via DirectInput + raw HID fallback.
// DirectInput provides shared access (game doesn't lock us out).
// XInput does NOT see DualSense natively on Epic Games Store titles.

#pragma once

#include <Windows.h>
#include <hidsdi.h>
#include <SetupAPI.h>
#include <cstdint>
#include <cstring>
#include <vector>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

// Sony DualSense identifiers
#define DS_VID              0x054C
#define DS_PID_STANDARD     0x0CE6
#define DS_PID_EDGE         0x0DF2

// Button bitmasks — Buttons[0] (data offset 0x07)
#define DS_BTN0_DPAD_MASK   0x0F
#define DS_BTN0_SQUARE      0x10
#define DS_BTN0_CROSS       0x20
#define DS_BTN0_CIRCLE      0x40
#define DS_BTN0_TRIANGLE    0x80

// Button bitmasks — Buttons[1] (data offset 0x08)
#define DS_BTN1_L1          0x01
#define DS_BTN1_R1          0x02
#define DS_BTN1_L2          0x04
#define DS_BTN1_R2          0x08
#define DS_BTN1_CREATE      0x10
#define DS_BTN1_OPTIONS     0x20
#define DS_BTN1_L3          0x40
#define DS_BTN1_R3          0x80

// Button bitmasks — Buttons[2] (data offset 0x09)
#define DS_BTN2_PS          0x01
#define DS_BTN2_TOUCHPAD    0x02
#define DS_BTN2_MUTE        0x04

// D-pad hat switch values (Buttons[0] bits 0-3)
#define DS_DPAD_N           0x0
#define DS_DPAD_NE          0x1
#define DS_DPAD_E           0x2
#define DS_DPAD_SE          0x3
#define DS_DPAD_S           0x4
#define DS_DPAD_SW          0x5
#define DS_DPAD_W           0x6
#define DS_DPAD_NW          0x7
#define DS_DPAD_NONE        0x8

enum class DSConnection : uint8_t { None = 0, USB = 1, Bluetooth = 2 };

struct DSState
{
    // Sticks: 0..255 raw (128 = center)
    uint8_t leftStickX{128};
    uint8_t leftStickY{128};
    uint8_t rightStickX{128};
    uint8_t rightStickY{128};

    // Triggers: 0..255
    uint8_t l2Trigger{0};
    uint8_t r2Trigger{0};

    // Raw button bytes
    uint8_t buttons0{0};   // D-pad + face
    uint8_t buttons1{0};   // shoulders + menu + sticks
    uint8_t buttons2{0};   // PS + touchpad + mute

    // Convenience: D-pad
    bool dpadUp{false};
    bool dpadDown{false};
    bool dpadLeft{false};
    bool dpadRight{false};

    // Convenience: face buttons (PlayStation names)
    bool cross{false};      // bottom (Xbox A position)
    bool circle{false};     // right (Xbox B position)
    bool square{false};     // left (Xbox X position)
    bool triangle{false};   // top (Xbox Y position)

    // Convenience: shoulders/triggers/sticks
    bool l1{false};         // LB equivalent
    bool r1{false};         // RB equivalent
    bool l2Btn{false};      // digital L2 click
    bool r2Btn{false};      // digital R2 click
    bool l3{false};
    bool r3{false};

    // Convenience: menu/system
    bool create{false};     // Share/Create button
    bool options{false};    // Options/Start button
    bool ps{false};         // PS button (home)
    bool touchpad{false};   // Touchpad click
    bool mute{false};       // Mute button

    bool connected{false};
    DSConnection connType{DSConnection::None};
};

class DualSenseReader
{
    HANDLE       m_handle{INVALID_HANDLE_VALUE};
    DSConnection m_connType{DSConnection::None};
    uint8_t      m_buffer[128]{};
    int          m_dataOffset{0};
    int          m_reportLen{0};
    bool         m_btSimple{false};      // true = 10-byte BT simple report
    bool         m_connected{false};
    ULONGLONG    m_lastScanTime{0};
    OVERLAPPED   m_overlapped{};         // for non-blocking ReadFile
    bool         m_readPending{false};   // overlapped read in progress

public:
    ~DualSenseReader() { close(); }

    void close()
    {
        if (m_readPending)
        {
            CancelIo(m_handle);
            m_readPending = false;
        }
        if (m_overlapped.hEvent)
        {
            CloseHandle(m_overlapped.hEvent);
            m_overlapped = {};
        }
        if (m_handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }
        m_connected = false;
        m_connType = DSConnection::None;
        m_btSimple = false;
        m_reportLen = 0;
    }

    bool scan()
    {
        if (m_connected) return true;

        ULONGLONG now = GetTickCount64();
        if (now - m_lastScanTime < 2000) return false;
        m_lastScanTime = now;

        GUID hidGuid{};
        HidD_GetHidGuid(&hidGuid);

        HDEVINFO devInfo = SetupDiGetClassDevsW(
            &hidGuid, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
        if (devInfo == INVALID_HANDLE_VALUE) return false;

        SP_DEVICE_INTERFACE_DATA ifData{};
        ifData.cbSize = sizeof(ifData);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, &hidGuid, i, &ifData); ++i)
        {
            DWORD reqSize = 0;
            SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, NULL, 0, &reqSize, NULL);
            if (reqSize == 0) continue;

            std::vector<uint8_t> detailBuf(reqSize);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, reqSize, NULL, NULL))
                continue;

            HANDLE hDev = CreateFileW(detail->DevicePath,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

            if (hDev == INVALID_HANDLE_VALUE) continue;

            HIDD_ATTRIBUTES attrs{};
            attrs.Size = sizeof(attrs);
            if (HidD_GetAttributes(hDev, &attrs))
            {
                if (attrs.VendorID == DS_VID &&
                    (attrs.ProductID == DS_PID_STANDARD || attrs.ProductID == DS_PID_EDGE))
                {
                    PHIDP_PREPARSED_DATA preparsed = nullptr;
                    if (HidD_GetPreparsedData(hDev, &preparsed))
                    {
                        HIDP_CAPS caps{};
                        HidP_GetCaps(preparsed, &caps);
                        HidD_FreePreparsedData(preparsed);

                        m_reportLen = caps.InputReportByteLength;
                        if (m_reportLen == 64)
                        {
                            m_connType = DSConnection::USB;
                            m_dataOffset = 1;
                            m_btSimple = false;
                        }
                        else if (m_reportLen >= 78)
                        {
                            m_connType = DSConnection::Bluetooth;
                            m_dataOffset = 2;
                            m_btSimple = false;
                        }
                        else if (m_reportLen >= 10)
                        {
                            // BT simple mode (10-byte report)
                            m_connType = DSConnection::Bluetooth;
                            m_dataOffset = 1;  // skip report ID
                            m_btSimple = true;
                        }
                        else
                        {
                            CloseHandle(hDev);
                            continue;
                        }

                        m_handle = hDev;
                        m_connected = true;
                        m_overlapped = {};
                        m_overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
                        m_readPending = false;
                        SetupDiDestroyDeviceInfoList(devInfo);
                        return true;
                    }
                }
            }
            CloseHandle(hDev);
        }

        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }

    bool poll(DSState& out)
    {
        out = DSState{};

        if (!m_connected)
        {
            if (!scan()) return false;
        }

        // Non-blocking overlapped read — start a new read if none pending
        if (!m_readPending)
        {
            memset(m_buffer, 0, sizeof(m_buffer));
            ResetEvent(m_overlapped.hEvent);
            DWORD bytesRead = 0;
            BOOL ok = ReadFile(m_handle, m_buffer, m_reportLen, &bytesRead, &m_overlapped);
            if (!ok)
            {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING)
                    m_readPending = true;
                else
                {
                    close();
                    return false;
                }
            }
            else
            {
                // Completed synchronously — data is in buffer
                m_readPending = false;
            }
        }

        // Check if overlapped read completed (non-blocking wait, 0 timeout)
        if (m_readPending)
        {
            DWORD result = WaitForSingleObject(m_overlapped.hEvent, 0);
            if (result == WAIT_OBJECT_0)
            {
                DWORD bytesRead = 0;
                GetOverlappedResult(m_handle, &m_overlapped, &bytesRead, FALSE);
                m_readPending = false;
                if (bytesRead == 0)
                {
                    close();
                    return false;
                }
            }
            else
            {
                // No data yet — return last known state as "connected but no new data"
                out.connected = true;
                out.connType = m_connType;
                return true;  // keep last state, don't block
            }
        }

        // Parse the report based on format
        const uint8_t* d = m_buffer + m_dataOffset;

        if (m_btSimple)
        {
            // BT simple 10-byte report: sticks at 0-3, buttons at 4-6, triggers at 7-8
            out.leftStickX  = d[0];
            out.leftStickY  = d[1];
            out.rightStickX = d[2];
            out.rightStickY = d[3];
            out.buttons0    = d[4];
            out.buttons1    = d[5];
            out.buttons2    = d[6];
            out.l2Trigger   = d[7];
            out.r2Trigger   = d[8];
        }
        else
        {
            // USB / BT extended: sticks at 0-3, triggers at 4-5, buttons at 7-9
            out.leftStickX  = d[0x00];
            out.leftStickY  = d[0x01];
            out.rightStickX = d[0x02];
            out.rightStickY = d[0x03];
            out.l2Trigger   = d[0x04];
            out.r2Trigger   = d[0x05];
            out.buttons0    = d[0x07];
            out.buttons1    = d[0x08];
            out.buttons2    = d[0x09];
        }

        // D-pad
        uint8_t dpad = out.buttons0 & DS_BTN0_DPAD_MASK;
        out.dpadUp    = (dpad == DS_DPAD_N  || dpad == DS_DPAD_NE || dpad == DS_DPAD_NW);
        out.dpadDown  = (dpad == DS_DPAD_S  || dpad == DS_DPAD_SE || dpad == DS_DPAD_SW);
        out.dpadLeft  = (dpad == DS_DPAD_W  || dpad == DS_DPAD_NW || dpad == DS_DPAD_SW);
        out.dpadRight = (dpad == DS_DPAD_E  || dpad == DS_DPAD_NE || dpad == DS_DPAD_SE);

        // Face buttons (same bitmasks for both formats)
        out.square   = (out.buttons0 & DS_BTN0_SQUARE)   != 0;
        out.cross    = (out.buttons0 & DS_BTN0_CROSS)    != 0;
        out.circle   = (out.buttons0 & DS_BTN0_CIRCLE)   != 0;
        out.triangle = (out.buttons0 & DS_BTN0_TRIANGLE) != 0;

        // Shoulders
        out.l1    = (out.buttons1 & DS_BTN1_L1)  != 0;
        out.r1    = (out.buttons1 & DS_BTN1_R1)  != 0;
        out.l2Btn = (out.buttons1 & DS_BTN1_L2)  != 0;
        out.r2Btn = (out.buttons1 & DS_BTN1_R2)  != 0;
        out.l3    = (out.buttons1 & DS_BTN1_L3)  != 0;
        out.r3    = (out.buttons1 & DS_BTN1_R3)  != 0;

        // Menu/system
        out.create   = (out.buttons1 & DS_BTN1_CREATE)  != 0;
        out.options  = (out.buttons1 & DS_BTN1_OPTIONS) != 0;
        out.ps       = (out.buttons2 & DS_BTN2_PS)       != 0;
        out.touchpad = (out.buttons2 & DS_BTN2_TOUCHPAD) != 0;
        out.mute     = (out.buttons2 & DS_BTN2_MUTE)     != 0;

        out.connected = true;
        out.connType = m_connType;

        // Immediately start next read
        m_readPending = false;

        return true;
    }

    bool isConnected() const { return m_connected; }
    DSConnection connectionType() const { return m_connType; }
    int reportLength() const { return m_reportLen; }
    bool isBtSimple() const { return m_btSimple; }
    int dataOffset() const { return m_dataOffset; }
    const uint8_t* rawBuffer() const { return m_buffer; }
};


// ---- DirectInput-based gamepad reader ----
// Works with DualSense, DualShock 4, and any DirectInput-compatible controller.
// Uses shared access so the game and mod can read simultaneously.

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

struct DIGamepadState
{
    // Sticks: -1000..+1000 (0 = center)
    long lX{0}, lY{0};    // left stick
    long rX{0}, rY{0};    // right stick

    // Triggers
    long lTrigger{0}, rTrigger{0};

    // D-pad (POV hat): -1 = centered, 0=N, 9000=E, 18000=S, 27000=W
    DWORD pov{0xFFFFFFFF};
    bool dpadUp{false};
    bool dpadDown{false};
    bool dpadLeft{false};
    bool dpadRight{false};

    // Buttons (up to 32)
    bool buttons[32]{};
    int  buttonCount{0};

    bool connected{false};
};

class DIGamepadReader
{
    IDirectInput8W*       m_di{nullptr};
    IDirectInputDevice8W* m_device{nullptr};
    bool                  m_connected{false};
    bool                  m_initialized{false};
    ULONGLONG             m_lastScanTime{0};

    // Callback for device enumeration
    static BOOL CALLBACK EnumCallback(const DIDEVICEINSTANCEW* inst, void* ctx)
    {
        auto* self = static_cast<DIGamepadReader*>(ctx);
        HRESULT hr = self->m_di->CreateDevice(inst->guidInstance, &self->m_device, NULL);
        if (SUCCEEDED(hr))
            return DIENUM_STOP;  // found one, stop
        return DIENUM_CONTINUE;
    }

public:
    ~DIGamepadReader() { close(); }

    void close()
    {
        if (m_device)
        {
            m_device->Unacquire();
            m_device->Release();
            m_device = nullptr;
        }
        if (m_di)
        {
            m_di->Release();
            m_di = nullptr;
        }
        m_connected = false;
        m_initialized = false;
    }

    bool scan()
    {
        if (m_connected && m_device) return true;

        ULONGLONG now = GetTickCount64();
        if (now - m_lastScanTime < 2000) return false;
        m_lastScanTime = now;

        if (!m_initialized)
        {
            HRESULT hr = DirectInput8Create(
                GetModuleHandle(NULL), DIRECTINPUT_VERSION,
                IID_IDirectInput8W, (void**)&m_di, NULL);
            if (FAILED(hr)) return false;
            m_initialized = true;
        }

        if (m_device)
        {
            m_device->Unacquire();
            m_device->Release();
            m_device = nullptr;
        }

        // Enumerate game controllers
        m_di->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumCallback, this,
                          DIEDFL_ATTACHEDONLY);

        if (!m_device) return false;

        // Set data format to standard joystick
        m_device->SetDataFormat(&c_dfDIJoystick2);

        // Non-exclusive background access — shared with the game
        m_device->SetCooperativeLevel(
            GetForegroundWindow(),
            DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);

        m_device->Acquire();
        m_connected = true;
        return true;
    }

    bool poll(DIGamepadState& out)
    {
        out = DIGamepadState{};

        if (!m_connected)
        {
            if (!scan()) return false;
        }

        // Try to re-acquire if lost
        HRESULT hr = m_device->Poll();
        if (FAILED(hr))
        {
            hr = m_device->Acquire();
            if (FAILED(hr)) { m_connected = false; return false; }
            m_device->Poll();
        }

        DIJOYSTATE2 js{};
        hr = m_device->GetDeviceState(sizeof(js), &js);
        if (FAILED(hr))
        {
            m_connected = false;
            return false;
        }

        // Sticks
        out.lX = js.lX;
        out.lY = js.lY;
        out.rX = js.lZ;   // DirectInput maps right stick to Z/Rz
        out.rY = js.lRz;

        // Triggers (may be on different axes depending on controller)
        out.lTrigger = js.lRx;
        out.rTrigger = js.lRy;

        // D-pad (POV hat)
        out.pov = js.rgdwPOV[0];
        if (out.pov != 0xFFFFFFFF)
        {
            // POV is in hundredths of degrees: 0=N, 9000=E, 18000=S, 27000=W
            out.dpadUp    = (out.pov >= 31500 || out.pov <= 4500);
            out.dpadRight = (out.pov >= 4500  && out.pov <= 13500);
            out.dpadDown  = (out.pov >= 13500 && out.pov <= 22500);
            out.dpadLeft  = (out.pov >= 22500 && out.pov <= 31500);
        }

        // Buttons (DualSense has ~14 buttons in DirectInput)
        for (int i = 0; i < 32; i++)
            out.buttons[i] = (js.rgbButtons[i] & 0x80) != 0;

        // Count how many buttons are available
        out.buttonCount = 14;  // typical for DualSense
        out.connected = true;
        return true;
    }

    bool isConnected() const { return m_connected; }
};
