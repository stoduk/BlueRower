#include <assert.h>
#include <stdint.h>
#include <RTClib.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <SPI.h> // SD.h doesn't include this despite requiring it
#include <SD.h>  //including this alone increases RAM usage by 500 bytes!  Need to be careful with RAM
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library
#include <SPI.h>

/*
 * Amount of text visible on ST7735 graphical LCD (colxrow)
 * font size   portrait     landscape
 *   1           21 x 20      26 x 16 
 *   2           10 x 10      13 x 8
 *   3           7+ x 6        8 x 5
 */
/*
 ***********************
 *                     *
 ***********************
*/

// define if we have a hardware RTC connected, undefine otherwise
#define USING_HW_RTC

// LCD defines - these are hard coded (the address is whatever the I2C module is set to
// respond to, the pin numbers are fixed by the circuit on the I2C module)
#define I2C_ADDR       0x27
#define BACKLIGHT_PIN  3
#define En_pin         2
#define Rw_pin         1
#define Rs_pin         0
#define D4_pin         4
#define D5_pin         5
#define D6_pin         6
#define D7_pin         7

// TFT display and SD card will share the hardware SPI interface.
// Hardware SPI pins are specific to the Arduino board type and
// cannot be remapped to alternate pins.  For Arduino Uno,
// Duemilanove, etc., pin 11 = MOSI, pin 12 = MISO, pin 13 = SCK.
#define TFT_CS  10  // Chip select line for TFT display
#define TFT_RST  8  // Reset line for TFT (or see below...)
#define TFT_DC   9  // Data/command line for TFT

#define SD_CS    7  // Chip select line for SD card

//Use this reset pin for the shield!
//#define TFT_RST  0  // you can also connect this to the Arduino reset!

Adafruit_ST7735 lcd = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

/*
 * Print a number to specified serial buffer, with leading zero if required (to make two characters).
 */
#define PRINT_W_LEADING_ZERO(serial, val) { \
    if (val < 10) {                          \
         (serial).print('0');                \
    }                                        \
    (serial).print((val));                   \
}

/*
 * For received data, print the value in its raw form (eg. "[12]").
 * Unlikely to be used once tested, so make it possible to enable/disable
 */
#ifdef __PRINT_RAW_VALUES__
#define PRINT_RAW_VALUE(serial, val) {     \
    (serial).print('[');                   \
    (serial).print(val);                   \
    (serial).print(']');                   \
}
#define PRINT_RAW_VALUE_HEX(serial, val) { \
    (serial).print('[');                   \
    (serial).print('0');                   \
    (serial).print('x');                   \
    (serial).print(val, HEX);              \
    (serial).print(']');                   \
}
#else
#define PRINT_RAW_VALUE(serial, val)
#define PRINT_RAW_VALUE_HEX(serial, val)
#endif


// time of last serial data, used to display some message if we are receiving no data but aren't dead!
static long serial_time = 0;

// Possible parts of a message.  Some are just the opcode, some have one or two data values associated - each value (including opcode) is a single byte).
typedef enum {
    MSG_PART_OPCODE = 0,
    MSG_PART_DATA1,
    MSG_PART_DATA2,
    MSG_PART_DATA3,
    MSG_PART_DATA4,
    MSG_PART_DATA5,
    MSG_PART_DATA6,
    MSG_PART_DATA7,
    MSG_PART_DATA8,
    MSG_PART_DATA9,
    MSG_PART_DATA10,
    MSG_PART_DATA11,
    MSG_PART_DATA12,
    MSG_PART_DATA13,
    MSG_PART_DATA14,
    MSG_PART_DATA15,
} msg_part_type;

// Possible messages from Water Rower to PC, defined by the Water Rower spec.  
// See wr_msg_format for how many message parts are expected.
typedef enum {
    WR_OPCODE_INVALID = 0,
    WR_OPCODE_MIN = 0xFB,
    WR_OPCODE_HEART_RATE = WR_OPCODE_MIN,
    WR_OPCODE_POWER_STROKE,
    WR_OPCODE_MOTOR_VOLTAGE,
    WR_OPCODE_DISTANCE,
    WR_OPCODE_SPEED,
    WR_OPCODE_MAX = WR_OPCODE_SPEED,
} wr_opcode_type;

