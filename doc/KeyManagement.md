# Key Management

`ok` uses `mole` for UART encryption, which depends on private keys. The target VM supplies a public UUID, which `ok` uses to look up the 128-byte keyset for `mole`. A single developer would have one record in this database, the one for the associated dev unit that connects to the host PC via UART.

Immediately after a blank device MCU is programmed, it contains a default keyset. The device must be re-keyed to make it a production unit. Any unprovisioned devices should display "UNPROVISIONED" if possible, and should limit functionality. `ok` should be able to cut new random keys, re-key devices, and add or replace records within the database.

To allow the database to be stored as a file on the host PC, each record is encrypted using a random IV and encryption and hmac keys as constants within `ok`. Each record in the database consists of:

- 16-byte UUID or user name
- 32-byte IV preamble
- 64-byte keyset data, encrypted with ok's private keys. 
- 16-byte hmac

At a small scale, keys in a file will work. `ok` would look up the target's key in the file.
A generator utility can generate the file, which would be used to provision devices, update them, etc.

The file is just a highly controlled item. It would be password-encrypted.
The file should be locked away in a tower, er I mean server.
Access to the server would be via local intranet or in some cases port forwarding.

#PKE anyway

Prearranged keys solve the access problem. PKE can be spoofed with a MitM. 
However, shipping around private keys for pairing is a security problem.
A possible middle ground is to use PKE to pair at will and require an admin passcode to do anything intrusive.

The admin passcode and boot key would be baked into the firmware at manufacture.
The public/private key pair would be generated and saved if the connection is not paired.
`ok` would need to provide the admin password to do anything system-level.
