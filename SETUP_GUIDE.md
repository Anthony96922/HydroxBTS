# Working OpenBTS-UMTS + OsmoHLR Lab Setup

## 1. What this backup contains

This folder is a reproducible snapshot of a working OpenBTS-UMTS lab setup with:

- PentHertz/OpenBTS-UMTS patched source
- Ettus/USRP B210-family radio via UHD
- UMTS AKA using OsmoHLR / Milenage vectors
- GSUP helper bridge from OpenBTS-UMTS to OsmoHLR
- native 128-bit UMTS integrity key handling
- successful Security Mode
- successful Attach
- PDP Context activation
- DCH packet bearer on RB5
- mini-GGSN / `sgsntun`
- working IPv4 packet data to the Internet

The exact live OpenBTS settings captured from the machine are in:

    docs/CURRENT_OPENBTS_CONFIG.txt

The exact sensitive runtime databases are under:

    PRIVATE_DO_NOT_UPLOAD/

That private directory is deliberately ignored by Git.

---

## 2. Architecture

    UE / SIM
       |
       | WCDMA / UMTS
       v
    USRP B210
       |
       | UHD
       v
    OpenBTS-UMTS
       |
       +---- RRC / RLC / SGSN / PDP
       |
       +---- /usr/local/bin/openbts-gsup-auth
       |           |
       |           | GSUP / IPA / TCP
       |           v
       |       OsmoHLR
       |       127.0.0.1:4222
       |
       +---- mini-GGSN
                    |
                    v
                 sgsntun
                    |
                    v
               Linux routing/NAT
                    |
                    v
                 Internet

OsmoHLR VTY is normally on localhost TCP 4258.

---

## 3. Important paths

Working source tree on the original machine:

    /media/yat/d9d3b612-b8b3-45e3-969d-d5d5260bbbe7/OpenBTS-UMTS

OpenBTS runtime database:

    /etc/OpenBTS/OpenBTS-UMTS.db

OsmoHLR configuration:

    /etc/osmocom/osmo-hlr.cfg

OsmoHLR subscriber database:

    /var/lib/osmocom/hlr.db

GSUP helper installed executable:

    /usr/local/bin/openbts-gsup-auth

This backup stores the patched source under:

    source/

The helper source is copied to:

    tools/openbts-gsup-auth.c

---

## 4. Git provenance

Before changing anything, inspect:

    docs/BASE_COMMIT.txt
    docs/GIT_REMOTES.txt
    docs/GIT_STATUS.txt
    docs/WORKING_TREE.patch
    docs/STAGED.patch

`WORKING_TREE.patch` is especially valuable. It is a plain diff of the
working modifications against the original checkout.

---

## 5. Build requirements

The original machine's exact relevant package/version snapshot is in:

    docs/system-snapshot/versions.txt
    docs/system-snapshot/relevant-packages.txt

The setup uses UHD and Osmocom libraries. The GSUP helper requires at least:

- libosmo-gsup-client
- libosmocore
- libosmogsm
- libtalloc
- pkg-config
- GCC

Build the helper with:

    gcc -O2 -Wall -Wextra \
      tools/openbts-gsup-auth.c \
      -o openbts-gsup-auth \
      $(pkg-config --cflags --libs \
          libosmo-gsup-client libosmocore libosmogsm) \
      -ltalloc

Install it:

    sudo install -m 0755 ./openbts-gsup-auth \
      /usr/local/bin/openbts-gsup-auth

---

## 6. OsmoHLR setup

Install and enable OsmoHLR using the packages appropriate for the distro.

The working lab expects GSUP on:

    127.0.0.1:4222

and the VTY on:

    127.0.0.1:4258

The exact original configuration is backed up privately as:

    PRIVATE_DO_NOT_UPLOAD/osmo-hlr.cfg

A public/redacted copy is:

    docs/osmo-hlr.PUBLIC.cfg

Start OsmoHLR:

    sudo systemctl enable --now osmo-hlr

Check it:

    systemctl status osmo-hlr --no-pager
    ss -ltnp | grep -E ':(4222|4258)'