// Check whether a given opcode is valid
#define WR_OPCODE_VALID(opcode) ((opcode >= WR_OPCODE_MIN) && (opcode <= WR_OPCODE_MAX))

// Convert a given opcode to the index in to msg_format
#define WR_OPCODE_TO_INDEX(opcode) (opcode - WR_OPCODE_MIN)

// Data associated with each type of message (the index of this array to use for a given opcode being found from OPCODE_TO_INDEX() macro)
// Currently this could just be an array (it effectively is!), but there could be more data to associate later - eg. value validation or debug strings.
static struct {
    byte msg_part_count; // part counter is incremented after each part is stored (after this value is inspected);
                                 // when the part counter hits this value we have a complete message
} wr_msg_format[] = {
    // Heart Rate
    {MSG_PART_DATA1},
    // Power Stroke
    {MSG_PART_OPCODE},
    // Motor Voltage
    {MSG_PART_DATA2},
    // Distance
    {MSG_PART_DATA1},
    // Speed
    {MSG_PART_DATA2},
};

// Possible messages from PC to BlueRower, defined by me (the aim being most are human readable).
// See br_msg_format for how many message parts are expected.
// by not having these as sequential numbers this makes the conversion from opcode to index (and validation of values) more perverse.
// Can switch from human readable to less perverse once things are working
typedef enum {
    BR_OPCODE_INVALID = 0,
    BR_OPCODE_MIN = 0xFF, // fix once sequential BR_OPCODE_GET_TIME
    BR_OPCODE_GET_TIME = 't',
    BR_OPCODE_SET_TIME = 'T',
    BR_OPCODE_SET_YEAR = 'Y',
    BR_OPCODE_SET_MONTH = 'M',
    BR_OPCODE_SET_DAY = 'D',
    BR_OPCODE_SET_HOUR = 'h',
    BR_OPCODE_SET_MINUTE = 'm',
    BR_OPCODE_SET_SECOND = 's',
    BR_OPCODE_LIST_DIRS = 'l',
    BR_OPCODE_LIST_EVENTS = 'k',
    BR_OPCODE_START_FILE_LOG = '1',
    BR_OPCODE_STOP_FILE_LOG = 'q',
    BR_OPCODE_START_BT_LOG = '2',
    BR_OPCODE_STOP_BT_LOG = 'w',
    BR_OPCODE_MAX = 0, // fix once sequential BR_OPCODE_SET_SECOND
} br_opcode_type;

static bool
br_opcode_validator (int opcode, int *index_ptr)
{
    int index = -1;
    int valid = false;
    
    switch (opcode) {
    case BR_OPCODE_GET_TIME:
        index = 0;
        break; 
    case BR_OPCODE_SET_TIME:
        index = 1;
        break; 
    case BR_OPCODE_SET_YEAR:
        index = 2;
        break; 
    case BR_OPCODE_SET_MONTH:
        index = 3;
        break; 
    case BR_OPCODE_SET_DAY:
        index = 4;
        break; 
    case BR_OPCODE_SET_HOUR:
        index = 5;
        break; 
    case BR_OPCODE_SET_MINUTE:
        index = 6;
        break; 
    case BR_OPCODE_SET_SECOND:
        index = 7;
        break; 
    case BR_OPCODE_LIST_DIRS:
        index = 8;
        break;
    case BR_OPCODE_LIST_EVENTS:
        index = 9;
        break;
    case BR_OPCODE_START_FILE_LOG:
        index = 10;
        break;
    case BR_OPCODE_STOP_FILE_LOG:
        index = 11;
        break;
    case BR_OPCODE_START_BT_LOG:
        index = 12;
        break;
    case BR_OPCODE_STOP_BT_LOG:
        index = 13;
        break;
    default:
        break;
    }
        
    if (index != -1) {
        valid = true;
    }    
    
    if (index_ptr != NULL) {
        *index_ptr = index;
    }
    return (valid);
}

