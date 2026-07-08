/*
 * Roboception GmbH
 * Munich, Germany
 * www.roboception.com
 *
 * Copyright (c) 2024 Roboception GmbH
 * All rights reserved
 *
 * Author: Heiko Hirschmueller
 */

#ifndef RC_DEVICE_CHOICE_H
#define RC_DEVICE_CHOICE_H

#include <FL/Fl.H>
#include <FL/Fl_Choice.H>

#include <vector>
#include <utility>
#include <string>
#include <tuple>

class DeviceChoice : public Fl_Choice
{
  public:

    DeviceChoice(int x, int y, int w, int h, const char *label);

    std::string getMAC();
    std::string getIP();

    void update(const std::vector<std::pair<std::string, std::string> > &list,
      const std::string &sel_mac);

    void update(const std::vector<std::tuple<std::string, std::string, std::string> > &list,
      const std::string &sel_mac);

  private:

    std::vector<std::string> ip_list;
};

#endif
