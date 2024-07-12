#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <exception>
#include <vector>
#include <set>
#include <algorithm>
#include <ranges>
#include <map>
#include <chrono>
#include <hidapi.h>
#include <fmt/format.h>
#include <fakekey/fakekey.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define MAX_STR 255
#define WHEEL_STEP 30000
#define TIMEOUT_MS (60 * 1000)

static Display* display;
static Window window;
static FakeKey* fakekey;
static std::set<uint16_t> currentKeyboardState;
static int minKeycode;
static int maxKeycode;
static int lastModifiedKey = 0;

static const std::vector<KeySym> MODIFIERS = {XK_Alt_L, XK_Meta_L, XK_Super_L};

static const std::map<int, std::pair<KeySym, bool>> KEYMAP = {
    {0x01, {XK_F1, false} }, /* SMART INSRT */
    {0x02, {XK_F2, false} }, /* APPEND */
    {0x03, {XK_F3, false} }, /* RIPL O/WR */
    {0x04, {XK_F4, false} }, /* CLOSE UP */
    {0x05, {XK_F5, false} }, /* PLACE ON TOP */
    {0x06, {XK_F6, false} }, /* SRC O/WR */
    {0x07, {XK_F7, false} }, /* IN */
    {0x08, {XK_F8, false} }, /* OUT */
    {0x09, {XK_F9, false} }, /* TRIM IN */
    {0x0a, {XK_F10, false}}, /* TRIM OUT */
    {0x0b, {XK_F11, false}}, /* ROLL */
    {0x0c, {XK_F12, false}}, /* SLIP SRC */
    {0x0d, {XK_F13, false}}, /* SLIP DEST */
    {0x0e, {XK_F14, false}}, /* TRANS DUR */
    {0x0f, {XK_F15, false}}, /* CUT */
    {0x10, {XK_F16, false}}, /* DIS */
    {0x11, {XK_F17, false}}, /* SMTH CUT */

    {0x1a, {XK_F18, false}}, /* SOURCE */
    {0x1b, {XK_F19, false}}, /* TIMELINE */
    {0x1c, {XK_F20, false}}, /* SHTL */
    {0x1d, {XK_F21, false}}, /* JOG */
    {0x1e, {XK_F22, false}}, /* SCRL */

    {0x31, {XK_F1, true}  }, /* ESC */
    {0x1f, {XK_F2, true}  }, /* SYNC BIN */
    {0x2c, {XK_F3, true}  }, /* AUDIO LEVEL */
    {0x2d, {XK_F4, true}  }, /* FULL VIEW */
    {0x22, {XK_F5, true}  }, /* TRANS */
    {0x2f, {XK_F6, true}  }, /* SPLIT */
    {0x2e, {XK_F7, true}  }, /* SNAP */
    {0x2b, {XK_F8, true}  }, /* RIPL DEL */

    {0x33, {XK_F10, true} }, /* CAM1 */
    {0x34, {XK_F11, true} }, /* CAM2 */
    {0x35, {XK_F12, true} }, /* CAM3 */
    {0x36, {XK_F13, true} }, /* CAM4 */
    {0x37, {XK_F14, true} }, /* CAM5 */
    {0x38, {XK_F15, true} }, /* CAM6 */
    {0x39, {XK_F16, true} }, /* CAM7 */
    {0x3a, {XK_F17, true} }, /* CAM8 */
    {0x3b, {XK_F18, true} }, /* CAM9 */
    {0x2c, {XK_F19, true} }, /* LIVE O/WR */
    {0x25, {XK_F20, true} }, /* VIDEO ONLY */
    {0x26, {XK_F21, true} }, /* AUDIO ONLY */
    {0x3c, {XK_F22, true} }, /* STOP/PLAY */
};

static void checkerror(int res)
{
    if (res == -1)
    {
        printf("error\n");
        exit(1);
    }
}

static uint64_t rol8(uint64_t v)
{
    return ((v << 56) | (v >> 8)) & 0xffffffffffffffff;
}

static uint64_t rol8n(uint64_t v, int n)
{
    while (n--)
        v = rol8(v);
    return v;
}

/* Authentication code borrowed from https://github.com/smunaut/blackmagic-misc.
 */
