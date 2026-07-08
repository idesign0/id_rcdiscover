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

#include "device_choice.h"

DeviceChoice::DeviceChoice(int x, int y, int w, int h, const char *label) :
  Fl_Choice(x, y, w, h, label)
{
  down_box(FL_GTK_DOWN_BOX);
}

std::string DeviceChoice::getMAC()
{
  std::string ret;

  int i=Fl_Choice::value();

  if (i >= 0 && i < size())
  {
    std::string label=menu()[i].label();

    size_t i=label.find(" - ");

    if (i != std::string::npos)
    {
      ret=label.substr(i+3);
    }
  }

  return ret;
}

std::string DeviceChoice::getIP()
{
  std::string ret;

  int i=Fl_Choice::value();

  if (i >= 0 && static_cast<size_t>(i) < ip_list.size())
  {
    ret=ip_list[static_cast<size_t>(i)];
  }

  return ret;
}

void DeviceChoice::update(const std::vector<std::pair<std::string, std::string> > &list,
  const std::string &sel_mac)
{
  clear();
  add("<CUSTOM>", 0, 0, 0, 0);

  ip_list.clear();
  ip_list.push_back(std::string());

  size_t sel=0;
  for (size_t i=0; i<list.size(); i++)
  {
    add((list[i].first+" - "+list[i].second).c_str(), 0, 0, 0, 0);
    ip_list.push_back(std::string());

    if (list[i].second == sel_mac)
    {
      sel=i+1;
    }
  }

  value(static_cast<int>(sel));
}

void DeviceChoice::update(const std::vector<std::tuple<std::string, std::string, std::string> > &list,
  const std::string &sel_mac)
{
  clear();
  add("<CUSTOM>", 0, 0, 0, 0);

  ip_list.clear();
  ip_list.push_back(std::string());

  size_t sel=0;
  for (size_t i=0; i<list.size(); i++)
  {
    add((std::get<0>(list[i])+" - "+std::get<1>(list[i])).c_str(), 0, 0, 0, 0);
    ip_list.push_back(std::get<2>(list[i]));

    if (std::get<1>(list[i]) == sel_mac)
    {
      sel=i+1;
    }
  }

  value(static_cast<int>(sel));
}
