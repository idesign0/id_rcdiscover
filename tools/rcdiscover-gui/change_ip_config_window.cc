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

#include "change_ip_config_window.h"
#include "help_window.h"

#include "layout.h"
#include "label.h"

#include "rcdiscover/gvcp_ip_config.h"

#include <FL/fl_ask.H>

#include <stdexcept>

namespace
{

void changingDeviceCb(Fl_Widget *, void *user_data)
{
  ChangeIPConfigWindow *win=reinterpret_cast<ChangeIPConfigWindow *>(user_data);
  win->isChangingDevice();
}

void currentIPCb(Fl_Widget *, void *user_data)
{
  ChangeIPConfigWindow *win=reinterpret_cast<ChangeIPConfigWindow *>(user_data);
  win->isChangingCurrentIP();
}

void updateCb(Fl_Widget *, void *user_data)
{
  ChangeIPConfigWindow *win=reinterpret_cast<ChangeIPConfigWindow *>(user_data);
  win->update();
}

void ipCb(Fl_Widget *, void *user_data)
{
  ChangeIPConfigWindow *win=reinterpret_cast<ChangeIPConfigWindow *>(user_data);
  win->doIP();
}

void subnetMaskCb(Fl_Widget *, void *user_data)
{
  ChangeIPConfigWindow *win=reinterpret_cast<ChangeIPConfigWindow *>(user_data);
  win->doSubnetMask();
}

void setCb(Fl_Widget *, void *user_data)
{
  ChangeIPConfigWindow *win=reinterpret_cast<ChangeIPConfigWindow *>(user_data);
  win->isSet();
}

void clearCb(Fl_Widget *, void *user_data)
{
  ChangeIPConfigWindow *win=reinterpret_cast<ChangeIPConfigWindow *>(user_data);
  win->isClear();
}

void helpCb(Fl_Widget *, void *)
{
  HelpWindow::showWindow("changeipconfig");
}

}

namespace
{

static ChangeIPConfigWindow *win=0;

}

ChangeIPConfigWindow *ChangeIPConfigWindow::showWindow()
{
  if (!win)
  {
    win=new ChangeIPConfigWindow();
  }

  win->show();

  return win;
}

void ChangeIPConfigWindow::hideWindow()
{
  if (win)
  {
    win->hide();
  }
}

void ChangeIPConfigWindow::updateDevices(
  const std::vector<std::tuple<std::string, std::string, std::string> > &list,
  const std::string &sel_mac)
{
  device->update(list, sel_mac);
  isChangingDevice();
  update();
}

void ChangeIPConfigWindow::update()
{
  std::string v=device->getMAC();

  if (v.size() == 0)
  {
    mac->activate();
    current_ip->activate();
  }
  else
  {
    mac->deactivate();
    current_ip->deactivate();
  }

  if (persistent_ip_enabled->value())
  {
    ip->activate();
    subnet_mask->activate();
    default_gateway->activate();
  }
  else
  {
    ip->deactivate();
    subnet_mask->deactivate();
    default_gateway->deactivate();
  }

  bool persistent_ok=!persistent_ip_enabled->value() ||
    (ip->isValid() && subnet_mask->isValid() && default_gateway->isValid());

  if (current_ip->isValid() && persistent_ok)
  {
    set_config->activate();
  }
  else
  {
    set_config->deactivate();
  }
}

void ChangeIPConfigWindow::isChangingDevice()
{
  mac->value(device->getMAC().c_str());
  current_ip->value(device->getIP().c_str());

  readCurrentConfigFromDevice();
  update();
}

void ChangeIPConfigWindow::isChangingCurrentIP()
{
  readCurrentConfigFromDevice();
  update();
}

void ChangeIPConfigWindow::readCurrentConfigFromDevice()
{
  if (current_ip->isValid())
  {
    try
    {
      rcdiscover::GvcpIPConfig gvcp(current_ip->getIP());
      const rcdiscover::IPConfig config=gvcp.readConfig();

      persistent_ip_enabled->value(config.persistent_ip_enabled);
      dhcp_enabled->value(config.dhcp_enabled);

      ip->setIP(config.persistent_ip);
      subnet_mask->setIP(config.persistent_subnet);
      default_gateway->setIP(config.persistent_gateway);
    }
    catch (const std::runtime_error &)
    {
      // device is currently not reachable or does not respond; leave the
      // form as-is so the user can still enter a configuration manually
    }
  }
}

void ChangeIPConfigWindow::doIP()
{
  if (ip->isValid() && !subnet_mask->isValid())
  {
    uint32_t v=ip->getIP();

    if ((v>>24) == 10) // 10.0.0.0/8 addresses
    {
      subnet_mask->value("255.0.0.0");
    }

    if ((v>>24) == 172 && (static_cast<uint8_t>(v>>16)&16) != 0) // 172.16.0.0/12 addresses
    {
      subnet_mask->value("255.240.0.0");
    }

    if ((v>>24) == 192 && static_cast<uint8_t>(v>>16) == 168) // 192.168.0.0/16 addresses
    {
      subnet_mask->value("255.255.0.0");
    }

    if ((v>>24) == 169 && static_cast<uint8_t>(v>>16) == 254) // 169.254.0.0/16 addresses
    {
      subnet_mask->value("255.255.0.0");
    }
  }

  doSubnetMask();
}

