# Privacy policy

`chats-gamebased` is a direct peer-to-peer Windows chat application.

## Data sent over the network

The application does not collect or upload telemetry, analytics, advertising identifiers, crash reports or account information.

Network traffic is limited to functionality requested by the user or required to establish the peer-to-peer connection:

- Chat messages and user-selected attachments are sent directly to the peer endpoints or group members selected by the user.
- LAN discovery uses local UDP broadcast on the application's chat port.
- The application contacts `stun.cloudflare.com:3478` using STUN to discover the public UDP endpoint needed for direct Internet peer-to-peer connections. The STUN service receives the source IP address and UDP port as an inherent part of the protocol; chat messages and attachments are not sent to the STUN service.

## Local data

Received attachments and imported sticker files may be stored locally by the application. They are not uploaded to a central service.

## Third-party services

The current build uses Cloudflare's public STUN endpoint solely for NAT/public-endpoint discovery. Cloudflare's own privacy terms apply to traffic received by that service.

## Control

Users choose which remote peer addresses they add and which peers or groups receive messages and attachments. Closing the application stops its peer-to-peer and STUN network activity.