The original service/unit snapshot is in:

    docs/system-snapshot/osmo-hlr-systemd.txt

### Subscriber provisioning

Never publish real K, Ki, OP, OPc or other subscriber authentication
material.

For a lab SIM, connect to the local OsmoHLR VTY:

    telnet 127.0.0.1 4258

Then, using placeholders rather than real secrets:

    enable
    subscriber imsi <IMSI> create
    subscriber imsi <IMSI> update msisdn <MSISDN>
    subscriber imsi <IMSI> update aud3g milenage k <K_HEX> opc <OPC_HEX>

The exact private HLR database can be restored from:

    PRIVATE_DO_NOT_UPLOAD/hlr.db

Do not commit that database to GitHub.

---

## 7. GSUP authentication bridge

The patched OpenBTS-UMTS SGSN asks the external helper for a UMTS AKA vector.

The helper talks GSUP to OsmoHLR at localhost port 4222.

Normal request:

    openbts-gsup-auth <IMSI>

Expected style of output:

    AKA RAND=... AUTN=... XRES=... CK=... IK=...

The helper also supports an AUTS/resynchronization form:

    openbts-gsup-auth <IMSI> <RAND> <AUTS>

The OpenBTS patch parses the helper output and uses:

- RAND
- AUTN
- full XRES
- CK
- IK

Important: GSUP itself is not an encrypted transport. Keep it on localhost,
an isolated trusted network, or a protected tunnel.

---

## 8. Major OpenBTS-UMTS patches in this working tree

Do not re-apply these blindly if using `source/`, because the source in this
backup is already patched.

The important modified areas include:

    SGSNGGSN/Sgsn.cpp
    SGSNGGSN/Sgsn.h
    SGSNGGSN/GPRSL3Messages.h
    SGSNGGSN/GPRSL3Messages.cpp
    SGSNGGSN/SgsnExport.h
    UMTS/IntegrityProtect.h
    UMTS/IntegrityProtect.cpp
    UMTS/URRC.cpp
    UMTS/URRCTrCh.cpp
    UMTS/UMTSPhCh.h
    apps/OpenBTS-UMTS.cpp

### AKA / GSUP

The SGSN obtains Milenage authentication vectors from OsmoHLR via the helper.

The Authentication Response handling was changed to accept the full UMTS RES,
not only the old GSM-style 4-byte SRES behavior.

### Native IK

The integrity layer was extended to load the full native 128-bit IK and use it
for UMTS Security Mode.

### Security state

After SecurityModeComplete, the GMM security state must be moved to the
started/secured state before continuing Attach. Without this, authentication
can restart unnecessarily during Attach.

### PTMSI allocation

The PTMSI allocator was corrected so allocated values remain in the expected
range/form rather than producing invalid identities.

### C-string lifetime fixes

Startup/config parsing code had temporary `std::string().c_str()` lifetime
problems. Those were fixed by keeping the backing `std::string` alive while
the C string is used.

This affected startup CLI/socket handling and RAI/MCC/MNC loading.

### DCH transport-format fix

`TrChConfig::configDchPS()` had a path where UL `ulMaxTBs` could remain zero
after quantizing a too-large transport block.

The fix sets a real transport-block count after falling back to the quantized
UL TB size.

### Channel-tree initialization fix

`ChannelTreeElt::mAlsoReserved` was not initialized.

The constructor must initialize all reservation state, including:

    mAlsoReserved(0)

Leaving this uninitialized can make perfectly usable DCH channels appear busy
or reserved.

### Packet data path verified

The working path was traced end-to-end:

    RB5 RLC
      -> gSgsnUplinkQueue
      -> UEInfo::ueRecvData()
      -> MSUEAdapter::sgsnWriteLowSide()
      -> SgsnInfo::sgsnSend2PdpLowSide()
      -> PdpContext::pdpWriteLowSide()
      -> mini-GGSN
      -> sgsntun

During debugging, valid raw IPv4 and IPv6 packets were observed immediately
before `PdpContext::pdpWriteLowSide()`.