static int
br_opcode_to_index (int opcode)
{
    int index = -1;
    if (!br_opcode_validator(opcode, &index)) {
        assert(false);
    }
    return (index);
}
// Check whether a given opcode is valid
//#define BR_OPCODE_VALID(opcode) ((opcode >= WR_OPCODE_MIN) && (opcode <= WR_OPCODE_MAX))
#define BR_OPCODE_VALID(opcode) (br_opcode_validator(opcode, NULL))
// Convert a given opcode to the index in to msg_format
#define BR_OPCODE_TO_INDEX(opcode) (br_opcode_to_index(opcode))

// Data associated with each type of message (the index of this array to use for a given opcode being found from OPCODE_TO_INDEX() macro)
// Currently this could just be an array (it effectively is!), but there could be more data to associate later - eg. value validation or debug strings.
static struct {
    byte msg_part_count; // part counter is incremented after each part is stored (after this value is inspected);
                                 // when the part counter hits this value we have a complete message
} br_msg_format[] = {
    // Get Time
    {MSG_PART_OPCODE},
    // Set Time
    {MSG_PART_DATA15},
    // Set year
    {MSG_PART_DATA2},
    // Set month
    {MSG_PART_DATA2},
    // Set day
    {MSG_PART_DATA2},
    // Set hour
    {MSG_PART_DATA2},
    // Set minute
    {MSG_PART_DATA2},
    // Set second
    {MSG_PART_DATA2},
    // List directories
    {MSG_PART_OPCODE},
    // List events for given user (8 characters)
    {MSG_PART_DATA8},
    // Start logging to file
    {MSG_PART_OPCODE},
    // Stop logging to file
    {MSG_PART_OPCODE},
    // Start logging to bluetooth
    {MSG_PART_OPCODE},
    // Stop logging to bluetooth
    {MSG_PART_OPCODE},
};

// The next message part expected - obviously set to opcode initially, and after each complete message is read (whether valid or not).
static byte br_msg_part = MSG_PART_OPCODE; // C++ doesn't like (enum)++, so cheat :(
static byte wr_msg_part = MSG_PART_OPCODE; // C++ doesn't like (enum)++, so cheat :(

// Store values for a given message.  These aren't reset between messages out of laziness, so assume that each message handler only reads data that will always be received (all messages are fixed length so this is fine, modulo coding errors).
// xxx could make data an array too, but need to make sure we can't overrun..
static byte wr_opcode = WR_OPCODE_INVALID;
static byte wr_data1;
static byte wr_data2;

static byte br_opcode = BR_OPCODE_INVALID;
static byte br_data[MSG_PART_DATA15];

#ifdef USING_HW_RTC
static RTC_DS1307 rtc;
#else
static RTC_Millis rtc;
#endif

/*
 * SoftwareSerial connection to bluetooth module (would ideally be a hardware serial, but we need
 * the one we have for the connection to water rower - which is used for receiving, and of data we
 * can't lose, so having the hardware buffers is a Good Thing).
 */
static SoftwareSerial btSerial(6, 5); // RX, TX [does Rx need to be an external interrupt port?]

/*
 * If we are logging to file, a valid file object.
 * 
 * If we are logging to bluetooth, set to true.
 */
static File log_file;
static bool log_bt = false;

/*
 * Create a new log file - the file name is limited to 8.3 characters, and is of the format
 * YYMMDDxx.log, where xx is a sequential number limiting us to 100 logs per day (in reality
 * only one or two will be created, but if the RTC goes haywire then we can cope).
 */
