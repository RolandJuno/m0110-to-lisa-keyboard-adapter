/*

Copyright 2011, 2012 Jun Wako <wakojun@gmail.com>

This software is licensed with a Modified BSD License.
All of this is supposed to be Free Software, Open Source, DFSG-free,
GPL-compatible, and OK to use in both free and proprietary applications.
Additions and corrections to this file are welcome.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in
  the documentation and/or other materials provided with the
  distribution.

* Neither the name of the copyright holders nor the names of
  contributors may be used to endorse or promote products derived
  from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

M0110A Support was contributed by skagon@github

Modifications for use with RebeccaRGB's LisaKeys project contributed by warmech@github

*/

//Include headers
#include <Arduino.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "m0110.h"

//Declare function prototypes
static inline uint8_t raw2scan(uint8_t raw);
static inline uint8_t inquiry(void);
static inline uint8_t instant(void);
static inline void clock_lo(void);
static inline void clock_hi(void);
static inline bool clock_in(void);
static inline void data_lo(void);
static inline void data_hi(void);
static inline bool data_in(void);
static inline uint16_t wait_clock_lo(uint16_t us);
static inline uint16_t wait_clock_hi(uint16_t us);
static inline uint16_t wait_data_lo(uint16_t us);
static inline uint16_t wait_data_hi(uint16_t us);
static inline void idle(void);
static inline void request(void);

//This pin corresponds to DIP switch 3 - NEW
static const uint8_t kbd_cable_type_pin = M0110_CABLE_TYPE;

//Setup() calls determine_keyboard_cable() to determine these values - MODIFIED
uint8_t M0110_CLOCK_BIT;
uint8_t M0110_DATA_BIT;

//Grab the state of the CABLE pin to determine what keyboard cable is in use - NEW
static inline void determine_keyboard_cable() {
    //if (digitalRead(kbd_cable_type_pin) == HIGH) { // Paul: I think this should be LOW,pin is pulled up. DIP open is HIGH, DIP closed is to GND
    if (digitalRead(kbd_cable_type_pin) == LOW) {
        //We're using a standard telephone handset cable
        M0110_CLOCK_BIT = 0; // PORTD0 is Pin 6
        M0110_DATA_BIT = 1;  // PORTD1 is Pin 5
    } else {
        //We're using a Macintosh keyboard cable
        M0110_CLOCK_BIT = 1; // PORTD1 is Pin 5
        M0110_DATA_BIT = 0;  // PORTD0 is Pin 6
    }
}

//Define microsecond wait statement with error handling in function
#define WAIT_US(stat, us, err) do { \
    if (!wait_##stat(us)) {         \
        m0110_error = err;          \
        goto ERROR;                 \
    }                               \
} while (0)

//Define millisecond wait statement with error handling in function
#define WAIT_MS(stat, ms, err) do { \
    uint16_t _ms = ms;              \
    while (_ms) {                   \
        if (wait_##stat(1000)) {    \
            break;                  \
        }                           \
        _ms--;                      \
    }                               \
    if (_ms == 0) {                 \
        m0110_error = err;          \
        goto ERROR;                 \
    }                               \
} while (0)

//Define bitwise operation checks for key presses and releases
#define KEY(raw)        ((raw) & 0x7f)
#define IS_BREAK(raw)   (((raw) & 0x80) == 0x80)

//Initialize error flag
uint8_t m0110_error = 0;

//Keyboard initializer - this is used to determine the correct keymap to use - MODIFIED
uint8_t m0110_init()
{
  uint8_t data;

  m0110_send(M0110_MODEL);
  data = m0110_recv();

  //m0110_send(M0110_TEST);
  //data = m0110_recv();

  return data;
}

//Provide a text name for the Mac keyboard model - NEW
char * m0110_identify_keyboard(uint8_t m0110_model) {
  switch (m0110_model) {
    case 0x03:
      return "M0110 (US)";
    case 0x09:
      return "M0110 (International)";
    case 0x0B:
      return "M0110A";
    case 0x11:
      return "M0120";
    case 0x13:
      return "M0110 (US) + M0120";
    case 0x19:
      return "M0110 (International) + M0120";
    case 0x1B:
      return "M0110A + M0120";
    default:
      return "No Macintosh keyboard is attached";
  }
}

