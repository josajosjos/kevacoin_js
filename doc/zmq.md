# Kevacoin Event Broadcasting with ZeroMQ

[ZeroMQ](http://zeromq.org/) is a lightweight wrapper around TCP
connections, inter-process communication, and shared-memory,
providing various message-oriented semantics such as publish/subscribe,
request/reply, and push/pull.

The Kevacoin Core daemon can be configured to act as a trusted "border
router", implementing the kevacoin wire protocol and relay, making
consensus decisions, maintaining the local blockchain database,
broadcasting locally generated transactions into the network, and
providing a queryable RPC interface to interact on a polled basis for
requesting blockchain related data. However, there exists only a
limited service to notify external software of events like the arrival
of new blocks or transactions.

The ZeroMQ facility implements a notification interface through a set
of specific notifiers. Currently there are notifiers that publish
blocks and transactions. This read-only facility requires only the
connection of a corresponding ZeroMQ subscriber port in receiving
software; it is not authenticated nor is there any two-way protocol
involvement. Therefore, subscribers should validate the received data
since it may be out of date, incomplete or even invalid.

ZeroMQ sockets are self-connecting and self-healing; that is,
connections made between two endpoints will be automatically restored
after an outage, and either end may be freely started or stopped in
any order.

Because ZeroMQ is message oriented, subscribers receive transactions
and blocks all-at-once and do not need to implement any sort of
buffering or reassembly.

## Prerequisites

The ZeroMQ feature in Kevacoin Core requires ZeroMQ API version 4.x or
newer. Typically, it is packaged by distributions as something like
*libzmq3-dev*. The C++ wrapper for ZeroMQ is *not* needed.

In order to run the example Python client scripts in contrib/ one must
also install *python3-zmq*, though this is not necessary for daemon
operation.

## Enabling

By default, the ZeroMQ feature is automatically compiled in if the
necessary prerequisites are found.  To disable, use --disable-zmq
during the *configure* step of building kevacoind:

    $ ./configure --disable-zmq (other options)

To actually enable operation, one must set the appropriate options on
the command line or in the configuration file.

## Usage

Currently, the following notifications are supported:

    -zmqpubhashtx=address
    -zmqpubhashblock=address
    -zmqpubrawblock=address
    -zmqpubrawtx=address
    -zmqpubkeva=address

The socket type is PUB and the address must be a valid ZeroMQ socket
address. The same address can be used in more than one notification.

For instance:

    $ kevacoind -zmqpubhashtx=tcp://127.0.0.1:28332 \
               -zmqpubrawtx=ipc:///tmp/kevacoind.tx.raw

Each PUB notification has a topic and body, where the header
corresponds to the notification type. For instance, for the
notification `-zmqpubhashtx` the topic is `hashtx` (no null
terminator) and the body is the transaction hash (32
bytes).

The command `getzmqnotifications` can be used to check what ZeroMQ notifications are enabled:

```
kevacoin-cli getzmqnotifications

[
  {
    "type": "pubkeva",
    "address": "tcp://127.0.0.1:29000"
  }
]
```

These options can also be provided in kevacoin.conf. The following is an sample
kevacoin.conf file that supports ZeroMQ notification of Keva events:

```
rpcport=9332
rpcuser=user
rpcpassword=userpassword
zmqpubkeva=tcp://127.0.0.1:29000
```

ZeroMQ endpoint specifiers for TCP (and others) are documented in the
[ZeroMQ API](http://api.zeromq.org/4-0:_start).

Client side, then, the ZeroMQ subscriber socket must have the
ZMQ_SUBSCRIBE option set to one or either of these prefixes (for
instance, just `hash`); without doing so will result in no messages
arriving. Please see `contrib/zmq/zmq_sub.py` for a working example.

## Kevacoin Specific Events

Once ZMQ notification is enabled for Keva events, it is easy to subscribe
to the events. The following is an Node.js example:

```javascript
var zmq = require('zeromq');

async function run() {
    // Create a subscriber socket.
    var sock = new zmq.Subscriber;
    var addr = 'tcp://127.0.0.1:29000';

    // Initiate connection to TCP socket.
    sock.connect(addr);

    // Subscribe to receive messages for a specific topic.
    // This can be "rawblock", "hashblock", "rawtx", or "hashtx".
    sock.subscribe('keva');

    for await (const [topic, message] of sock) {
        if (topic.toString() === 'keva') {
            let json = JSON.parse(message);
            console.log('received keva:');
            console.log(json);
        }
    }
}

run();
```

Keva messages are in JSON format. This is an example of the `keva_update` messsage:

```javascript
{
  tx: '690652bbee2bce22fdc9c5619ac77f3b7645423e2790860afa0fa2d14ff0c1be',
  height: 10673,
  timestamp: 1580520584,
  type: 'keva_update',
  namespace: 'Nd25va1gcEFjWgJtzU7Vuu3dG7gWE7G77y',
  key: 'This is key',
  value: 'This is value'
}
```

This is an example of the `keva_namespace` (creation of namespace) messsage:

```javascript
{
  tx: 'a6b4792a2150e1f15a45ff658dbfc64f34a0b0b27270321f557acfa0f70027d6',
  height: 10677,
  timestamp: 1580520947,
  type: 'keva_namespace',
  namespace: 'NRT9nLFy433BWeBakmyV1TFugai6dZt7BH'
}
```

When developing applications on Kevacoin, it is convenient to be able to subscribe to certain Keva events. For example, a Twitter-like application on Keva blockchain can listen to the event that a user follows the other user. Assume that the follower adds a key to her own namespace (`N_follower`) to indicate that she is following the other user whose namespace is `N_celeb`:

```
keva_put  <N_follower>  <N_celeb>  true
```

The application listening to this kind of event will be notified, and then when `N_celeb` publishes a new update, the application will notify the follower.

Similarly, if the follower stops following, she can set the value to `false`:
```
keva_put  <N_follower>  <N_celeb>  true
```
The application will also receive this event and stop send her the updates.

Note that the data is completely on the blockchain and the application can be written by anyone. The developer of the application has no monopoly on the data. The pub/sub APIs make it easier to develop applications, though technically the data on the blockchain provides sufficient information and is the single source of truth.

## Remarks

From the perspective of kevacoind, the ZeroMQ socket is write-only; PUB
sockets don't even have a read function. Thus, there is no state
introduced into kevacoind directly. Furthermore, no information is
broadcast that wasn't already received from the public P2P network.

No authentication or authorization is done on connecting clients; it
is assumed that the ZeroMQ port is exposed only to trusted entities,
using other means such as firewalling.

Note that when the block chain tip changes, a reorganisation may occur
and just the tip will be notified. It is up to the subscriber to
retrieve the chain from the last known block to the new tip.

There are several possibilities that ZMQ notification can get lost
during transmission depending on the communication type your are
using. kevacoind appends an up-counting sequence number to each
notification which allows listeners to detect lost notifications.