static File
create_log_file (char *parent_dir)
{
    bool found_free = false;
    File newfile;
    char filename[22]; // 8 for parent dir, 1 for slash, 8.3 for rest, 1 for null
    char *fileptr = NULL;
    DateTime now;
    int val;
    int i;
    
    Serial.print(F("Looking for files in directory "));
    Serial.println(parent_dir);
    
    now = rtc.now();
    
    strncpy(filename, parent_dir, 8); // filename not null terminated yet!
    fileptr = filename + strlen(parent_dir);
    *fileptr++ = '/';
    
    for (i = 0; i < 3; i++) {
        switch (i) {
        case 0:
            // ditch first two characters of year
            val = now.year() - 2000;
            break;
        case 1:
            val = now.month();
            break;
        case 2:
            val = now.day();
            break;
        default:
            break;
        }
        
        if (val < 10) {
            *fileptr++ = '0';
        } else {
            *fileptr++ = (val / 10) + '0';
        }
        *fileptr++ = (val % 10) + '0';
    }

    // we'll overwrite +0/+1 until we find a unique number or give up
    // write the rest of the filename just once, here
    *(fileptr + 2) = '.';
    *(fileptr + 3) = 'l';
    *(fileptr + 4) = 'o';
    *(fileptr + 5) = 'g';
    *(fileptr + 6) = '\0';
    
    // in a loop try to create the file
    // obviously fileptr isn't updated here, as we keep rewriting it until a free file is found
    for (i = 0; !found_free && i < 100; i++) {
        if (i < 10) {
            *(fileptr + 0) = '0';
        } else {
            *(fileptr + 0) = (i / 10) + '0';
        }
        *(fileptr + 1) = (i % 10) + '0';

        Serial.print(F("Checking for file "));
        Serial.println(filename);
        if (!SD.exists(filename)) {
            found_free = true;
        }
    }
    
    if (found_free) {
        Serial.print(F("Found free file "));
        Serial.println(filename);
        
        newfile = SD.open(filename, FILE_WRITE);
    }
    
    return (newfile);
}

static void
display_time (void)
{
    DateTime now;
    
    now = rtc.now();
    lcd.setTextColor(ST7735_WHITE); 
    lcd.print(F("Time: "));
    if (now.hour() < 10) {
        lcd.print('0');
    }
    lcd.print(now.hour(), DEC);
    lcd.print(':');
    if (now.minute() < 10) {
        lcd.print('0');
    }
    lcd.print(now.minute(), DEC);
    lcd.print(':');
    if (now.second() < 10) {
        lcd.print('0');
    }
    lcd.print(now.second(), DEC);
    lcd.println("");
    //lcd.setCursor(0, 1);
    lcd.print(F("Date: "));
    lcd.print(now.day(), DEC);
    lcd.print('/');
    lcd.print(now.month(), DEC);
    lcd.print('/');
    lcd.print(now.year(), DEC);
    lcd.println("");
}