//Sends commands to the M0110/A
uint8_t m0110_send(uint8_t data)
{
    /*
    From the Mac Plus Technical Information page at the 68K
    Mac Linux site (http://www.mac.linux-m68k.org/devel/plushw.php):

    Command name    Value   Keyboard response
    Inquiry         $10     Key Transition code or Null ($7B)
    Instant         $14     Key Transition code or Null ($7B)
    Model Number    $16     Bit 0:    1
                            Bits 1-3: keyboard model number, 1-8
                            Bits 4-6: next device number, 1-8
                            Bit 7:    1 if another device connected
    Test            $36     ACK ($7D) or NAK ($77)
    */

    m0110_error = 0;

    //Set CLK to HIGH and DAT to LOW
    request();

    //The keyboard may block long time
    WAIT_MS(clock_lo, 250, 1);  
    for (uint8_t bit = 0x80; bit; bit >>= 1) {
        WAIT_US(clock_lo, 250, 3);
        if (data & bit) {
            data_hi();
        } else {
            data_lo();
        }
        WAIT_US(clock_hi, 200, 4);
    }

    // Hold the last bit for ~80us
    _delay_us(100);
    idle();
    return 1;

ERROR:
    _delay_ms(500);
    idle();
    return 0;
}

//Gets data from the keyboard
uint8_t m0110_recv(void)
{
    //INIT data byte
    uint8_t data = 0;
    m0110_error = 0;

    //The keyboard may block long time
    WAIT_MS(clock_lo, 250, 1);
    for (uint8_t i = 0; i < 8; i++) {
        data <<= 1;
        WAIT_US(clock_lo, 200, 2);
        WAIT_US(clock_hi, 200, 3);
        if (data_in()) {
            data |= 1;
        }
    }
    idle();
    return data;

ERROR:
    _delay_ms(500);
    idle();
    return 0xFF;
}

//Get the keypress/release data byte from the keyboard
uint8_t m0110_recv_key(void)
{
    static uint8_t keybuf = 0x00;
    static uint8_t keybuf2 = 0x00;
    static uint8_t rawbuf = 0x00;
    uint8_t raw, raw2, raw3;

    if (keybuf) {
        raw = keybuf;
        keybuf = 0x00;
        return raw;
    }
    if (keybuf2) {
        raw = keybuf2;
        keybuf2 = 0x00;
        return raw;
    }

    if (rawbuf) {
        raw = rawbuf;
        rawbuf = 0x00;
    } else {
        raw = instant();  // Use INSTANT for better response. Should be INQUIRY ?
    }

    switch (KEY(raw)) {

        case M0110_KEYPAD:
            raw2 = instant();
            switch (KEY(raw2)) {
                case M0110_ARROW_UP:
                case M0110_ARROW_DOWN:
                case M0110_ARROW_LEFT:
                case M0110_ARROW_RIGHT:
                    if (IS_BREAK(raw2)) {
                        // Case B,F,N:
                        keybuf = (raw2scan(raw2) | M0110_CALC_OFFSET); // Calc(u)
                        return (raw2scan(raw2) | M0110_KEYPAD_OFFSET); // Arrow(u)
                    }
                    break;
            }
            //Keypad or Arrow
            return (raw2scan(raw2) | M0110_KEYPAD_OFFSET);
            break;

        case M0110_SHIFT:
            raw2 = instant();
            switch (KEY(raw2)) {

                case M0110_SHIFT:
                    //Case: 5-8,C,G,H
                    rawbuf = raw2;
                    return raw2scan(raw); // Shift(d/u)
                    break;

                case M0110_KEYPAD:
                    //Shift + Arrow, Calc, or etc.
                    raw3 = instant();
                    switch (KEY(raw3)) {

                        case M0110_ARROW_UP:
                        case M0110_ARROW_DOWN:
                        case M0110_ARROW_LEFT:
                        case M0110_ARROW_RIGHT:
                            if (IS_BREAK(raw)) {
                                if (IS_BREAK(raw3)) {
                                    //Case 4:
                                    keybuf2 = raw2scan(raw); //Shift (UP)
                                    keybuf  = (raw2scan(raw3) | M0110_CALC_OFFSET); //Calc (UP)
                                    return (raw2scan(raw3) | M0110_KEYPAD_OFFSET);  //Arrow (UP)
                                } else {
                                    //Case 3:
                                    return (raw2scan(raw)); // Shift(u)
                                }
                            } else {
                                if (IS_BREAK(raw3)) {
                                    //Case 2:
                                    keybuf  = (raw2scan(raw3) | M0110_CALC_OFFSET); //Calc (UP)
                                    return (raw2scan(raw3) | M0110_KEYPAD_OFFSET);  //Arrow (UP)
                                } else {
                                    //Case 1:
                                    return (raw2scan(raw3) | M0110_CALC_OFFSET); //Calc (DOWN)
                                }
                            }
                            break;
                        default:
                            //Shift + Keypad
                            keybuf = (raw2scan(raw3) | M0110_KEYPAD_OFFSET);
                            //Shift (DOWN/UP)
                            return raw2scan(raw);
                            break;
                    }
                    break;
                default:
                    //Shift + Normal keys
                    keybuf = raw2scan(raw2);
                    //Shift (DOWN/UP)
                    return raw2scan(raw);
                    break;
            }
            break;
        default:
            //Normal keys
            return raw2scan(raw);
            break;
    }
}


