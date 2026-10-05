#include "ble-client.hpp"
#include <iostream>

int main()
{
    BleClient client;

    if (!client.initialize())
        return -1;

    /*
     * Replace with actual path.
     *
     * Example:
     * /org/bluez/hci0/dev_DA_12_34_56_78_90
     */
    std::string devicePath =
        "/org/bluez/hci0/dev_DA_12_34_56_78_90";

    /*
     * Replace with discovered characteristic path.
     */
    std::string charPath =
        "/org/bluez/hci0/dev_DA_12_34_56_78_90/service0012/char0015";

    if (!client.connectDevice(devicePath))
    {
        std::cerr
            << "Connect failed\n";
        return -1;
    }

    if (!client.startNotify(charPath))
    {
        std::cerr
            << "StartNotify failed\n";
        return -1;
    }

    std::cout
        << "Subscribed for temperature updates\n";

    client.processEvents();

    return 0;
}