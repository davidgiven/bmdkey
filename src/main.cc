#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <exception>
#include <vector>
#include <hidapi.h>
#include <fmt/format.h>

#define MAX_STR 255

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

/* Authentication code borrowed from https://github.com/smunaut/blackmagic-misc. */
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

static uint64_t getInt64(const uint8_t* p)
{
    return ((uint64_t)p[0] << 0) | ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
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

    std::vector<uint8_t> recv()
    {
        std::vector<uint8_t> data(64);

        int res = hid_read(_handle, &data[0], sizeof(data));
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
}

int main()
{
    hid_init();
    {
        HidDevice device(0x1edb, 0xda0e);
        authenticate(device);

        device.send({3, 0,0,0,0,0,0});
        for (;;)
        {
            auto data = device.recv();

            for (uint8_t c : data)
                fmt::print("{:02x} ", c);
            fmt::print("\n");
        }
    }
    hid_exit();
    return 0;
}