uint8_t raw2scan(uint8_t raw)
{
    // Hack to swap the T and Y keys (by Paul Rickards) Oct 29, 2023
    switch (raw) {
      case 0x23:
        return 0x10;
        break;
      case 0xA3:
        return 0x90;
        break;
      case 0x21:
        return 0x11;
        break;
      case 0xA1:
        return 0x91;
        break;
    }
    return (raw == M0110_NULL) ? M0110_NULL : ((raw == M0110_ERROR) ? M0110_ERROR : (((raw&0x80) | ((raw & 0x7F) >> 1))));
}

static inline uint8_t inquiry(void)
{
    m0110_send(M0110_INQUIRY);
    return m0110_recv();
}

static inline uint8_t instant(void)
{
    m0110_send(M0110_INSTANT);
    uint8_t data = m0110_recv();
    if (data != M0110_NULL) {
        //debug_hex(data); debug(" ");
    }
    return data;
}

static inline void clock_lo()
{
    M0110_CLOCK_PORT &= ~(1<<M0110_CLOCK_BIT);
    M0110_CLOCK_DDR  |=  (1<<M0110_CLOCK_BIT);
}
static inline void clock_hi()
{
    /* input with pull up */
    M0110_CLOCK_DDR  &= ~(1<<M0110_CLOCK_BIT);
    M0110_CLOCK_PORT |=  (1<<M0110_CLOCK_BIT);
}
static inline bool clock_in()
{
    M0110_CLOCK_DDR  &= ~(1<<M0110_CLOCK_BIT);
    M0110_CLOCK_PORT |=  (1<<M0110_CLOCK_BIT);
    _delay_us(1);
    return M0110_CLOCK_PIN&(1<<M0110_CLOCK_BIT);
}
static inline void data_lo()
{
    M0110_DATA_PORT &= ~(1<<M0110_DATA_BIT);
    M0110_DATA_DDR  |=  (1<<M0110_DATA_BIT);
}
static inline void data_hi()
{
    /* input with pull up */
    M0110_DATA_DDR  &= ~(1<<M0110_DATA_BIT);
    M0110_DATA_PORT |=  (1<<M0110_DATA_BIT);
}
static inline bool data_in()
{
    M0110_DATA_DDR  &= ~(1<<M0110_DATA_BIT);
    M0110_DATA_PORT |=  (1<<M0110_DATA_BIT);
    _delay_us(1);
    return M0110_DATA_PIN&(1<<M0110_DATA_BIT);
}

static inline uint16_t wait_clock_lo(uint16_t us)
{
    while (clock_in() && us) { asm(""); _delay_us(1); us--; }
    return us;
}
static inline uint16_t wait_clock_hi(uint16_t us)
{
    while (!clock_in() && us) { asm(""); _delay_us(1); us--; }
    return us;
}
static inline uint16_t wait_data_lo(uint16_t us)
{
    while (data_in() && us)  { asm(""); _delay_us(1); us--; }
    return us;
}
static inline uint16_t wait_data_hi(uint16_t us)
{
    while (!data_in() && us)  { asm(""); _delay_us(1); us--; }
    return us;
}