---

## 9. OpenBTS runtime configuration

Do not guess values when restoring. Use the captured live values in:

    docs/CURRENT_OPENBTS_CONFIG.txt

Important groups include:

- `UMTS.Identity.*`
- `GPRS.RAC`
- `GPRS.NMO`
- `UMTS.Best.Effort.BytesPerSec`
- `UMTS.UseTurboCodes`
- `UMTS.Radio.*`
- `GGSN.*`

The exact original database is:

    PRIVATE_DO_NOT_UPLOAD/OpenBTS-UMTS.db

Restore it only on a private/trusted machine:

    sudo mkdir -p /etc/OpenBTS
    sudo cp PRIVATE_DO_NOT_UPLOAD/OpenBTS-UMTS.db \
      /etc/OpenBTS/OpenBTS-UMTS.db

The SQL dump is also stored privately:

    PRIVATE_DO_NOT_UPLOAD/OpenBTS-UMTS.sql

---

## 10. Turbo-code warning

Do not casually enable `UMTS.UseTurboCodes`.

This working tree previously hit a Turbo interleaver assertion with an invalid
or zero-sized transport configuration.

Keep the exact working value from `docs/CURRENT_OPENBTS_CONFIG.txt` unless you
are deliberately debugging the transport configuration.

---

## 11. Build OpenBTS-UMTS

Use the patched source under:

    source/

If the checkout already contains a working configured build system, rebuild
with:

    make -j"$(nproc)"

If starting from a clean machine, use the normal build/bootstrap procedure
for the PentHertz/OpenBTS-UMTS base revision recorded in:

    docs/BASE_COMMIT.txt

Then verify that the GSUP helper is installed before starting OpenBTS-UMTS.

---

## 12. Recommended service start order

1. Start OsmoHLR.
2. Verify TCP 4222 is listening.
3. Verify `openbts-gsup-auth <test IMSI>` can obtain a vector.
4. Start OpenBTS-UMTS.
5. Wait for the radio/transceiver to become ready.
6. Attach the lab UE.
7. Confirm Security Mode completes.
8. Confirm Attach Complete.
9. Confirm Activate PDP Context.
10. Confirm Radio Bearer Setup Complete.
11. Confirm `sgsntun` exists.
12. Confirm Linux forwarding/NAT.
13. Test data.

Typical checks:

    systemctl status osmo-hlr --no-pager
    ss -ltnp | grep 4222
    ip link show sgsntun
    sudo tcpdump -ni sgsntun

---

## 13. Linux forwarding and NAT

The exact original host rules are backed up privately:

    PRIVATE_DO_NOT_UPLOAD/iptables-save.txt
    PRIVATE_DO_NOT_UPLOAD/network-state.txt

A generic restore/test setup is:

    sudo sysctl -w net.ipv4.ip_forward=1

    WAN="$(ip route show default | awk '/default/ {print $5; exit}')"

    sudo iptables -t nat -A POSTROUTING \
      -s <UE_SUBNET> -o "$WAN" -j MASQUERADE

    sudo iptables -A FORWARD \
      -i sgsntun -o "$WAN" -j ACCEPT

    sudo iptables -A FORWARD \
      -i "$WAN" -o sgsntun \
      -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT

Replace `<UE_SUBNET>` with the subnet from the live GGSN/OpenBTS
configuration. Do not blindly assume a subnet on another machine.

Before adding duplicate rules, inspect:

    sudo iptables -t nat -S POSTROUTING
    sudo iptables -S FORWARD

---

## 14. GGSN / DNS notes

The working OpenBTS database should be treated as canonical.

During debugging, packet data was blocked before the TUN path even though
valid IPv4/IPv6 packets were reaching the PDP context. GGSN firewall/DNS
settings were therefore checked and corrected.

Preserve the exact working values from:

    docs/CURRENT_OPENBTS_CONFIG.txt

For a new installation, make sure:

- the GGSN TUN interface is created
- the UE receives a valid PDP address
- DNS given to the UE is reachable from the host
- the GGSN firewall is not unexpectedly dropping lab traffic
- IPv4 forwarding is enabled
- NAT/forwarding rules exist

---

## 15. What healthy logs look like

A good attach/data session should progress roughly as:

    RRC Connection Request
    RRC Connection Setup Complete
    Attach Request
    Identity Response
    AKA challenge
    Authentication Response
    Security Mode Complete
    Attach Accept
    Attach Complete
    Activate PDP Context Request
    Radio Bearer Setup
    Radio Bearer Setup Complete
    Activate PDP Context Accept
    RB5 user data

A real IPv4 packet begins with a high nibble of 4, commonly:

    45 ...

IPv6 begins with:

    60 ...

If RB5 packets are valid but `sgsntun` is silent, inspect the
PDP/mini-GGSN path rather than changing the radio.

---

## 16. Debug probes

The source tree may still contain temporary debug probes added while bringing
packet data up, including strings similar to:

    ### UL USERDATA LEFT RLC
    ### SGSN QUEUE RX
    ### SGSN LOWSIDE
    ### PDP LOOKUP
    ### PDP PACKET HEX

They are useful for debugging, but the per-packet output can be very noisy.

Before publishing a polished release, either remove them or put them behind a
debug flag.

Search for them with:

    grep -Rni '### .*' source/UMTS source/SGSNGGSN

---

## 17. Protect private SIM material

Never publish:

- K / Ki
- OP
- OPc
- SQN state
- AUTS from a real/private subscriber
- raw subscriber databases
- secret tokens/passwords

The backup intentionally places the exact HLR and OpenBTS databases in:

    PRIVATE_DO_NOT_UPLOAD/

and the top-level `.gitignore` excludes that directory.

If the GitHub repository is public, verify before the first push:

    git status
    git ls-files | grep -Ei \
      '(PRIVATE_DO_NOT_UPLOAD|hlr\.db|OpenBTS-UMTS\.db|\.sqlite)'

That command should print nothing sensitive.

---

## 18. Restoring the private lab state

On your own trusted machine only:

1. Install required packages.
2. Copy `source/` to the desired source location.
3. Build/install the GSUP helper.
4. Install OsmoHLR.
5. Restore `PRIVATE_DO_NOT_UPLOAD/osmo-hlr.cfg`.
6. Restore `PRIVATE_DO_NOT_UPLOAD/hlr.db`.
7. Restore `PRIVATE_DO_NOT_UPLOAD/OpenBTS-UMTS.db`.
8. Start OsmoHLR.
9. Verify GSUP port 4222.
10. Build/start OpenBTS-UMTS.
11. Restore host routing/NAT as appropriate.
12. Attach the lab UE and verify packet data.

The private database copies are the easiest way to reproduce the exact
working state without re-entering subscriber authentication values.

---

## 19. Preparing the public GitHub repository

From the backup folder:

    cd ~/OpenBTS-UMTS-WORKING-BACKUP
    git init
    git add .gitignore README.md SETUP_GUIDE.md source tools docs
    git status

Inspect the staged files carefully.

Then:

    git commit -m "Working OpenBTS-UMTS OsmoHLR AKA and packet data setup"

Add your own GitHub remote and push when ready.

Do NOT use `git add -f PRIVATE_DO_NOT_UPLOAD`.

---

## 20. Files worth preserving forever

The most valuable recovery files are:

    source/
    tools/openbts-gsup-auth.c
    docs/WORKING_TREE.patch
    docs/BASE_COMMIT.txt
    docs/CURRENT_OPENBTS_CONFIG.txt
    PRIVATE_DO_NOT_UPLOAD/OpenBTS-UMTS.db
    PRIVATE_DO_NOT_UPLOAD/hlr.db
    PRIVATE_DO_NOT_UPLOAD/osmo-hlr.cfg
    PRIVATE_DO_NOT_UPLOAD/iptables-save.txt

The first five are appropriate for a public source repository after review.
The private directory is a local disaster-recovery copy only.
