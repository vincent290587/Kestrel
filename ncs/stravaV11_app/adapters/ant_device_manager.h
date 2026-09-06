/*
 * Placeholder for rf/ant_device_manager.h from stravaV10, not ported yet
 * (it pulls in the full ANT+ stack via "ant.h"). UserSettings only needs
 * the four default device-number constants; the real channel/pairing API
 * comes back once the ANT+ connectivity phase is ported.
 */

#ifndef ADAPTERS_ANT_DEVICE_MANAGER_H_
#define ADAPTERS_ANT_DEVICE_MANAGER_H_

#define HRM_DEVICE_NUMBER      17334U
#define BSC_DEVICE_NUMBER      15568U
#define GLASSES_DEVICE_NUMBER  0xFDDA
#define TACX_DEVICE_NUMBER     15568U

#endif /* ADAPTERS_ANT_DEVICE_MANAGER_H_ */