static uint64_t calculateKeyboardResponse(uint64_t challenge)
{
    static const uint64_t auth_even_tbl[] = {
        0x3ae1206f97c10bc8,
        0x2a9ab32bebf244c6,
        0x20a6f8b8df9adf0a,
        0xaf80ece52cfc1719,
        0xec2ee2f7414fd151,
        0xb055adfd73344a15,
        0xa63d2e3059001187,
        0x751bf623f42e0dde,
    };
    static const uint64_t auth_odd_tbl[] = {
        0x3e22b34f502e7fde,
        0x24656b981875ab1c,
        0xa17f3456df7bf8c3,
        0x6df72e1941aef698,
        0x72226f011e66ab94,
        0x3831a3c606296b42,
        0xfd7ff81881332c89,
        0x61a3f6474ff236c6,
    };
    static const uint64_t mask = 0xa79a63f585d37bf0;

    uint64_t n = challenge & 7;
    uint64_t v = rol8n(challenge, n);

    uint64_t k;
    if ((v & 1) == ((0x78 >> n) & 1))
        k = auth_even_tbl[n];
    else
    {
        v = v ^ rol8(v);
        k = auth_odd_tbl[n];
    }

    return v ^ (rol8(v) & mask) ^ k;
}

static uint64_t getInt16(const uint8_t* p)
{
    return ((uint64_t)p[0] << 0) | ((uint64_t)p[1] << 8);
}

static uint32_t getInt32(const uint8_t* p)
{
    return ((uint32_t)getInt16(p) << 0) | ((uint32_t)getInt16(p + 2) << 16);
}

static uint64_t getInt64(const uint8_t* p)
{
    return ((uint64_t)getInt32(p) << 0) | ((uint64_t)getInt32(p + 4) << 32);
}

static void putInt64(uint8_t* p, uint64_t value)
{
    p[0] = value >> 0;
    p[1] = value >> 8;
    p[2] = value >> 16;
    p[3] = value >> 24;
    p[4] = value >> 32;
    p[5] = value >> 40;
    p[6] = value >> 48;
    p[7] = value >> 56;
}

class HidException : public std::exception
{
public:
    HidException(const std::string& s): _message(s) {}

    const char* what() const noexcept override
    {
        return _message.c_str();
    }

private:
    std::string _message;
};

class TimeoutException : public std::exception
{
};

class HidDevice
{
public:
    HidDevice(uint16_t product, uint16_t vendor)
    {
        for (;;)
        {
            _handle = hid_open(product, vendor, nullptr);
            if (_handle)
                break;

            fmt::print("Waiting...\n");
            sleep(1);
        }

        hid_set_nonblocking(_handle, false);
    }

    ~HidDevice()
    {
        if (_handle)
            hid_close(_handle);
    }

    void sendFeatureReport(const std::vector<uint8_t>& report)
    {
        checkError(hid_send_feature_report(_handle, &report[0], report.size()));
    }

    std::vector<uint8_t> recvFeatureReport(int id, int length)
    {
        std::vector<uint8_t> data(10);
        data.at(0) = id;

        int res = hid_get_feature_report(_handle, &data[0], length);
        checkError(res);
        data.resize(res);

        return data;
    }

    void send(const std::vector<uint8_t>& data)
    {
        checkError(hid_write(_handle, &data[0], data.size()));
    }

    std::vector<uint8_t> recv(int msTimeout)
    {
        std::vector<uint8_t> data(64);

        int res = hid_read_timeout(_handle, &data[0], sizeof(data), msTimeout);
        if (res == 0)
            throw TimeoutException();

        checkError(res);
        data.resize(res);

        return data;
    }

private:
    void checkError(int res)
    {
        if (res == -1)
            throw HidException(fmt::format("failed: {}", strerror(errno)));
    }

    hid_device* _handle;
};