#define __FREE_MEMORY_TRACKING__
#ifdef __FREE_MEMORY_TRACKING__
// Stolen from internet
static int 
freeRam (void) {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

static void
display_free_ram (void)
{
    lcd.setTextColor(ST7735_WHITE); 
    lcd.print(F("Free RAM:"));
    lcd.println("");
    //lcd.setCursor(0, 1);
    lcd.println(freeRam());
    delay(1000);
}
#endif

void
setup (void)
{
    bool show_time = true; // ART: tmp set to true
    
    Serial.begin(1200);  // talk to water rower *and* computer (via USB) over hardware serial port
    btSerial.begin(9600);    // Software serial connection to Bluetooth module
                             // must match the BT's AT programming, but keep low
                             // else Arduino can't keep up and we'll get corruption

    lcd.initR(INITR_BLACKTAB);
    lcd.fillScreen(ST7735_BLACK);

    //lcd.begin (16,2); // 16 character x 2 line LCD
    //lcd.setBacklightPin(BACKLIGHT_PIN,POSITIVE);
    //lcd.setBacklight(HIGH);
    //lcd.setTextColor(ST7735_WHITE); 
#ifndef __TESTING__
    lcd.setRotation(0);
    lcd.setCursor(0, 0);
    lcd.setTextColor(ST7735_WHITE);
    
    lcd.setTextWrap(true);
    
    //lcd.println(F("BlueRower"));
    //lcd.println(F("..booting.."));
#else   
    for (int i = 1; i < 4; i++) {
        for (int j = 0; j < 2; j++) {
            lcd.fillScreen(ST7735_BLACK);
            lcd.setCursor(0, 0);
            lcd.setRotation(j);
            lcd.setTextSize(i);
            lcd.println(F("0123456789012345678901234567890123456789"));
            lcd.println(F("0123456789012345678901234567890123456789"));
            lcd.println(F("0123456789012345678901234567890123456789"));
            lcd.println(F("0123456789012345678901234567890123456789"));
            lcd.println(F("0123456789012345678901234567890123456789"));
            lcd.println(F("0123456789012345678901234567890123456789"));
            lcd.println(F("0123456789012345678901234567890123456789"));
            lcd.println(F("0123456789012345678901234567890123456789"));
            lcd.println(F("0123456789012345678901234567890123456789"));
            lcd.println(F("0123456789012345678901234567890123456789"));
            lcd.println(F("0123456789012345678901234567890123456789"));
            lcd.println(F("0123456789012345678901234567890123456789"));
            delay(10000);
        }
    }
    lcd.println(F("00000000000000"));
    lcd.println(F("11111111111111"));
    lcd.println(F("iiiiiiiiiiiiii"));
    lcd.println(F("MMMMMMMMMMMMMM"));

    delay(1000);
#endif    

    serial_time = millis();
    Serial.println(F("Booted.."));
    
    // Set CS for SD card to be pin 10 (even if I move this, the library requires pin 10 to be set to output, to force Arduino to be Master role                            
    pinMode(10, OUTPUT);

    if (!SD.begin(SD_CS)) {
            lcd.setTextColor(ST7735_WHITE);
        lcd.print(F("SD init failed"));
        delay(4000);
    }
  
#ifdef USING_HW_RTC
    /*
     * If we are using the hardware RTC, then not much
     * we can do if we can't initialise it - as the RTC
     * library stands it will return uninit values after
     * this which seems more hassle than it is worth
     * dealing with - times/dates could be anywhere!
     */
    Wire.begin();
    rtc.begin();

    if (!rtc.ispresent()) {
        Serial.println(F("RTC is NOT running!"));
        lcd.setTextColor(ST7735_WHITE); 
        lcd.print(F("RTC unresponsive"));
        delay(1000);
        assert(false);
    }
    
    if (!rtc.isrunning()) {
        Serial.println(F("RTC not running - set to compile time/date"));
        lcd.setTextColor(ST7735_WHITE); 
        lcd.print(F("RTC may be wrong"));
        delay(1000);
        
        rtc.adjust(DateTime(__DATE__, __TIME__));
        if (!rtc.isrunning()) {
            Serial.println(F("Failed to start RTC!"));
            lcd.setTextColor(ST7735_WHITE); 
            lcd.print(F("Failed to start RTC"));
            delay(1000);
            assert(false);
        }
    }

#else
    /*
     * No RTC, so use the software RTC library (good for 49 days run time!)
     * Use compile time to set the date, its the best we have and will be vaguely close to valid
     */
    rtc.begin(DateTime(__DATE__, __TIME__));
    show_time = true;
    
    // xxx warn user, and pause for confirmation?
#endif

    if (show_time) {
        display_time();
        // just pause until user dismisses?        
        delay(1000);
    }

    display_free_ram();
}

static void
sd_list_dirs (void)
{
    int counter = 0;
    File root;
    
    // xxx
    display_free_ram();
    root = SD.open("/");
    if (!root) {
        btSerial.println(F("Directory doesn't exist!"));
    }
    // xxx
    display_free_ram();
    while(true) {
        File entry =  root.openNextFile();
        if (!entry) {
            break;
        }
     
        if (entry.isDirectory() && strchr(entry.name(), '~') == NULL) {
            btSerial.println(entry.name());
            counter++;
        }
        entry.close();
   }
   
   root.close();
   
   btSerial.print(F("Number of entries found: "));
   btSerial.println(counter);
}

static void
sd_list_events (void)
{
    char dirname[9] = {};
    int counter = 0;
    File dir;
    File root;
    unsigned int i;

    /*
     * The name of the user is stored in DATA1-8 (with trailing spaces if needed)
     * List files in the directory with this name, after stripping the spaces.
     * No leading '/' needed for top level directory as this is implied.
     */
    for (i = 0; i < 8; i++) {
        if (br_data[i] == ' ') {
            dirname[i] = '\0';
            break;
        }
        dirname[i] = br_data[i];
    }
    dirname[8] = '\0';
    
    btSerial.print(F("Listing directory '"));
    btSerial.print(dirname);
    btSerial.println("'");
    
    dir = SD.open(dirname);
    if (!dir) {
      btSerial.println(F("Directory doesn't exist!"));
    }
    
    while (true) {
        File entry =  dir.openNextFile();
        if (!entry) {
            break;
        }
        btSerial.println(entry.name());
        if (!entry.isDirectory() && strchr(entry.name(), '~') == NULL) {
            btSerial.println(entry.name());
            counter++;
        }
        entry.close();
   }
   
   dir.close();
   
   btSerial.print(F("Number of entries found: "));
   btSerial.println(counter);
}

#ifdef __UNDEF_NEED_TO_PLAN__
static void
sd_replay_event (void)
{
    // argh, need both user/dir name and file name.  But what is file name! 
}
#endif

/*
 * Could this be a function?  My C++ foo isn't up to it - I'd need to pass
 * in an argument that behaves as either an SDFile or a SoftwareSerial - but
 * apparently you can't pass in Stream itself, as that is abstract.  WTF.
 */
#define WRITE_ISO_TIME(stream, time_now) {       \
    stream.print(now.year());                    \
    PRINT_W_LEADING_ZERO(stream, now.month())    \
    PRINT_W_LEADING_ZERO(stream, now.day());     \
    stream.print('T');                           \
    PRINT_W_LEADING_ZERO(stream, now.hour());    \
    PRINT_W_LEADING_ZERO(stream, now.minute());  \
    PRINT_W_LEADING_ZERO(stream, now.second());  \
    stream.println("");                          \
}

