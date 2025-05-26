//===============================================================================
// kms.h
//===============================================================================

#ifndef __KMS_H__
#define __KMS_H__
#include <stdint.h>

/** Thin-client communication interface
 * @param DeviceName Counted zstring, first byte is the length of the string
 * @param key pointer to a pointer to the key
 * @return 0 if success, else failure code
 */
int KMSlookup(const uint8_t *DeviceName, uint8_t **key);

#endif // __KMS_H__
