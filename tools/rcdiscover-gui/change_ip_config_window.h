/*
 * Roboception GmbH
 * Munich, Germany
 * www.roboception.com
 *
 * Copyright (c) 2026 Roboception GmbH
 * All rights reserved
 *
 * Author: Heiko Hirschmueller
 */

#ifndef RC_CHANGE_IP_CONFIG_WINDOW_H
#define RC_CHANGE_IP_CONFIG_WINDOW_H

#include "device_choice.h"
#include "input_mac.h"
#include "input_ip.h"
#include "button.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Check_Button.H>

#include <vector>
#include <utility>
#include <tuple>
#include <string>

class ChangeIPConfigWindow : public Fl_Double_Window
{
  public:

    static ChangeIPConfigWindow *showWindow();
    static void hideWindow();

    void updateDevices(const std::vector<std::tuple<std::string, std::string, std::string> > &list,
      const std::string &sel_mac);

    void update();

    void isChangingDevice();
    void isChangingCurrentIP();
    void doIP();
    void doSubnetMask();
    void isSet();
    void isClear();

  private:

    ChangeIPConfigWindow();

    void readCurrentConfigFromDevice();

    DeviceChoice *device;
    InputIP *current_ip;
    InputMAC *mac;
    Fl_Check_Button *persistent_ip_enabled;
    Fl_Check_Button *dhcp_enabled;
    Fl_Check_Button *lla_enabled;
    InputIP *ip;
    InputIP *subnet_mask;
    InputIP *default_gateway;
    Button *set_config;
    Button *clear_form;
    Button *help;
};

#endif