#define TIME_BYTE_TO_VAL(byte10s, byte1s) ((10 * ((byte10s) - '0')) + ((byte1s) - '0'))
static void
br_parse_complete_message (void)
{
    DateTime time;
    DateTime now = rtc.now();
    int year = now.year(), month = now.month(), day = now.day(), hour = now.hour(), minute = now.minute(), second = now.second();    
    bool update_time = true;

    switch (br_opcode) {
    case BR_OPCODE_GET_TIME:
        // PC requesting time (send in ISO 8601 compliant format YYYYMMDDThhmmss)
        WRITE_ISO_TIME(btSerial, now);
        update_time = false;
        break;
    case BR_OPCODE_SET_TIME:
        // br_data[0] and [1] are ignored - will be 20 in 2013, so ignore for now.  Y2k1 bug ftw.
        year   = TIME_BYTE_TO_VAL(br_data[2], br_data[3]);
        month  = TIME_BYTE_TO_VAL(br_data[4], br_data[5]);
        day    = TIME_BYTE_TO_VAL(br_data[6], br_data[7]);
        // br_data[8] will be 'T'
        hour   = TIME_BYTE_TO_VAL(br_data[9], br_data[10]);
        minute = TIME_BYTE_TO_VAL(br_data[11], br_data[12]);
        second = TIME_BYTE_TO_VAL(br_data[13], br_data[14]);
        
        break;
    case BR_OPCODE_SET_YEAR:
        year   = TIME_BYTE_TO_VAL(br_data[0], br_data[1]);
        
        break;
    case BR_OPCODE_SET_MONTH:
        month   = TIME_BYTE_TO_VAL(br_data[0], br_data[1]);
        
        break;
    case BR_OPCODE_SET_DAY:
        day   = TIME_BYTE_TO_VAL(br_data[0], br_data[1]);
        
        break;
    case BR_OPCODE_SET_HOUR:
        hour   = TIME_BYTE_TO_VAL(br_data[0], br_data[1]);
        
        break;
    case BR_OPCODE_SET_MINUTE:
        minute   = TIME_BYTE_TO_VAL(br_data[0], br_data[1]);
        
        break;
    case BR_OPCODE_SET_SECOND:
        second   = TIME_BYTE_TO_VAL(br_data[0], br_data[1]);
        
        break;
    case BR_OPCODE_LIST_DIRS:
        sd_list_dirs();
        update_time = false;
        break;
    case BR_OPCODE_LIST_EVENTS:
        sd_list_events();
        update_time = false;
        break;
    case BR_OPCODE_START_FILE_LOG:
        // xxx this hard codes me for now!
        log_file = create_log_file("Anthony"); // Can't trivially store in flash with F()
        if (log_file) {
            WRITE_ISO_TIME(log_file, now);
            btSerial.print(F("Logging to file started: "));
            btSerial.println(log_file.name());
            lcd.print(F("Logging to file started: "));
            lcd.println(log_file.name());
        } else {
            // xxx halt here?
            btSerial.println(F("Logging to file failed!"));
            lcd.println(F("Logging to file failed!"));
        }
        update_time = false;
        break;
    case BR_OPCODE_STOP_FILE_LOG:
        if (log_file) {
            log_file.close();
            btSerial.println(F("Logging to file stopped"));
        }
        update_time = false;
        break;
    case BR_OPCODE_START_BT_LOG:
        log_bt = true;
        btSerial.println(F("Logging to BT started"));
        lcd.println(F("Logging to BT started"));
        update_time = false;
        break;
    case BR_OPCODE_STOP_BT_LOG:
        log_bt = false;
        btSerial.println(F("Logging to BT stopped"));
        lcd.println(F("Logging to BT stopped"));
        update_time = false;
        break;
    default:
        // Either we've got garbage, or started from the middle of the message..
        // If its garbage we are screwed; if we started in the middle of a message then
        // discard this byte which is part of the previous message (we may loop through
        // a couple of times to clear the old message)
        update_time = false;
        Serial.print(F("Discarding unexpected opcode value "));
        PRINT_RAW_VALUE_HEX(Serial, br_opcode);
        Serial.println("");
        break;
    }  
    
    if (update_time) {
        btSerial.print(F("Updating time for "));
        btSerial.write(br_opcode);
        btSerial.print("\n");

        time = DateTime(year, month, day, hour, minute, second);
        rtc.adjust(time);
        display_time();
        //delay(4000); // xxx
    }
}

