# Channel plan

Marine VHF offers the international channels of the ITU Radio Regulations, Appendix 18
("Table of transmitting frequencies in the VHF maritime mobile band"), and nothing that a
single country or region adds or changes. This page lists the sources, the channels in the
selector, the channels left out and why, and the choices that are still open.

## Source

- **ITU Radio Regulations, Appendix 18 (Rev.WRC-19).** WRC-12 and WRC-15 reorganised it
  for digital data (AIS application-specific messages, VDES), WRC-19 added the VDES
  satellite component. I found no WRC-23 revision of the table; WRC-23 revised
  Resolution 363, which asks WRC-27 to look at the band again, so the table may change in
  2027. The text used here: the WRC-15 Final Acts (Appendix 18 pages and notes) and the
  USCG NAVCEN transcription of the WRC-19 table.
- **ITU-R M.1084-5, Annex 4**: the four-digit designators (1xxx = the ship frequency of a
  two-frequency channel used as a single-frequency channel, 2xxx = the coast frequency).
- **ETSI EN 301 025-1** (fixed VHF with DSC), clause 5.4, for the dual-watch timing, and
  **ETSI EN 301 178** (portable VHF, non-GMDSS) for power and channel rules. Neither is
  binding on this app; they are what an IC-M94-class radio is tested against, so the app
  follows them where it can.

`tools/app18_reference.csv` is the Appendix 18 table typed in row by row, and
`tools/check_channels.py` checks the app's table (built for the host) against it.

## The frequencies

Every channel has a ship (lower-leg) frequency between 156.025 and 157.425 MHz:

- 01..28: 156.050 + (n - 1) x 0.050 MHz
- 60..88: 156.025 + (n - 60) x 0.050 MHz

A two-frequency (duplex) channel also has a coast frequency 4.6 MHz higher (160.625 to
162.000 MHz). A ship always **transmits on the ship frequency** and, on a duplex channel,
**receives on the coast frequency**.

## Duplex channels: yes, they are international

Appendix 18 gives a coast-station frequency for 01-05, 07, 18-28, 60-66 and 78-86: these
are two-frequency channels for port operations and public correspondence, everywhere.
Note m) lets administrations run most of them single-frequency instead, subject to
coordination, which is where the national 1xxx/"A" channels come from; the two-frequency
form is the international default, so that is what the app does: on 01-05, 07, 18-23,
60-66 and 78-83 it receives on the coast frequency and transmits on the ship frequency
(DUP on the status line).

## The selector

58 channels, in numeric order, each four-digit channel right after its two-digit one (as
on the IC-M94). RX/TX in MHz.

| Ch | RX | TX | | Ch | RX | TX | |
|---|---|---|---|---|---|---|---|
| 01 | 160.650 | 156.050 | dup | 1027 | 157.350 | 157.350 | z, zz |
| 02 | 160.700 | 156.100 | dup | 1028 | 157.400 | 157.400 | z, zz |
| 03 | 160.750 | 156.150 | dup | 60 | 160.625 | 156.025 | dup |
| 04 | 160.800 | 156.200 | dup | 61 | 160.675 | 156.075 | dup |
| 05 | 160.850 | 156.250 | dup | 62 | 160.725 | 156.125 | dup |
| 06 | 156.300 | 156.300 | intership, SAR (f) | 63 | 160.775 | 156.175 | dup |
| 07 | 160.950 | 156.350 | dup | 64 | 160.825 | 156.225 | dup |
| 08 | 156.400 | 156.400 | intership | 65 | 160.875 | 156.275 | dup |
| 09 | 156.450 | 156.450 | | 66 | 160.925 | 156.325 | dup |
| 10 | 156.500 | 156.500 | | 67 | 156.375 | 156.375 | |
| 11 | 156.550 | 156.550 | | 68 | 156.425 | 156.425 | |
| 12 | 156.600 | 156.600 | | 69 | 156.475 | 156.475 | |
| 13 | 156.650 | 156.650 | navigation safety (k) | 71 | 156.575 | 156.575 | |
| 14 | 156.700 | 156.700 | | 72 | 156.625 | 156.625 | intership |
| 15 | 156.750 | 156.750 | (g) | 73 | 156.675 | 156.675 | |
| **16** | 156.800 | 156.800 | distress, safety, calling | 74 | 156.725 | 156.725 | |
| 17 | 156.850 | 156.850 | (g) | 75 | 156.775 | 156.775 | **1 W** (n) |
| 18 | 161.500 | 156.900 | dup | 76 | 156.825 | 156.825 | **1 W** (n) |
| 19 | 161.550 | 156.950 | dup | 77 | 156.875 | 156.875 | intership |
| 1019 | 156.950 | 156.950 | | 78 | 161.525 | 156.925 | dup |
| 2019 | 161.550 | — | receive only (mm) | 1078 | 156.925 | 156.925 | |
| 20 | 161.600 | 157.000 | dup | 2078 | 161.525 | — | receive only (mm) |
| 1020 | 157.000 | 157.000 | | 79 | 161.575 | 156.975 | dup |
| 2020 | 161.600 | — | receive only (mm) | 1079 | 156.975 | 156.975 | |
| 21 | 161.650 | 157.050 | dup | 2079 | 161.575 | — | receive only (mm) |
| 22 | 161.700 | 157.100 | dup | 80 | 161.625 | 157.025 | dup |
| 23 | 161.750 | 157.150 | dup | 81 | 161.675 | 157.075 | dup |
| | | | | 82 | 161.725 | 157.125 | dup |
| | | | | 83 | 161.775 | 157.175 | dup |
| | | | | 87 | 157.375 | 157.375 | z, zz |
| | | | | 88 | 157.425 | 157.425 | z, zz |