void ChangeIPConfigWindow::doSubnetMask()
{
  if (ip->isValid() && subnet_mask->isValid())
  {
    default_gateway->setIP((ip->getIP() & subnet_mask->getIP()) | 0x1);
  }

  update();
}

void ChangeIPConfigWindow::isSet()
{
  bool persistent_ok=!persistent_ip_enabled->value() ||
    (ip->isValid() && subnet_mask->isValid() && default_gateway->isValid());

  if (current_ip->isValid() && persistent_ok)
  {
    try
    {
      if (persistent_ip_enabled->value() &&
        (ip->getIP() & subnet_mask->getIP()) != (default_gateway->getIP() & subnet_mask->getIP()))
      {
        if (fl_choice("IP address and gateway appear to be in different subnets. ",
          "Cancel", "Proceed", 0) != 1)
        {
          return;
        }
      }

      rcdiscover::GvcpIPConfig gvcp(current_ip->getIP());

      if (fl_choice("Are you sure to change the IP configuration of the device with current "
        "IP address %s?\nThe device must be reconnected or power cycled for the new "
        "configuration to take effect.", "No", "Yes", 0, current_ip->value()) == 1)
      {
        rcdiscover::IPConfig config;
        config.persistent_ip_enabled=persistent_ip_enabled->value() != 0;
        config.dhcp_enabled=dhcp_enabled->value() != 0;
        config.persistent_ip=ip->getIP();
        config.persistent_subnet=subnet_mask->getIP();
        config.persistent_gateway=default_gateway->getIP();

        gvcp.writeConfig(config);

        hide();
      }
    }
    catch (const rcdiscover::GvcpAccessDeniedException &)
    {
      fl_alert("The device is currently in use by another client and cannot be reconfigured.");
    }
    catch (const std::runtime_error &ex)
    {
      fl_alert("%s", ex.what());
    }
  }
}

void ChangeIPConfigWindow::isClear()
{
  ip->value("");
  subnet_mask->value("");
  default_gateway->value("");

  update();
}

ChangeIPConfigWindow::ChangeIPConfigWindow() : Fl_Double_Window(500, 314, "Change IP configuration")
{
  int width=500-2*GAP_SIZE;
  int row_height=28;
  int label_width=140;

  new Label(ADD_BELOW_XY, label_width, row_height, "Device");
  device=new DeviceChoice(ADD_RIGHT_XY, width-GAP_SIZE-label_width, row_height, 0);
  device->add("<Custom>");
  device->callback(changingDeviceCb, this);

  new Label(ADD_BELOW_XY, label_width, row_height, "Current IP address");
  current_ip=new InputIP(ADD_RIGHT_XY, width-GAP_SIZE-label_width, row_height, 0);
  current_ip->setChangeCallback(currentIPCb, this);

  new Label(ADD_BELOW_XY, label_width, row_height, "MAC address");
  mac=new InputMAC(ADD_RIGHT_XY, width-GAP_SIZE-label_width, row_height, 0);
  mac->setChangeCallback(updateCb, this);

  new Label(ADD_BELOW_XY, label_width, row_height, "IP configuration");
  persistent_ip_enabled=new Fl_Check_Button(ADD_RIGHT_XY, 110, row_height, "Persistent IP");
  persistent_ip_enabled->callback(updateCb, this);
  dhcp_enabled=new Fl_Check_Button(ADD_RIGHT_XY, 90, row_height, "DHCP");
  dhcp_enabled->callback(updateCb, this);
  lla_enabled=new Fl_Check_Button(ADD_RIGHT_XY, 110, row_height, "Link-local");
  lla_enabled->value(1);
  lla_enabled->deactivate();

  new Label(ADD_BELOW_XY, label_width, row_height, "New IP address");
  ip=new InputIP(ADD_RIGHT_XY, width-GAP_SIZE-label_width, row_height, 0);
  ip->setChangeCallback(ipCb, this);

  new Label(ADD_BELOW_XY, label_width, row_height, "Subnet mask");
  subnet_mask=new InputIP(ADD_RIGHT_XY, width-GAP_SIZE-label_width, row_height, 0);
  subnet_mask->setChangeCallback(subnetMaskCb, this);

  new Label(ADD_BELOW_XY, label_width, row_height, "Default gateway");
  default_gateway=new InputIP(ADD_RIGHT_XY, width-GAP_SIZE-label_width, row_height, 0);
  default_gateway->setChangeCallback(updateCb, this);

  set_config=new Button(ADD_BELOW_XY, 200, row_height, "Change IP configuration");
  set_config->callback(setCb, this);

  clear_form=new Button(ADD_RIGHT_XY, 100, row_height, "Clear form");
  clear_form->callback(clearCb, this);

  help=new Button(width+GAP_SIZE-row_height, addRightY(), row_height, row_height, "?");
  help->callback(helpCb, this);

  checkGroupSize(__func__);
  end();

  size_range(w(), h(), w(), h());

  update();
}