static void
br_update_handle (void)
{
    int val;
    
    if (btSerial.available()) {
        val = btSerial.read();
        
        if (val < 0 || val > 255) {
            btSerial.print(F("Urgh, unexpected message value!"));
            PRINT_RAW_VALUE_HEX(btSerial, val);
            btSerial.println("");

            delay(500);
            assert(false);
        }
        
        switch (br_msg_part) {
        case MSG_PART_OPCODE:
            br_opcode = val;
            break;
        case MSG_PART_DATA1:
        case MSG_PART_DATA2:
        case MSG_PART_DATA3:
        case MSG_PART_DATA4:
        case MSG_PART_DATA5:
        case MSG_PART_DATA6:
        case MSG_PART_DATA7:
        case MSG_PART_DATA8:
        case MSG_PART_DATA9:
        case MSG_PART_DATA10:
        case MSG_PART_DATA11:
        case MSG_PART_DATA12:
        case MSG_PART_DATA13:
        case MSG_PART_DATA14:
        case MSG_PART_DATA15:
            br_data[br_msg_part - 1] = val;
            break;
        default:
            btSerial.print(F("Urgh, unexpected message part, shouldn't be possible! "));
            PRINT_RAW_VALUE_HEX(btSerial, val);
            btSerial.println("");

            delay(500);
            assert(false);
            break;
        }
        
        if (BR_OPCODE_VALID(br_opcode)) {
            if (br_msg_part == br_msg_format[BR_OPCODE_TO_INDEX(br_opcode)].msg_part_count) {
                // message complete, so handle it!
                br_parse_complete_message();
                
                // reset part counter, ready for the next message
                br_msg_part = MSG_PART_OPCODE;
            } else {
                br_msg_part++;
            }
        } else {
            // unexpected opcode, so ditch this message
            // reset part counter and hope we find a valid message next time!
            br_msg_part = MSG_PART_OPCODE;
        }    
    }
}

