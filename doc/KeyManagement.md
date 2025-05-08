# Key Management

`ok` uses `mole` for UART encryption, which depends on private keys. The target VM supplies a public UUID, which `ok` uses to look up the 128-byte keyset for `mole`. A single developer would have one record in this database, the one for the associated dev unit that connects to the host PC via UART.

Immediately after a blank device MCU is programmed, it contains a default keyset. The device must be re-keyed to make it a production unit. Any unprovisioned devices should display "UNPROVISIONED" if possible, and should limit functionality. `ok` should be able to cut new random keys, re-key devices, and add or replace records within the database.

To allow the database to be stored as a file on the host PC, each record is encrypted using a random IV and encryption and hmac keys as constants within `ok`. Each record in the database consists of:

- 16-byte UUID or user name
- 32-byte IV preamble
- 128-byte keyset data, encrypted with ok's private keys. 
- 16-byte hmac

Total: 192 bytes, of which 176 are AEAD data.