25 duplex, 4 receive-only. With `MARINE_VDES_VOICE=1`, 24-26 and 84-86 are added as duplex
channels (64 in all).

Power: selecting 16 sets HI (EN 301 178: channel 16 always starts at full power, the user
may lower it); 75 and 76 are held at LO whatever is selected (App18 note n: 1 W to protect
channel 16).

## Left out, and why

| App18 row | Why it is not in the selector |
|---|---|
| 70 (156.525) | DSC only (note j); EN 301 178 requires that it cannot be used otherwise |
| AIS 1, AIS 2 (161.975, 162.025) | AIS only |
| 2027 / ASM 1, 2028 / ASM 2 (161.950, 162.000) | application-specific messages, data (note z) |
| 27, 28 duplex | since 1 January 2019 each is split into two simplex channels (note z): 1027/1028 for voice (zz), 2027/2028 for ASM data. Only the United States keeps 27/28 as duplex public-correspondence channels (note zx), so that is a national use |
| 1024-2086 | VDES, digital only (terrestrial and satellite) |
| 24-26, 84-86 | see the choices below |
| 2006 (160.900) | experimental use and autonomous maritime radio devices, not voice (note r) |

Also left out because they are national or regional, not App18 channels or uses:
the USA/Canada simplex "A" channels other than 1019/1020/1078/1079 (1001, 1005, 1007,
1018, 1021-1023, 1063, 1065, 1066, 1080-1083), the Canadian "B" receive-only channels,
weather channels, the Nordic leisure channels L1-L3, UK M1/M2 and private (P) channels,
Dutch channel 31, and the 12.5 kHz interleaved channels of ITU-R M.1084 Annex 4 (3-digit
designators such as 202).

## Choices still open

These are where "international" is not one answer. Each is a build option or a one-line
change.

1. **24, 25, 26, 84, 85, 86: out by default** (`MARINE_VDES_VOICE=1` adds them as duplex
   voice). App18 still lists them with voice columns, but its notes make their use
   regional: in Region 2 they are designated for digital emissions (note ww); in Regions 1
   and 3 they are identified for VDES, with analogue voice allowed only without protection
   and subject to coordination (note w); some African countries and China designate them
   for digital use (note x). In CEPT countries ECC/DEC/(19)03 gave them to VDES after a
   transition that ended in 2023; the USCG says they cannot be used in the US from 1
   January 2030. Many radios still offer them as voice channels (for instance the
   channel list of the JRC JHS-770S).
2. **27 and 28: replaced by 1027 and 1028** (App18 note z), rather than kept as duplex
   channels as many older radios do.
3. **1019, 1020, 1078, 1079 and 2019, 2020, 2078, 2079: in.** They are rows of App18
   itself (added by WRC-15), unlike the other four-digit channels. The 2xxx ones are
   receive-only, since App18 has no ship frequency for them (note mm: ships may transmit
   there only where national rules allow).
4. **15 and 17 at any power** (`MARINE_LOW_15_17=1` holds them at LO). App18 note g limits
   them to 1 W e.r.p. only when they are used for on-board communication; many radios
   force low power on them anyway.

## Plans that give a channel number other frequencies

Yes, several, which is why a channel group setting (INT/USA/CAN) exists on radios like the
IC-M94:

- **USA**: many App18 duplex channels are used single-frequency on the ship frequency, the
  old "A" channels, now 1xxx (for instance US 22A/1022 is 157.100 simplex, the USCG liaison
  channel, where international 22 is 157.100/161.700 duplex). Several others do not exist
  in the US plan, 15 is receive-only, and 27/28 stay duplex. The FCC has not adopted
  1027/1028.
- **Canada**: a different mix of "A" channels, plus "B" channels: the coast frequency
  received only (continuous marine broadcasts).
- **12.5 kHz interleaved channels** (ITU-R M.1084 Annex 4, App18 note e): where an
  administration uses them, three-digit channels sit between the 25 kHz ones.

In all of these the ship's transmit frequency for a given number stays the same; what
changes is what the radio listens on (coast frequency, ship frequency, or nothing), and
whether the channel may be used at all. So supporting another plan later would only need
another table of designators and flags, not other frequency maths. The app does not do it,
on purpose.