static void
wr_parse_complete_message (void)
{
    switch (wr_opcode) {
    case WR_OPCODE_DISTANCE:
        Serial.print(F("Distance covered: "));
        Serial.print(wr_data1  / 10);
        Serial.print(wr_data1 % 10);
        PRINT_RAW_VALUE(Serial, wr_data1);
        Serial.println("");
        break;
    case WR_OPCODE_SPEED:
        Serial.print(F("Stroke rate: "));
        Serial.print(wr_data1);
        Serial.print(wr_data2 / 10);
        Serial.print(wr_data2 % 10);
        PRINT_RAW_VALUE(Serial, wr_data1);
        PRINT_RAW_VALUE(Serial, wr_data2);
        Serial.println("");
        break;
    case WR_OPCODE_MOTOR_VOLTAGE:
        // Only SIII monitor sends these, so don't waste RAM printing the details of a message we are unlikely to get!  May care if others use this..
        Serial.println(F("ERROR: unexpected motor voltage reading"));
        break;      
    case WR_OPCODE_POWER_STROKE:
        Serial.println(F("End of power stroke"));
        break;
    case WR_OPCODE_HEART_RATE:  
        Serial.print(F("Heart rate: "));
        Serial.print(wr_data1);
        Serial.println("");
        break;
    default:
        // Either we've got garbage, or started from the middle of the message..
        // If its garbage we are screwed; if we started in the middle of a message then
        // discard this byte which is part of the previous message (we may loop through
        // a couple of times to clear the old message)
        Serial.print(F("Discarding unexpected opcode value "));
        PRINT_RAW_VALUE_HEX(Serial, wr_opcode);
        Serial.println("");
        break;
    }  
}

void 
loop (void)
{
    int val;
    
    /*
     * A message is one, two or three bytes, with the first byte always being the opcode
     * For simplicity we only peek initially, we can then make sure we've got enough serial
     * data for whatever the message type is.
     */
    if (Serial.available()) {
        serial_time = millis();
        val = Serial.read();
        
        if (val < 0 || val > 255) {
            Serial.print(F("Urgh, unexpected message value!"));
            PRINT_RAW_VALUE_HEX(Serial, val);
            Serial.println("");

            delay(500);
            assert(false);
        }

        // send value to Bluetooth module, if its there and paired
        if (log_bt) {
            btSerial.write(val);
        }
        if (log_file) {
            log_file.print(millis());
            log_file.print(',');
            log_file.println(val);
        }
        
        switch (wr_msg_part) {
        case MSG_PART_OPCODE:
            wr_opcode = val;
            break;
        case MSG_PART_DATA1:
            wr_data1 = val;
            break;
        case MSG_PART_DATA2:
            wr_data2 = val;
            break;
        default:
            Serial.print(F("Urgh, unexpected message part, shouldn't be possible! "));
            PRINT_RAW_VALUE_HEX(Serial, val);
            Serial.println("");

            delay(500);
            assert(false);
            break;
        }
        
        if (WR_OPCODE_VALID(wr_opcode)) {
            if (wr_msg_part == wr_msg_format[WR_OPCODE_TO_INDEX(wr_opcode)].msg_part_count) {
                // message complete, so handle it!
                wr_parse_complete_message();
                
                // reset part counter, ready for the next message
                wr_msg_part = MSG_PART_OPCODE;
            } else {
                wr_msg_part++;
            }
        } else {
            // unexpected opcode, so ditch this message
            // reset part counter and hope we find a valid message next time!
            Serial.print(F("Invalid opcode found "));
            PRINT_RAW_VALUE_HEX(Serial, wr_opcode);
            Serial.println("");

            wr_msg_part = MSG_PART_OPCODE;
        }    
    }
  
    if (millis() - serial_time > 5000) {
        Serial.println(F("...sleeping..."));
        serial_time = millis();
    }
    
    if (btSerial.available()) {
        /*
         * Time/date updated over PC connection.  Move this to BT connection in due course
         * (as we can't guarantee the incoming data from water rower won't clash, without 
         * somehow guaranteeing it is disconncted, which would need to be a physical switch).
         */
        br_update_handle();
    }    
}
