// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/events/test/device_data_manager_test_api.h"

#include "ui/events/devices/device_data_manager.h"

namespace ui {
namespace test {

DeviceDataManagerTestAPI::DeviceDataManagerTestAPI()
    : should_delete_instance_(false) {
}

DeviceDataManagerTestAPI::~DeviceDataManagerTestAPI() {
  if (should_delete_instance_)
    DeviceDataManager::DeleteInstance();
}

bool DeviceDataManagerTestAPI::CreateDeviceDataManagerInstance() {
  should_delete_instance_ = true;
  DeviceDataManager::DeleteInstance();
  DeviceDataManager::set_instance(new DeviceDataManager());
  return true;
}

void DeviceDataManagerTestAPI::
    NotifyObserversTouchscreenDeviceConfigurationChanged() {
  DeviceDataManager::GetInstance()
      ->NotifyObserversTouchscreenDeviceConfigurationChanged();
}

void DeviceDataManagerTestAPI::
    NotifyObserversKeyboardDeviceConfigurationChanged() {
  DeviceDataManager::GetInstance()
      ->NotifyObserversKeyboardDeviceConfigurationChanged();
}

void DeviceDataManagerTestAPI::
    NotifyObserversMouseDeviceConfigurationChanged() {
  DeviceDataManager::GetInstance()
      ->NotifyObserversMouseDeviceConfigurationChanged();
}

void DeviceDataManagerTestAPI::
    NotifyObserversTouchpadDeviceConfigurationChanged() {
  DeviceDataManager::GetInstance()
      ->NotifyObserversTouchpadDeviceConfigurationChanged();
}

void DeviceDataManagerTestAPI::NotifyObserversDeviceListsComplete() {
  DeviceDataManager::GetInstance()->NotifyObserversDeviceListsComplete();
}

void DeviceDataManagerTestAPI::OnDeviceListsComplete() {
  DeviceDataManager::GetInstance()->OnDeviceListsComplete();
}

}  // namespace test
}  // namespace ui
