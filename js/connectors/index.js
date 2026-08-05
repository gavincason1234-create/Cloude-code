import { BleScanConnector, BleDeviceConnector } from './bluetooth.js';
import { NetworkLinkConnector, LocalInterfaceConnector } from './network.js';
import { UsbConnector, HidConnector, SerialConnector, MediaConnector } from './wired.js';
import {
  GeoConnector,
  BatteryConnector,
  MotionConnector,
  HostProfileConnector,
} from './environment.js';

/** Registry order drives the order of the connector panel. */
export const CONNECTORS = [
  BleScanConnector,
  BleDeviceConnector,
  NetworkLinkConnector,
  LocalInterfaceConnector,
  UsbConnector,
  HidConnector,
  SerialConnector,
  MediaConnector,
  GeoConnector,
  BatteryConnector,
  MotionConnector,
  HostProfileConnector,
];
