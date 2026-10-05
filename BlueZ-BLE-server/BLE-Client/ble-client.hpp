#pragma once

#include <string>
#include <vector>
#include <dbus/dbus.h>

class BleClient
{
public:
    BleClient();
    ~BleClient();

    bool initialize();

    bool connectDevice(
        const std::string& devicePath);

    bool startNotify(
        const std::string& characteristicPath);

    void processEvents();

private:
    DBusConnection* m_conn;

    bool callMethod(
        const std::string& service,
        const std::string& path,
        const std::string& interface,
        const std::string& method);
        
    void parseTemperatureNotification(DBusMessage *msg);
};