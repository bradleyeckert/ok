#include <stdlib.h>
#include <stdint.h>
#include "../mole/moleconfig.h"

static uint8_t KMSreceivedKey[] = TESTPASS_1;

int KMSlookup(const uint8_t *DeviceName, uint8_t **key) {
    *key = KMSreceivedKey;
    return 0;
}