static void authenticate(HidDevice& device)
{
    device.sendFeatureReport({6, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    auto challengeFromKeyboard = device.recvFeatureReport(6, 10);

    device.sendFeatureReport({6, 1, 0, 0, 0, 0, 0, 0, 0, 0});
    device.recvFeatureReport(6, 10);

    std::vector<uint8_t> responseToKeyboard = {6, 3, 0, 0, 0, 0, 0, 0, 0, 0};
    putInt64(&responseToKeyboard[2],
        calculateKeyboardResponse(getInt64(&challengeFromKeyboard[2])));
    device.sendFeatureReport(responseToKeyboard);

    auto result = device.recvFeatureReport(6, 10);
    if ((result.at(0) != 6) || (result.at(1) != 4))
        throw HidException("unable to authenticate keyboard");
    fmt::print("Authenticated\n");
}

static void pressReleaseKey(int keynum, bool pressed)
{
    auto it = KEYMAP.find(keynum);
    if (it == KEYMAP.end())
        return;

    KeySym keysym = it->second.first;
    KeySym keysyms[] = {keysym, keysym};

    KeyCode code = XKeysymToKeycode(display, keysym);
    if (!code)
    {
        XChangeKeyboardMapping(display,
            /* first_keycode= */ maxKeycode - 1,
            /* keysyms_per_keycode= */ 2,
            /* keysyms= */ keysyms,
            /* num_codes= */ 1);
        XSync(display, False);
        code = maxKeycode - 1;
    }

    fakekey_send_keyevent(
        fakekey, code, pressed, it->second.second ? FAKEKEYMOD_SHIFT : 0);
}

static void pressReleaseModifiers(bool pressed)
{
    for (KeySym keysym : MODIFIERS)
    {
        fakekey_send_keyevent(
            fakekey, XKeysymToKeycode(display, keysym), pressed, 0);
        XSync(display, false);
    }
}

int main()
{
    display = XOpenDisplay(nullptr);
    window = XDefaultRootWindow(display);
    fakekey = fakekey_init(display);
    atexit(
        []()
        {
            for (uint16_t k : currentKeyboardState)
                pressReleaseKey(k, false);
            pressReleaseModifiers(false);
        });

    XDisplayKeycodes(display, &minKeycode, &maxKeycode);

    hid_init();
    {
        HidDevice device(0x1edb, 0xda0e);
        authenticate(device);

        device.send({3, 0, 0, 0, 0, 0, 0});
        device.send({2, 0xff, 0xff, 0xff, 0xff});
        int32_t sentWheelPosition = 0;
        int32_t wheelPosition = 0;
        int msTimeout = TIMEOUT_MS;
        for (;;)
        {
            try
            {
                auto beforeTime = std::chrono::steady_clock::now();
                auto data = device.recv(msTimeout);
                auto afterTime = std::chrono::steady_clock::now();
                msTimeout -=
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        afterTime - beforeTime)
                        .count();

                switch (data[0])
                {
                    case 3:
                    {
                        /* wheel packet */
                        int32_t delta = getInt32(&data[2]);
                        wheelPosition += delta;
                        for (;;)
                        {
                            int32_t totalDelta =
                                wheelPosition - sentWheelPosition;
                            if (abs(totalDelta) < WHEEL_STEP)
                                break;

                            int button = (totalDelta < 0) ? 4 : 5;
                            sentWheelPosition +=
                                (totalDelta < 0) ? -WHEEL_STEP : WHEEL_STEP;
                            XTestFakeButtonEvent(display, button, true, 0);
                            XSync(display, false);
                            XTestFakeButtonEvent(display, button, false, 0);
                            XSync(display, false);
                        }
                        break;
                    }

                    case 4:
                    {
                        /* Keyboard packet */
                        std::set<uint16_t> newKeyboardState;
                        for (int i = 0; i < 6; i++)
                        {
                            uint16_t keycode = getInt16(&data[1 + i * 2]);
                            if (keycode)
                                newKeyboardState.insert(
                                    getInt16(&data[1 + i * 2]));
                        }

                        std::set<uint16_t> keysPressed;
                        std::ranges::set_difference(newKeyboardState,
                            currentKeyboardState,
                            std::inserter(keysPressed, keysPressed.begin()));

                        std::set<uint16_t> keysReleased;
                        std::ranges::set_difference(currentKeyboardState,
                            newKeyboardState,
                            std::inserter(keysReleased, keysReleased.begin()));

                        if (currentKeyboardState.empty() &&
                            !newKeyboardState.empty())
                        {
                            pressReleaseModifiers(true);
                        }

                        for (uint16_t k : keysPressed)
                            pressReleaseKey(k, true);
                        for (uint16_t k : keysReleased)
                            pressReleaseKey(k, false);

                        if (!currentKeyboardState.empty() &&
                            newKeyboardState.empty())
                        {
                            pressReleaseModifiers(false);
                        }

                        currentKeyboardState = newKeyboardState;
                        break;
                    }

                    default:
                        fmt::print("Unhandled packet: ");
                        for (uint8_t c : data)
                            fmt::print("{:02x} ", c);
                        fmt::print("\n");
                }
            }
            catch (const TimeoutException& e)
            {
                msTimeout = 0;
            }

            if (msTimeout <= 0)
            {
                msTimeout = TIMEOUT_MS;
                authenticate(device);
            }
        }
    }
    hid_exit();
    return 0;
}
