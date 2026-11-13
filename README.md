# ESPTelex — ESP32 TW39 i-Telex & Centralex Interface

**ESPTelex** is an ESP32-based hardware and firmware interface that connects classic electromechanical teleprinters (Fernschreiber) with a **TW39** current-loop interface and rotary dial control unit (**FSG** — *Fernschaltgerät*) to the global **i-Telex** network over Wi-Fi.

It supports both **Direct-IP** operation (via port forwarding, default TCP port `134`) and **Centralex** (NAT/CGNAT-friendly remote server standby without any router port forwarding), along with an interactive **Telnet & USB Serial CLI**, **Virtual Terminal mode**, **local offline mode**, **OTA firmware updates**, and a full **front-panel LED & button interface**.

---

## Table of Contents

1. [Key Features](#key-features)
2. [Hardware Architecture & Pinout](#hardware-architecture--pinout)
3. [Front Panel User Interface (LEDs & Buttons)](#front-panel-user-interface-leds--buttons)
   - [Single-Color State LEDs](#single-color-state-leds)
   - [RGB Status LEDs (`SDCARD`, `NETWORK`, `ITELEX`)](#rgb-status-leds-sdcard-network-itelex)
   - [Front-Panel Buttons (`TEST` & `DISCONNECT`)](#front-panel-buttons-test--disconnect)
4. [SD Card Configuration & Directory Structure](#sd-card-configuration--directory-structure)
   - [Wi-Fi Configuration (`/configs/wifi.txt`)](#wi-fi-configuration-configswifitxt)
   - [Centralex & Polarity Configuration (`/configs/centralex.txt`)](#centralex--polarity-configuration-configscentralextxt)
   - [Local Speed-Dial Directory (`/telexdirectory/<num>.txt`)](#local-speed-dial-directory-telexdirectorynumtxt)
5. [Operating Modes & Calling Workflows](#operating-modes--calling-workflows)
   - [Outgoing Calls via Rotary Dial (TW39)](#outgoing-calls-via-rotary-dial-tw39)
   - [Local Mode (Dialing `0`)](#local-mode-dialing-0)
   - [Incoming Calls (Direct-IP & Centralex)](#incoming-calls-direct-ip--centralex)
   - [Call Termination (ST Button / Hangup)](#call-termination-st-button--hangup)
6. [Telnet & Serial Command Console (CLI)](#telnet--serial-command-console-cli)
7. [i-Telex Protocol Compliance](#i-telex-protocol-compliance)
8. [Building & Flashing with PlatformIO](#building--flashing-with-platformio)

---

## Key Features

- **Full TW39 Signaling & 50-BaudITA2/Baudot Engine**:
  - Automatic idle current (*Ruhestrom*, ~3–5 mA) and active current (*Schreibstrom*, ~40 mA) polarity switching with H-bridge driver control.
  - Hardware-timed 50-baud 5N2 UART framing via an external **SC16IS752** I²C UART with true transmitter-empty (`TEMT` / `THRE`) flow-control pacing.
  - Software **echo cancellation queue** (`echo_queue`) that suppresses half-duplex local loop echoes while preserving genuine keyboard input.
  - Fast hardware + current-verified **ST break detection** (>400 ms open loop) and post-hangup H-relay settling guard.
- **i-Telex & Centralex Protocol Support**:
  - Binary **TNS (Telex Roll-Call Server)** subscriber lookup (`tlnserv.teleprinter.net` / `tlnserv2.teleprinter.net` / `tlnserv3.teleprinter.net`, port `11811`) supporting hostname and static/dynamic IP entries (connection types `1`–`5`).
  - Full **Centralex** client (`0x81` Connect Remote, `0x82` Confirm, `0x00` 15s Heartbeat, `0x83` Incoming Call, `0x84` Accept Call, `0x03 'occ'` Occupied disconnect during outgoing calls, and automatic 5s standby reconnection after call end).
- **Diagnostics, Self-Test & Safety**:
  - Real-time loop current measurement via **INA226** over I²C.
  - Hardware **Self-Test (`TEST` button)** that verifies I²C peripherals, SD card, Wi-Fi/DNS, Centralex, and TW39 idle/active loop currents, displaying results on the front-panel LEDs and printing a diagnostic slip on the physical teletype.
  - Hardware **Emergency Disconnect (`DISCONNECT` button)** that cleanly closes any active session and briefly interrupts loop power (`250 ms`) to release stuck FSG relays.

---

## Hardware Architecture & Pinout

### ESP32 GPIO Pin Assignment

| Signal / Function | ESP32 GPIO | Direction | Description |
| :--- | :---: | :---: | :--- |
| `STATUS_LED_PIN` | `GPIO 13` | Output | Onboard 1 Hz heartbeat indicator |
| `DRIVER_ON_PIN` | `GPIO 14` | Output | TW39 line driver power enable (`1` = ON, `0` = OFF) |
| `POL_SWITCH_PIN` | `GPIO 27` | Output | TW39 loop polarity switch (`1` = Normal/Idle, `0` = Reversed/Active; invertible via config) |
| `TTY_RX_PIN` | `GPIO 4` | Input | Fast digital current-loop receiver sense (`HIGH` = Mark / loop closed, `LOW` = Space / loop open) |
| `CURRENT_ALERT_PIN` | `GPIO 26` | Input | INA226 alert input |
| `EXT_UART_NIRQ_PIN` | `GPIO 16` | Input (Pull-up) | SC16IS752 active-LOW interrupt request |
| `EXT_UART_NRST_PIN` | `GPIO 25` | Output | SC16IS752 active-LOW hardware reset |
| `TCA_IRQ` | `GPIO 34` | Input | TCA9555 active-LOW interrupt input |
| `I2C_SDA_PIN` | `GPIO 21` | I/O | I²C Data (`TCA9555` @ `0x20`, `INA226` @ `0x40`, `SC16IS752` @ `0x4D`/`0x9A`) |
| `I2C_SCL_PIN` | `GPIO 22` | Output | I²C Clock |
| `SD_DETECT_PIN` | `GPIO 17` | Input | SD card slot detect switch (active-LOW when inserted) |
| `SD_CS_PIN` | `GPIO 5` | Output | SPI SD Card Chip Select |
| `SD_SCK_PIN` | `GPIO 18` | Output | SPI Clock |
| `SD_SDO_PIN` | `GPIO 19` | Input | SPI MISO |
| `SD_SDI_PIN` | `GPIO 23` | Output | SPI MOSI |

### TCA9555 I²C Expander (`0x20`) Pin Mapping

| TCA Pin | Macro Name | Type | Description |
| :---: | :--- | :---: | :--- |
| `P00` | `TCA_IDLE_LED` | Output (Active-LOW) | Single-color **IDLE** state LED |
| `P01` | `TCA_DIALING_LED` | Output (Active-LOW) | Single-color **DIALING** state LED |
| `P02` | `TCA_CONNECTED_LED` | Output (Active-LOW) | Single-color **CONNECTED** state LED |
| `P03` | `TCA_SDCARD_LED_G` | Output (Active-LOW) | **SDCARD** RGB LED — Green channel |
| `P04` | `TCA_SDCARD_LED_R` | Output (Active-LOW) | **SDCARD** RGB LED — Red channel |
| `P05` | `TCA_SDCARD_LED_B` | Output (Active-LOW) | **SDCARD** RGB LED — Blue channel |
| `P06` | `TCA_NETWORK_LED_G` | Output (Active-LOW) | **NETWORK** RGB LED — Green channel |
| `P07` | `TCA_NETWORK_LED_R` | Output (Active-LOW) | **NETWORK** RGB LED — Red channel |
| `P10` (`8`) | `TCA_NETWORK_LED_B` | Output (Active-LOW) | **NETWORK** RGB LED — Blue channel |
| `P11` (`9`) | `TCA_ITELEX_LED_G` | Output (Active-LOW) | **ITELEX** RGB LED — Green channel |
| `P12` (`10`) | `TCA_ITELEX_LED_R` | Output (Active-LOW) | **ITELEX** RGB LED — Red channel |
| `P13` (`11`) | `TCA_ITELEX_LED_B` | Output (Active-LOW) | **ITELEX** RGB LED — Blue channel |
| `P16` (`14`) | `TCA_TEST_BTN` | Input (Active-LOW) | Front-panel **TEST** button |
| `P17` (`15`) | `TCA_DISCONNECT_BTN` | Input (Active-LOW) | Front-panel **DISCONNECT** button |

---

## Front Panel User Interface (LEDs & Buttons)

### Single-Color State LEDs

| System State | `IDLE` LED | `DIALING` LED | `CONNECTED` LED |
| :--- | :---: | :---: | :---: |
| **Idle (`ITELEX_IDLE_STATE`)** | Slow Blink (1 Hz) | OFF | OFF |
| **Rotary Dialing (`ITELEX_DIALING_STATE`)** | OFF | **ON** (Solid) | OFF |
| **Connecting / Handshake (`CONNECTING`, `CS_*`)** | OFF | OFF | Slow Blink (1 Hz) |
| **Online / Connected / Local (`ONLINE`, `LOCAL`)** | OFF | OFF | **ON** (Solid) |
| **Disconnecting / Abort (`DISCONNECT`, `CS_ABORT`)** | **ON** | **ON** | **ON** |

---

### RGB Status LEDs (`SDCARD`, `NETWORK`, `ITELEX`)

| RGB Indicator | Color / Pattern | Meaning |
| :--- | :--- | :--- |
| **`SDCARD`** | **Green** (Solid) | SD card inserted and filesystem mounted |
| | **Blue** (Pulse) | SD card Read/Write activity (loading config, subscriber lookup, writing logs) |
| | **Red** (Solid) | SD card missing or mount failure |
| **`NETWORK`** | **Green** (Solid) | Wi-Fi connected and ready |
| | **Blue** (Pulse) | Network packet / Nameserver query / Baudot TCP activity |
| | **Yellow** (Fast Blink) | Wi-Fi scanning, connecting, or reconnecting |
| | **Red** (Solid) | Wi-Fi connection failed / no network |
| **`ITELEX`** | **Green** (Solid) | Ready to receive calls (Centralex `STANDBY` confirmed, or Direct-IP listening on port `134`) |
| | **Yellow** (Fast Blink) | Centralex connecting or waiting for `0x82` login confirmation |
| | **Yellow** (Solid) | Centralex waiting for reconnect timer (`WAIT_RECONNECT`) |
| | **Blue** (Fast Blink) | Outgoing rotary dialing or i-Telex version/direct-dial handshake in progress |
| | **Blue** (Solid) | Active i-Telex call established (`ITELEX_ONLINE_STATE`) |
| | **Cyan** (Pulse / Solid) | Flashes Cyan on Baudot TX/RX telegrams & TNS lookups; Solid Cyan in `LOCAL` mode |
| | **Red** (Solid) | Disconnecting, handshake aborted, Wi-Fi down, or Centralex configuration error |

---

### Front-Panel Buttons (`TEST` & `DISCONNECT`)

#### 1. `TEST` Button — Hardware & TW39 Line Self-Test
Pressing the **`TEST`** button (or running `selftest` / `test` in the CLI) executes a comprehensive diagnostic routine:
1. **LED Lamp Test**: Sweeps **Red -> Green -> Blue** across the front-panel LEDs to verify all LED channels.
2. **I²C Peripheral Check**: Verifies communication with the `TCA9555` IO expander (`0x20`), `INA226` current sensor (`0x40`), and `SC16IS752` dual UART.
3. **SD Card & Config Check**: Verifies SD card presence/mount and checks `/configs/wifi.txt` and `/configs/centralex.txt`.
4. **Network & TNS Check**: Verifies Wi-Fi connection, IP address, RSSI, and DNS resolution of `tlnserv2.teleprinter.net`.
5. **i-Telex / Centralex Check**: Verifies Centralex standby state or Direct-IP server readiness.
6. **TW39 Current-Loop Test & Teletype Printout**:
   - When the station is `IDLE`, it measures **Idle Current (*Ruhestrom*)** (pass range: `1.5–12.0 mA`), then switches to active polarity for `600 ms` to measure **Active Current (*Schreibstrom*)** (pass range: `20.0–65.0 mA`) and checks that `TTY_RX` (`GPIO 4`) closes to `HIGH`.
   - If a teletype is connected and active current is normal, ESPTelex spins up the motor and **prints a formatted diagnostic report directly onto the teletype**, then returns cleanly to idle polarity:
     ```text
     --- esptelex selftest ---
     hw: ok  sd: ok
     net: ok (192.168.1.50)
     itlx: cx 123456 ok
     line idle: 3.8 ma
     line actv: 41.9 ma
     ```
7. **4-Second LED Result Summary**:
   - **`SDCARD` RGB**: Green = OK, Red = Fail
   - **`NETWORK` RGB**: Green = Wi-Fi + DNS OK, Yellow = Wi-Fi only, Red = Disconnected
   - **`ITELEX` RGB**: Green = i-Telex + I²C HW OK, Yellow = i-Telex warning, Red = HW/i-Telex fault
   - **`IDLE` LED**: Solid ON if Ruhestrom passed
   - **`CONNECTED` LED**: Solid ON if Schreibstrom passed
   - **`DIALING` LED**: Fast Blink if either TW39 line current test failed

#### 2. `DISCONNECT` Button — Emergency Force Disconnect
Pressing the **`DISCONNECT`** button (or running `forcedisc` in the CLI) forces an immediate station reset in case of a teletype or line failure:
- Sends an i-Telex `End (0x03)` packet if in a call (or `occ` if in Centralex standby) and closes the TCP connection.
- **Cuts TW39 loop driver power (`DRIVER_ON_PIN = 0`) for `250 ms`** while switching back to idle polarity, forcing any mechanically or electrically latched FSG H-relay to drop out immediately.
- Flushes all SC16IS752 hardware FIFOs and software Baudot/echo buffers, resets rotary dial counters, returns the state machine to `ITELEX_IDLE_STATE`, and schedules Centralex standby reconnection in 5 seconds.

---

## SD Card Configuration & Directory Structure

Copy the contents of the [`sd-contents/`](sd-contents/) folder to the root of a FAT32-formatted microSD card:

```text
/
├── configs/
│   ├── wifi.txt          # Wi-Fi credentials (one SSID + password pair per 2 lines)
│   ├── centralex.txt     # Centralex & polarity configuration
│   └── hardware.txt      # Optional hardware settings (e.g. invert_polarity=1)
├── logs/
│   └── debuglog.txt      # Timestamped system & call logs
└── telexdirectory/
    ├── 20.txt            # Local speed-dial entries (numbers <= 9999)
    └── ...
```

### Wi-Fi Configuration (`/configs/wifi.txt`)
`/configs/wifi.txt` stores one or more Wi-Fi networks as consecutive pairs of lines (`SSID` on the first line, `PASSWORD` on the second line). ESPTelex tries each entry in order until connected:

```text
MyWorkshopWiFi
super_secret_password
BackupHotspot
hotspot_password
```

### Centralex & Polarity Configuration (`/configs/centralex.txt`)
`/configs/centralex.txt` configures Centralex remote standby and optional TW39 line polarity inversion:

```ini
# ESPTelex Centralex Configuration
# enabled: 1 = enable Centralex standby, 0 = disable (direct IP mode only)
enabled=1

# Centralex Server hostname or IP (primary: tlnserv2.teleprinter.net or 78.47.222.94)
server=tlnserv2.teleprinter.net

# Centralex Server port (default: 49491)
port=49491

# Your registered i-Telex subscriber number
number=123456

# Your subscriber PIN
pin=0000

# Optional: Invert line polarity (1 = invert idle/active polarity for machines or wiring with reversed polarity)
invert_polarity=0
```

### Local Speed-Dial Directory (`/telexdirectory/<num>.txt`)
Dialing any number **`1`–`9999`** looks up `/telexdirectory/<number>.txt` on the SD card instead of querying the public TNS nameserver. Each file has 11 newline-separated fields:

```text
118811
Automatische Auskunft =a
2
1
telex.telephontechnik.de
0.0.0.0
5001
0
0
0
```
*(Fields in order: `call_number`, `name`, `flags`, `connection_type`, `hostname`, `IP_address`, `port`, `extension`, `pin`, `timestamp`).*

---

## Operating Modes & Calling Workflows

### Outgoing Calls via Rotary Dial (TW39)
1. Press the **AT (Anruftaste)** on the FSG. Loop current rises from ~3 mA (*Ruhestrom*) to ~40 mA.
2. After 1 second of sustained current, ESPTelex pulses a **25 ms interruption** (`Wahlaufforderungsimpuls`) to signal dial-tone readiness and lights the **`DIALING` LED**.
3. Dial the subscriber number on the rotary dial:
   - **`1`–`9999`**: Loaded from `/telexdirectory/<num>.txt` on the SD card.
   - **`>= 10000`**: Queried live from the i-Telex TNS nameserver (`tlnserv3.teleprinter.net:11811`).
4. Once the 3-second inter-digit timeout expires, ESPTelex connects to the peer, reverses loop polarity to start the teletype motor, negotiates i-Telex protocol `Version 1` + `Direct Dial`, and enters `ONLINE` state.

### Local Mode (Dialing `0`)
- Press **AT** and dial **`0`** on the rotary dial.
- ESPTelex reverses polarity to start the local teletype motor, prints the current date and time (`HH:MM:SS  DD.MM. YYYY`), and enters **`LOCAL` mode** (`ITELEX` LED = Cyan, `CONNECTED` LED = ON) for offline typing or paper-tape preparation.

### Incoming Calls (Direct-IP & Centralex)
- **Direct-IP Mode (`enabled=0`)**: ESPTelex listens on TCP port `134`. When an incoming TCP connection arrives, ESPTelex switches loop polarity to active, starts the teletype motor, and completes the i-Telex handshake.
- **Centralex Mode (`enabled=1`)**: ESPTelex maintains a persistent standby connection to the Centralex server (`0x81` -> `0x82`) with a 15-second heartbeat (`0x00`). When a remote caller dials your number on the Centralex server, the server sends `Remote Call (0x83)`, ESPTelex replies `Accept Call (0x84)`, spins up the teletype motor, and transitions the socket directly into the i-Telex session.

### Call Termination (ST Button / Hangup)
- Holding the **ST (Schlusstaste)** on the FSG breaks the TW39 loop (>400 ms at `< 10 mA`). ESPTelex detects the break, sends `End (0x03)` to the remote station, switches polarity back to idle, and powers down the teletype motor.
- When the remote station hangs up (sends `End 0x03` or closes the TCP socket), ESPTelex immediately restores idle polarity so the local teletype stops within `< 100 ms`, applies a 1.5-second H-relay release guard, and reconnects to Centralex standby 5 seconds later.

---

## Telnet & Serial Command Console (CLI)

Connect via **USB Serial (`115200 8N1`)** or **Telnet (TCP port `23`)**:

```bash
telnet <device-ip> 23
```

### Available Commands

| Command | Arguments / Alias | Description |
| :--- | :--- | :--- |
| `help` | — | Display the full command reference menu |
| `status` | — | Display system state, TW39 loop current, polarity, GPIO states, Wi-Fi RSSI, SD card, Centralex status, and NTP time |
| `call <number>` | — | Dial a subscriber number using the **physical teletype** |
| `dial <number>` | — | Dial a subscriber number in **Virtual Terminal mode** (teletype motor stays OFF; type & read via Telnet/Serial) |
| `msg <text>` | — | Send ASCII text converted to Baudot + `CR LF` in an active call (supports `\w` = WRU, `\r` = CR, `\n` = LF, `\b` = Bell) |
| `raw <text>` | — | Send ASCII text converted to Baudot **without** trailing `CR LF` |
| `wru` | — | Send `WRU` (*Who are you? / Wer da?*, `FIGS + D + LTRS`) to trigger remote answerback |
| `crlf` / `cr` / `lf` | — | Send Carriage Return + Line Feed, `CR` only, or `LF` only |
| `bell` | — | Send Bell signal (*Klingel*, `FIGS + J + LTRS`) |
| `hangup` | `disc` | Cleanly terminate the current i-Telex session (`End 0x03`) |
| `forcedisc` | — | Emergency force disconnect: closes TCP session and cuts loop driver power for `250 ms` |
| `selftest` | `test [notty]` | Run full hardware, SD, network, Centralex, and TW39 line self-test (`selftest notty` skips teletype printing) |
| `tx <text>` | `tty-txtext` | Print ASCII text directly to the local physical teletype |
| `query <number>` | `tty-dial` | Look up subscriber metadata from SD directory (`<= 9999`) or TNS nameserver (`> 9999`) |
| `centralex` | `[reconnect\|disconnect]` | Show Centralex configuration/state, reload & reconnect standby, or disconnect with `occ` |
| `line` | `loop` | Display real-time TW39 current (signed & absolute mA), polarity, `TTY_RX`, and driver state |
| `pol [0\|1]` | `polarity` | Toggle or explicitly set the TW39 polarity pin (`0` = Reversed, `1` = Normal) |
| `invert-polarity` | `[0\|1]` | Toggle or set idle/active polarity inversion at runtime |
| `pwr [0\|1]` | — | Toggle or set TW39 line driver power (`DRIVER_ON_PIN`) |
| `set-verbosity` | `<0-3>` | Set console log level (`0` = Silent, `1` = Basic, `2` = Detailed, `3` = Verbose) |
| `time` | — | Display current RTC / NTP synchronized time |
| `reboot` | — | Restart the ESP32 |
| `exit` | *(Telnet only)* | Disconnect the active Telnet session |

---

## i-Telex Protocol Compliance

ESPTelex implements the official **i-Telex Communication Specification (r874)**:

| Packet Type | Code | Direction | Implementation Details |
| :--- | :---: | :---: | :--- |
| **Heartbeat** | `0x00` | Bidirectional | Sent every 15s in Centralex standby; resets 30s online inactivity timer |
| **Direct Dial** | `0x01` | Caller -> Callee | Sends target extension (`0` = default); also supports incoming calls that omit `0x01` before `0x02` per Spec §2.2.2 |
| **Baudot Data** | `0x02` | Bidirectional | Single or multi-byte ITA2 payloads (`0x01`–`0x1F`) with automatic echo cancellation |
| **End** | `0x03` | Bidirectional | Clean disconnect (`03 00`) and Centralex occupied notification (`03 03 'o' 'c' 'c'`) |
| **Reject** | `0x04` | Callee -> Caller | Parses and logs ASCII reject reasons (`occ`, `abs`, `na`, `nc`, `der`) and terminates call |
| **Acknowledge** | `0x06` | Bidirectional | Sent every 1s in `ONLINE` state; counter increments strictly as bytes leave the SC16IS752 50-baud transmit register |
| **Version** | `0x07` | Bidirectional | Negotiates Protocol Version `1` (`07 01 01`), including downgrade negotiation when peers propose Version > 1 |
| **Loopback** | `0x08` | Bidirectional | Accepted and resets connection timeout timer |
| **Connect Remote** | `0x81` | Client -> Centralex | 6-byte payload: 32-bit little-endian subscriber number + 16-bit little-endian PIN |
| **Remote Confirm** | `0x82` | Centralex -> Client | Transitions station into `CENTRALEX_STATE_STANDBY` |
| **Remote Call** | `0x83` | Centralex -> Client | Incoming call alert; answered with `0x84` (`Accept Call`) when `IDLE` or rejected with `0x04 "occ"` if busy |
| **Accept Call** | `0x84` | Client -> Centralex | Accepts incoming Centralex call and starts TW39 active loop & i-Telex handshake |

---

## Building & Flashing with PlatformIO

### Prerequisites
- [PlatformIO Core (CLI)](https://docs.platformio.org/en/latest/core/index.html) or VS Code with the PlatformIO IDE extension.

### Build Firmware
```bash
pio run
```

### Flash via USB
```bash
pio run --target upload
```

### Flash via Over-The-Air (OTA)
Uncomment `upload_protocol = espota` and set `upload_port = <device-ip>` in [`platformio.ini`](platformio.ini), then run:
```bash
pio run --target upload
```

