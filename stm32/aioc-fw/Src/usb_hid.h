#ifndef USB_HID_H_
#define USB_HID_H_

#include <stdint.h>
#include <stdbool.h>
#include "usb_descriptors.h"

/* Internal AIOC/CM108 button bits.
 *
 * Keep these values in the historical AIOC order used by io.c/cos.h and by
 * the legacy 4-byte AIOC control report:
 *   bit 0 = Volume Up
 *   bit 1 = Volume Down
 *   bit 2 = Playback Mute
 *   bit 3 = Record Mute
 *
 * Do not make these equal to the HID Consumer Control payload bits. The
 * Consumer report has a different bit layout and is translated in usb_hid.c
 * immediately before sending Report ID AIOC_HID_REPORT_ID_CM108.
 */
#define USB_HID_BUTTON_VOLUP    0x01
#define USB_HID_BUTTON_VOLDN    0x02
#define USB_HID_BUTTON_PLAYMUTE 0x04
#define USB_HID_BUTTON_RECMUTE  0x08

void USB_HIDInit(void);
void USB_HIDTask(void);
bool USB_HIDSendButtonState(uint8_t inputsMask);

#endif /* USB_HID_H_ */
