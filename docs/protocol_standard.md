# Chat Protocol (ADT)

## Data Structure:
- proto-buffer (read the docs if u need them)

## Quick Overview:
- Kinda of the point for using proto-buffering is to have pretty simple data structures, that's why for each different 'method', we'll use a different 'protos' for each one of them.

- Will send the ips and the usernames, the servers will get in charge of managing them


## Unique types
  StatusEnum:
    Active = 0
    DoNotDisturb = 1
    Invisble = 2


## Simple desc


- Client sided (from client to server) [DONE]
  - Registration: [register.proto]
    - Username: string
  - Message general: [message_general.proto]
    - Message: string
    - Status: StatusEnum
    - Username-origin: string
  - Message dm: [message_dm.proto]
    - Message: string
    - Status: StatusEnum
    - Username-des: string
  - Change status: [change_status.proto]
    - Status: StatusEnum
    - Username: string
  - List users: [list_users.proto]
    - Username: string
  - Get user info: [get_user_info.proto]
    - Username-des: string
    - Username: string
  - Quit: [quit.proto]
    - Quit: 0-1

![NOTE]: Protos from the client side will always carry there IP in them, for having notion of which are they

- Server sided (from server to client)
  - All-Users: [all_users.proto]
    - Usernames: strings []
    - Status: StatusEnum []
  - For-dm: [for_dm.proto]
    - Username-des: string
    - Message: string
  - Broadcast delivery: [broadcast_messages.proto]
    - Message: string
    - Username-origin: string
  - Get user info response: [get_user_info_response.proto]
    - Ip-address: string
    - Username: string
    - Status: StatusEnum
  - Server response: [server_response.proto]
    - Status-code: int32
    - Message: string
    - Is-successful: bool
    
    > [NOTE]: Each implementation of this protocol can design their own codes to handle the status of the server response.


## TCP Framing

TCP is a byte-stream protocol — it does not preserve message boundaries. Every message sent over TCP **must** include a 5-byte header before the serialized protobuf payload so the receiver knows what to read and how to parse it.

### Header format

```
┌──────────────────┬───────────────────────────┬────────────────────────────┐
│  1 byte: type    │  4 bytes: payload length   │  N bytes: protobuf payload │
│  (uint8)         │  (uint32, big-endian)       │                            │
└──────────────────┴───────────────────────────┴────────────────────────────┘
```

- **type**: identifies which proto struct to use for deserialization.
- **length**: exact number of bytes of the serialized protobuf that follow.
- **payload**: the raw bytes from `SerializeToString()`.

### Message type table

| Type | Direction        | Proto                   |
|------|-----------------|-------------------------|
| 1    | client → server | register                |
| 2    | client → server | message_general         |
| 3    | client → server | message_dm              |
| 4    | client → server | change_status           |
| 5    | client → server | list_users              |
| 6    | client → server | get_user_info           |
| 7    | client → server | quit                    |
| 10   | server → client | server_response         |
| 11   | server → client | all_users               |
| 12   | server → client | for_dm                  |
| 13   | server → client | broadcast_messages      |
| 14   | server → client | get_user_info_response  |

### Send/receive contract

**Sending:**
1. Serialize the proto object to bytes.
2. Build a 5-byte header: `[type (1B)][length (4B big-endian)]`.
3. Send header + payload in one write.

**Receiving:**
1. Read exactly 5 bytes → parse type and length.
2. Read exactly `length` bytes → that is the protobuf payload.
3. Use `type` to select the correct proto class and call `ParseFromString()`.

> [NOTE]: The protos themselves are not modified. Framing is handled entirely in the send/receive layer of the application code.