static inline void idle(void)
{
    clock_hi();
    data_hi();
}

static inline void request(void)
{
    clock_hi();
    data_lo();
}

/*
Primitive M0110 Library for AVR
==============================


Signaling
---------
CLOCK is always from KEYBOARD. DATA are sent with MSB first.

1) IDLE: both lines are high.
    CLOCK ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    DATA  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

2) KEYBOARD->HOST: HOST reads bit on rising edge.
    CLOCK ~~~~~~~~~~~~|__|~~~|__|~~~|__|~~~|__|~~~|__|~~~|__|~~~|__|~~~|__|~~~~~~~~~~~
    DATA  ~~~~~~~~~~~~X777777X666666X555555X444444X333333X222222X111111X000000X~~~~~~~
                      <--> 160us(clock low)
                         <---> 180us(clock high)

3) HOST->KEYBOARD: HOST asserts bit on falling edge.
    CLOCK ~~~~~~~~~~~~|__|~~~|__|~~~|__|~~~|__|~~~|__|~~~|__|~~~|__|~~~|__|~~~~~~~~~~~
    DATA  ~~~~~~|_____X777777X666666X555555X444444X333333X222222X111111X000000X~~~~~~~
                <----> 840us(request to send by host)                     <---> 80us(hold DATA)
                      <--> 180us(clock low)
                         <---> 220us(clock high)


Protocol
--------
COMMAND:
    Inquiry     0x10    get key event with block
    Instant     0x12    get key event
    Model       0x14    get model number(M0110 responds with 0x09)
                        bit 7   1 if another device connected(used when keypad exists?)
                        bit4-6  next device model number
                        bit1-3  keyboard model number
                        bit 0   always 1
    Test        0x16    test(ACK:0x7D/NAK:0x77)

KEY EVENT:
    bit 7       key state(0:press 1:release)
    bit 6-1     scan code(see below)
    bit 0       always 1
    To get scan code use this: ((bits&(1<<7)) | ((bits&0x7F))>>1).

    Note: On the M0110A, Keypad keys and Arrow keys are preceded by 0x79.
          Moreover, some Keypad keys(=, /, * and +) are preceded by 0x71 on press and 0xF1 on release.

ARROW KEYS:
    Arrow keys and Calc keys(+,*,/,= on keypad) share same byte sequence and preceding byte of
    Calc keys(0x71 and 0xF1) means press and release event of SHIFT. This causes a very confusing situation,
    it is difficult or impossible to tell Calc key from Arrow key plus SHIFT in some cases.

    Raw key events:
            press               release
            ----------------    ----------------
    Left:         0x79, 0x0D          0x79, 0x8D
    Right:        0x79, 0x05          0x79, 0x85
    Up:           0x79, 0x1B          0x79, 0x9B
    Down:         0x79, 0x11          0x79, 0x91
    Pad+:   0x71, 0x79, 0x0D    0xF1, 0x79, 0x8D
    Pad*:   0x71, 0x79, 0x05    0xF1, 0x79, 0x85
    Pad/:   0x71, 0x79, 0x1B    0xF1, 0x79, 0x9B
    Pad=:   0x71, 0x79, 0x11    0xF1, 0x79, 0x91


RAW CODE:
    M0110A
    ,---------------------------------------------------------. ,---------------.
    |  `|  1|  2|  3|  4|  5|  6|  7|  8|  9|  0|  -|  =|Bcksp| |Clr|  =|  /|  *|
    |---------------------------------------------------------| |---------------|
    |Tab  |  Q|  W|  E|  R|  T|  Y|  U|  I|  O|  P|  [|  ]|   | |  7|  8|  9|  -|
    |-----------------------------------------------------'   | |---------------|
    |CapsLo|  A|  S|  D|  F|  G|  H|  J|  K|  L|  ;|  '|Return| |  4|  5|  6|  +|
    |---------------------------------------------------------| |---------------|
    |Shift   |  Z|  X|  C|  V|  B|  N|  M|  ,|  ,|  /|Shft|Up | |  1|  2|  3|   |
    |---------------------------------------------------------' |-----------|Ent|
    |Optio|Mac    |           Space           |  \|Lft|Rgt|Dn | |      0|  .|   |
    `---------------------------------------------------------' `---------------'
    ,---------------------------------------------------------. ,---------------.
    | 65| 25| 27| 29| 2B| 2F| 2D| 35| 39| 33| 3B| 37| 31|   67| |+0F|*11|*1B|*05|
    |---------------------------------------------------------| |---------------|
    |   61| 19| 1B| 1D| 1F| 23| 21| 41| 45| 3F| 47| 43| 3D|   | |+33|+37|+39|+1D|
    |-----------------------------------------------------'   | |---------------|
    |    73| 01| 03| 05| 07| 0B| 09| 4D| 51| 4B| 53| 4F|    49| |+2D|+2F|+31|*0D|
    |---------------------------------------------------------| |---------------|
    |      71| 0D| 0F| 11| 13| 17| 5B| 5D| 27| 5F| 59|  71|+1B| |+27|+29|+2B|   |
    |---------------------------------------------------------' |-----------|+19|
    |   75|     6F|            63             | 55|+0D|+05|+11| |    +25|+03|   |
    `---------------------------------------------------------' `---------------'
    + 0x79, 0xDD / 0xF1, 0xUU
    * 0x71, 0x79,DD / 0xF1, 0x79, 0xUU


MODEL NUMBER:
    M0110(GS536):           0x03  00000011 : model number 1 (001)
    M0110(GS624):           0x09  00001001 : model number 4 (100)
    M0110A(M923):           0x0B  00001011 : model number 5 (101)
    M0110AJ(M839):          0x0B  00001011 : model number 5 (101)
    M0110AJ(A615):          0x0B  00001011 : model number 5 (101)
    M0120(BCG9GRM0120):     0x11  00010001
    M0120 & M0110(G536):    0x13  00010011
    M0120 & M0110(G624):    0x19  00011001
    M0120 & M0110A(M923):   0x1B  00011011


Scan Code
---------
    m0110_recv_key() function returns following scan codes instead of M0110 raw codes.
    Scan codes are 1 byte size and MSB(bit7) is set when key is released.

        scancode = ((raw&0x80) | ((raw&0x7F)>>1))

    M0110                                                          M0120
    ,---------------------------------------------------------.    ,---------------.
    |  `|  1|  2|  3|  4|  5|  6|  7|  8|  9|  0|  -|  =|Backs|    |Clr|  -|Lft|Rgt|
    |---------------------------------------------------------|    |---------------|
    |Tab  |  Q|  W|  E|  R|  T|  Y|  U|  I|  O|  P|  [|  ]|  \|    |  7|  8|  9|Up |
    |---------------------------------------------------------|    |---------------|
    |CapsLo|  A|  S|  D|  F|  G|  H|  J|  K|  L|  ;|  '|Return|    |  4|  5|  6|Dn |
    |---------------------------------------------------------|    |---------------|
    |Shift   |  Z|  X|  C|  V|  B|  N|  M|  ,|  ,|  /|        |    |  1|  2|  3|   |
    `---------------------------------------------------------'    |-----------|Ent|
         |Opt|Mac |         Space               |Enter|Opt|        |      0|  .|   |
         `------------------------------------------------'        `---------------'
    ,---------------------------------------------------------.    ,---------------.
    | 32| 12| 13| 14| 15| 17| 16| 1A| 1C| 19| 1D| 1B| 18|   33|    | 47| 4E| 46| 42|
    |---------------------------------------------------------|    |---------------|
    |   30| 0C| 0D| 0E| 0F| 10| 11| 20| 22| 1F| 23| 21| 1E| 2A|    | 59| 5B| 5C| 4D|
    |---------------------------------------------------------|    |---------------|
    |    39| 00| 01| 02| 03| 05| 04| 26| 28| 25| 29| 27|    24|    | 56| 57| 58| 48|
    |---------------------------------------------------------|    |---------------|
    |      38| 06| 07| 08| 09| 0B| 2D| 2E| 2B| 2F| 2C|      38|    | 53| 54| 55|   |
    `---------------------------------------------------------'    |-----------| 4C|
         | 3A|  37|             31              |   34| 3A|        |     52| 41|   |
         `------------------------------------------------'        `---------------'

    International keyboard(See page 22 of "Technical Info for 128K/512K")
    ,---------------------------------------------------------.
    | 32| 12| 13| 14| 15| 17| 16| 1A| 1C| 19| 1D| 1B| 18|   33|
    |---------------------------------------------------------|
    |   30| 0C| 0D| 0E| 0F| 10| 11| 20| 22| 1F| 23| 21| 1E| 2A|
    |------------------------------------------------------   |
    |    39| 00| 01| 02| 03| 05| 04| 26| 28| 25| 29| 27| 24|  |
    |---------------------------------------------------------|
    |  38| 06| 07| 08| 09| 0B| 2D| 2E| 2B| 2F| 2C| 0A|      38|
    `---------------------------------------------------------'
         | 3A|  37|             34              |   31| 3A|
         `------------------------------------------------'

    M0110A
    ,---------------------------------------------------------. ,---------------.
    |  `|  1|  2|  3|  4|  5|  6|  7|  8|  9|  0|  -|  =|Bcksp| |Clr|  =|  /|  *|
    |---------------------------------------------------------| |---------------|
    |Tab  |  Q|  W|  E|  R|  T|  Y|  U|  I|  O|  P|  [|  ]|   | |  7|  8|  9|  -|
    |-----------------------------------------------------'   | |---------------|
    |CapsLo|  A|  S|  D|  F|  G|  H|  J|  K|  L|  ;|  '|Return| |  4|  5|  6|  +|
    |---------------------------------------------------------| |---------------|
    |Shift   |  Z|  X|  C|  V|  B|  N|  M|  ,|  ,|  /|Shft|Up | |  1|  2|  3|   |
    |---------------------------------------------------------' |-----------|Ent|
    |Optio|Mac    |           Space           |  \|Lft|Rgt|Dn | |      0|  .|   |
    `---------------------------------------------------------' `---------------'
    ,---------------------------------------------------------. ,---------------.
    | 32| 12| 13| 14| 15| 17| 16| 1A| 1C| 19| 1D| 1B| 18|   33| | 47| 68| 6D| 62|
    |---------------------------------------------------------| |---------------|
    |   30| 0C| 0D| 0E| 0F| 10| 11| 20| 22| 1F| 23| 21| 1E|   | | 59| 5B| 5C| 4E|
    |-----------------------------------------------------'   | |---------------|
    |    39| 00| 01| 02| 03| 05| 04| 26| 28| 25| 29| 27|    24| | 56| 57| 58| 66|
    |---------------------------------------------------------| |---------------|
    |      38| 06| 07| 08| 09| 0B| 2D| 2E| 2B| 2F| 2C|  38| 4D| | 53| 54| 55|   |
    |---------------------------------------------------------' |-----------| 4C|
    |   3A|     37|            31             | 2A| 46| 42| 48| |     52| 41|   |
    `---------------------------------------------------------' `---------------'


References
----------
Technical Info for 128K/512K and Plus
    ftp://ftp.apple.asimov.net/pub/apple_II/documentation/macintosh/Mac%20Hardware%20Info%20-%20Mac%20128K.pdf
    ftp://ftp.apple.asimov.net/pub/apple_II/documentation/macintosh/Mac%20Hardware%20Info%20-%20Mac%20Plus.pdf
Protocol:
    Page 20 of Tech Info for 128K/512K
    http://www.mac.linux-m68k.org/devel/plushw.php
Connector:
    Page 20 of Tech Info for 128K/512K
    http://www.kbdbabel.org/conn/kbd_connector_macplus.png
Signaling:
    http://www.kbdbabel.org/signaling/kbd_signaling_mac.png
    http://typematic.blog.shinobi.jp/Entry/14/
M0110 raw scan codes:
    Page 22 of Tech Info for 128K/512K
    Page 07 of Tech Info for Plus
    http://m0115.web.fc2.com/m0110.jpg
    http://m0115.web.fc2.com/m0110a.jpg
*/
