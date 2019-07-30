/***************************************************
Adruino Play

MischuS

****************************************************/

// include SPI, MP3, Buttons and SDfat libraries
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_VS1053.h>
#include <SdFat.h>
#include <JC_Button.h>
#include <sdios.h>
#include <MD_MAX72xx.h>

#include <MFRC522.h>
//#include <EEPROM.h>

//#define HARDWARE_TYPE MD_MAX72XX::PAROLA_HW
//#define MAX_DEVICES 4

//#define CLK_PIN 13  // or SCK
//#define DATA_PIN 11 // or MOSI
//#define CS_PIN 2    // or SS
//MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// define the pins used for SPI Communication
#define CLK 13       // SPI Clock
#define MISO 12      // SPI master input data
#define MOSI 11      // SPI master output data
#define SHIELD_CS 7  // VS1053 chip select pin
#define SHIELD_DCS 6 // VS1053 Data/command select pin (output)
#define DREQ 3       // VS1053 Data request
#define CARDCS 4     // SD chip select pin
#define RST_PIN 9    // MFRC522 reset pin
#define SS_PIN 10    // MFRC522 chip select pin

// define reset pin
#define SHIELD_RESET -1 // VS1053 reset pin (unused!)

// define button PINs
#define yellowButton A4
#define blueButton A1
#define greenButton A2
#define redButton A3
#define whiteButton A0

// define communication settings
#define BAUDRATE 38400 // baudrate

// define behaviour of buttons
#define LONG_PRESS 1000

// define limits
#define MAX_VOLUME 20
#define MIN_VOLUME 100
#define VOLUME_STEPTIME 200

//custom type definitions
struct nfcTagData // struct to hold NFC tag data
{
  uint8_t cookie;   // byte 0 nfc cookie to identify the nfc tag to belong to the player
  char pname[28];   // byte 1-28 char array to hold the path to the folder
  uint8_t trackcnt; // byte 29 number of tracks in the specific folder
  uint8_t mode;     // byte 30play mode assigne to the nfcTag
  uint8_t special;  // byte 31 track or function for admin nfcTags
};

struct playDataInfo //
{
  uint8_t mode;         // play mode
  uint8_t pathLine;     // line number of the current play path in the index file
  uint8_t trackcnt;     // track count of
  uint8_t currentTrack; // current track
  char dirname[37];     // buffer to hold current dirname
  char fname[13];       // buffer to hold current file name
  char buffer[50];      // general buffer used throught the program
};

// function definition
void indexDirectoryToFile(File dir, File *indexFile);
static void nextTrack(uint16_t track);
int voiceMenu(playDataInfo *playData, int option);           // voice menu for setting up device
void resetCard();                                            // resets a card
int setupCard(nfcTagData *nfcData, playDataInfo *playData);  // first time setup of a card
bool readCard(nfcTagData *dataIn);                           // reads card content and save it in nfcTagObject
bool writeCard(nfcTagData *dataOut);                         // writes card content from nfcTagObject
void findPath(playDataInfo *playData);
void getTrackName(playDataInfo *playData);
void playFolder(playDataInfo *playData, uint8_t foldernum);
void playMenuOption(playDataInfo *playData, int option);
void startPlaying(playDataInfo *playData);
void playNext(playDataInfo *playData);
void playPrevious(playDataInfo *playData);

// helper function for development
void dump_byte_array(byte *dumpbuffer, byte dumpbufferSize); // dump a byte aray as hex to the Serial port

// instanciate global objects
// create instance of musicPlayer object
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(SHIELD_RESET, SHIELD_CS, SHIELD_DCS, DREQ, CARDCS);

// create instance of card reader
MFRC522 mfrc522(SS_PIN, RST_PIN); // create instance of MFRC522 object

// objects for SD handling
ifstream sdin; // input stream for searching in indexfile
SdFat SD;      // file system object

// Buttons
Button upButton(blueButton);
Button leftButton(greenButton);
Button middleButton(whiteButton);
Button rightButton(redButton);
Button downButton(yellowButton);

// global variables
uint8_t volume = 60;    // settings of amplifier
bool tagStatus = false; // tagStatus=true, tag is present, tagStatus=false no tag present

// NFC management
MFRC522::StatusCode status; // status code of MFRC522 operations
uint8_t pageAddr = 0x06;    // start using nfc tag starting from page 6
                            // ultraligth memory has 16 pages, 4 bytes per page
                            // pages 0 to 4 are for special functions

/* SETUP Everything */
void setup()
{
  /*------------------------
  setup communication
  ------------------------*/

  Serial.begin(BAUDRATE); // init serial communication
  while (!Serial)
    ; // wait until serial has started
  Serial.println(F("MischuToni Serial Communication Stated"));
  SPI.begin(); // start SPI communication
  Serial.println(F("SPI Communication started"));

  /*------------------------  
  setup nfc HW
  ------------------------*/

  mfrc522.PCD_Init();                // initialize card reader
  mfrc522.PCD_DumpVersionToSerial(); // print FW version of the reader

  /*------------------------
  setup player
  ------------------------*/

  musicPlayer.begin();                                 // setup music player
  Serial.println(F("VS1053 found"));                   // print music player info
  musicPlayer.setVolume(volume, volume);               // set volume for R and L chan, 0: loudest, 256: quietest
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT); // setup music player to use interupts

  /*------------------------
  setup SD card
  ------------------------*/
  Serial.print(F("Initializing SD card..."));
  if (!SD.begin(CARDCS))
  {
    Serial.println(F("SD failed, or not present"));
    while (1)
      ; // SD failed, program stopped
  }
  Serial.println(F("initialization done."));

  /*------------------------
  set pin mode of buttons
  ------------------------*/
  middleButton.begin();
  leftButton.begin();
  rightButton.begin();
  upButton.begin();
  downButton.begin();

  pinMode(yellowButton, INPUT_PULLUP);
  pinMode(blueButton, INPUT_PULLUP);
  pinMode(greenButton, INPUT_PULLUP);
  pinMode(whiteButton, INPUT_PULLUP);
  pinMode(redButton, INPUT_PULLUP);

  /*------------------------
  startup program
  ------------------------*/
  randomSeed(analogRead(A5)); // initialize random number geneartor

  if (leftButton.read() && middleButton.read() && rightButton.read())
  {
    Serial.println(F("Reset everything"));
  }
}

/* Main Loop of program
 *  
 */

void loop()
{
  /*------------------------
  init variables
  ------------------------*/
  static bool middleButtonLongPressDetect = false; // this is used to ignore release after long press, since middel button has two functions

  playDataInfo playData;
  nfcTagData dataIn;

  /*------------------------
  player status handling
  ------------------------*/
  if (!musicPlayer.playingMusic && tagStatus && !musicPlayer.paused())
  {
    if (playData.currentTrack < playData.trackcnt)
      // tag is present but no music is playing play next track if possible
      playNext(&playData);
  }

  /*------------------------
  buttons handling
  ------------------------*/
  // read out all button states
  middleButton.read();
  leftButton.read();
  rightButton.read();
  upButton.read();
  downButton.read();

  // up/down button handling for volume control
  if (upButton.wasReleased() || upButton.pressedFor(LONG_PRESS)) //increase volume
  {
    volume = volume - 5;
    if (volume <= MAX_VOLUME)
    {
      volume = MAX_VOLUME;
      Serial.println(F("maximum volume reached"));
    }
    musicPlayer.setVolume(volume, volume);
    Serial.println(volume);
    delay(VOLUME_STEPTIME); // delay the program execution not to step up volume too fast
  }
  if (downButton.wasReleased() || downButton.pressedFor(LONG_PRESS)) //decrease volume
  {
    volume = volume + 5;
    if (volume >= MIN_VOLUME)
    {
      volume = MIN_VOLUME;
      Serial.println(F("minimum volume reached"));
    }
    musicPlayer.setVolume(volume, volume);
    Serial.println(volume);
    delay(VOLUME_STEPTIME); // delay the program execution not to step up volume too fast
  }

  // left/right button handling for next track, previous track
  if (leftButton.wasReleased() && tagStatus) // short press left button goes back 1 track
  {
    Serial.println(F("previous track"));
    playPrevious(&playData);
  }
  // right button handling
  if (rightButton.wasReleased() && tagStatus) // short press of right button is next track
  {
    Serial.println(F("next track"));
    playNext(&playData);
  }

  // middle button handling long press for card setup, short press for play/pause
  if (middleButton.pressedFor(LONG_PRESS)) // long press allows to setup new card
  {
    Serial.println(F("Middle Button Long Press"));
    if (tagStatus == false) // no card present enter reset card menu
    {
      Serial.println(F("Reset nfc Tag ..."));
      if (!musicPlayer.startPlayingFile("/VOICE/0800_R~1.MP3"))
      {
        Serial.println(F("Failed to start playing Voice Menue"));
      }
      resetCard();
    }
    middleButtonLongPressDetect = true; // long press detected, thus set state to ignore button release
  }
  else if (middleButton.wasReleased() && tagStatus) // short press of  middle button is play/pause
  {
    Serial.println(F("Middle Button Release"));
    if (middleButtonLongPressDetect == true) // check whether it is a release after longPress
      middleButtonLongPressDetect = false;   // reset long press detect to initial state, since button is released
    else
    {
      Serial.println(F("Middle Button Short Press"));
      if (!musicPlayer.paused())
      {
        Serial.println(F("Paused"));
        musicPlayer.pausePlaying(true);
      }
      else
      {
        Serial.println(F("Resumed"));
        musicPlayer.pausePlaying(false);
      }
    }
  }

  /*------------------------
  serial IF handling
  ------------------------*/
  // i = index SD card for music folders and mp3 files
  // ' ' = PAUSE/PLAY
  // + = VOLUME up
  // - = VOLUME down
  // n = next track
  // p = previous track
  if (Serial.available())
  {
    char c = Serial.read();
    // if we get an 's' on the serial console, stop!
    if (c == 'i')
    {
      // index SD card //HAS TO BE MOVED TO AN EXTRA FUNCTION LATER ON
      if (SD.exists("/index.txt")) // remove existing index file from SD card
      {
        SD.remove("/index.txt");
      }
      File indexfile = SD.open("/index.txt", FILE_WRITE); // open new indexfile on SD card
      if (indexfile)
      {
        Serial.println(F("Index File opend, start indexing"));
      }
      else
      {
        Serial.println(F("error opening Index File"));
      }
      //File root = SD.open("/MUSIC");
      File root = SD.open("/");
      indexDirectoryToFile(root, &indexfile);
      indexfile.close();
      Serial.println(F("Indexed SD card"));
      root.close();
    }

    // if we get an ' ' on the serial console, pause/unpause!
    if (c == ' ')
    {
      if (!musicPlayer.paused())
      {
        Serial.println(F("Paused"));
        musicPlayer.pausePlaying(true);
      }
      else
      {
        Serial.println(F("Resumed"));
        musicPlayer.pausePlaying(false);
      }
    }

    // volume up with '+' on the serial console
    if (c == '+')
    {
      volume = volume - 5;
      musicPlayer.setVolume(volume, volume);
      Serial.println(volume);
    }

    // volume down with '-' on the serial console
    if (c == '-')
    {
      volume = volume + 5;
      musicPlayer.setVolume(volume, volume);
      Serial.println(volume);
    }

    // next track with 'n' on the serial console
    if (c == 'n')
    {
      playNext(&playData);
    }

    // previous track with 'p' on the serial console
    if (c == 'p')
    {
      playPrevious(&playData);
    }
  }

  /*------------------------
  NFC Tag handling
  ------------------------*/
  bool newTagStatus = false;
  for (uint8_t i = 0; i < 3; i++) //try to check tag status several times since eventhough tag is present it is not continiously seen
  {
    if (mfrc522.PICC_IsNewCardPresent())
    {
      newTagStatus = true;
      if (mfrc522.PICC_ReadCardSerial())
        ;
      break;
      Serial.println(F("Faild to read card"));
    }
  }
  if (newTagStatus != tagStatus) // nfc card status changed
  {
    tagStatus = newTagStatus;
    if (tagStatus) // nfc card added
    {
      int knownTag = 1;
      Serial.println(F("Card detected"));
      readCard(&dataIn);
      if (dataIn.cookie != 42)
      {
        // tag is not know, init card
        knownTag = 0;
        Serial.println(F("unknown card"));
        nfcTagData writeData;
        knownTag = setupCard(&writeData, &playData);
        readCard(&dataIn);
      }
      if (knownTag)
      {
        //tag is known copy data to playData struct
        Serial.println(F("known card"));
        strcpy(playData.dirname, "/MUSIC/");
        strcat(playData.dirname, dataIn.pname);
        playData.trackcnt = dataIn.trackcnt;
        playData.mode = dataIn.mode;
        playData.currentTrack = 1;
        // search linenumber of path in indexfile
        findPath(&playData);
        // get TrackName and fullfilepath from indexfile
        getTrackName(&playData);
        // start playing immediately
        startPlaying(&playData);
      }
    }
    else // nfc card removed
    {
      Serial.println(F("Card removed"));
      musicPlayer.stopPlaying();
    }
  }
}

/* support functions
==========================================================================================
*/

int voiceMenu(playDataInfo *playData, int option)
{
  int returnValue = 0;

  Serial.print("entering voice menu with option: ");
  Serial.println(option, DEC);

  musicPlayer.stopPlaying();
  switch (option) // TODO: replace by better filenaming and sprintf statement later on!!!!
    {
    case 1:
      if (!musicPlayer.startPlayingFile("/VOICE/0300_N~1.mp3"))
        Serial.println(F("Failed to start playing Option Text 1"));
      break;
    case 2:
      if (!musicPlayer.startPlayingFile("/VOICE/0310_T~1.mp3"))
        Serial.println(F("Failed to start playing Option Text 2"));
      break;
    case 3:
      if (!musicPlayer.startPlayingFile("/VOICE/0320_S~1.mp3"))
        Serial.println(F("Failed to start playing Option Text 3"));
      break;
    }
  
  do
  {
    middleButton.read();
    rightButton.read();
    leftButton.read();
    upButton.read();
    downButton.read();

    // exit voice menu by middle button
    if (middleButton.wasPressed())
    {
      Serial.println("middle button press");
      musicPlayer.stopPlaying();

      //read all buttons after a delay of 100ms in order to avoid actions triggered by buttons when leaving the menu
      /*delay(100);
      middleButton.read();
      rightButton.read();
      leftButton.read();
      upButton.read();
      downButton.read();*/

      if (returnValue != 0)
        return returnValue;
      delay(1000);
    }
    switch (option)
    {
    case 1: // folder select
      // browse folders by up/down buttons
      if (upButton.wasPressed())
      {
        returnValue += 1;
        Serial.print("folder index:\t");
        Serial.println(returnValue, DEC);
        playFolder(playData, returnValue);
      }
      if (downButton.wasPressed())
      {
        if (returnValue <= 1)
          returnValue = 1;
        else
          returnValue -= 1;
        playFolder(playData, returnValue);
      }
      // browse within a folder by left/right buttons
      if (rightButton.wasPressed())
      {
        playNext(playData);
      }
      if (leftButton.wasPressed())
      {
        playPrevious(playData);
      }
      break;

    case 2: // play mode select
      if (upButton.wasPressed())
      {
        returnValue += 1;
        if (returnValue >= 6)
          returnValue = 1;
        playMenuOption(playData, returnValue);
      }
      if (downButton.wasPressed())
      {
        returnValue -= 1;
        if (returnValue <= 0)
          returnValue = 5;
        playMenuOption(playData, returnValue);
      }
      break;

    case 3: // select track within folder
      if (upButton.wasPressed())
      {
        returnValue += 1;
        if (returnValue > playData->trackcnt)
          returnValue = 1;
        Serial.println(returnValue, DEC);
        //playMenuOption(playData, returnValue);
      }
      if (downButton.wasPressed())
      {
        returnValue -= 1;
        if (returnValue <= 0)
          returnValue = playData->trackcnt;
        Serial.println(returnValue, DEC);
        //playMenuOption(playData, returnValue);
      }
      break;
    }
  } while (true);
}

void resetCard()
{
  nfcTagData emptyData = {0, "", 0, 0, 0};
  do
  {
    leftButton.read();
    upButton.read();
    downButton.read();

    if (upButton.wasReleased() || downButton.wasReleased())
    {
      Serial.print(F("abort"));
      musicPlayer.stopPlaying();
      if (!musicPlayer.startPlayingFile("/VOICE/0802_R~1.MP3"))
      {
        Serial.println(F("Failed to start playing"));
      }
      return;
    }
  } while (!mfrc522.PICC_IsNewCardPresent());
  if (!mfrc522.PICC_ReadCardSerial())
    return; // wait until card is there, otherwise return to the beginning of the loop
  tagStatus = true;
  Serial.print(F("Reset Card!"));
  writeCard(&emptyData);
  musicPlayer.stopPlaying();
  if (!musicPlayer.startPlayingFile("/VOICE/0801_R~1.MP3"))
  {
    Serial.println(F("Failed to start playing"));
  }
}

int setupCard(nfcTagData *nfcData, playDataInfo *playData)
{
  // variables for function
  int returnValue = 0;
  int result = 0;

  // STEP 1 select playfolder by voiceMenu
  result = voiceMenu(playData, 1);
  if (result > 0) // copy selected folder to nfcData struct
  {
    strncpy(nfcData->pname, playData->dirname + 7, 28);

    nfcData->trackcnt = playData->trackcnt;

    musicPlayer.stopPlaying();
    if (!musicPlayer.startPlayingFile("/VOICE/0310_T~1.mp3"))
    {
      Serial.println(F("Failed to start playing"));
    }
  }
  else // play error message and exit card setup
  {
    musicPlayer.stopPlaying();
    if (!musicPlayer.startPlayingFile("/VOICE/0401_E~1.mp3"))
    {
      Serial.println(F("Failed to start playing"));
    }
    // read buttons before leaving in order to avoid button event when reentering main loop
    leftButton.read();
    middleButton.read();
    rightButton.read();
    upButton.read();
    downButton.read();

    return returnValue;
  }

  // STEP 2 select play mode by voiceMenu
  result = voiceMenu(playData, 2);
  if (result > 0)
    nfcData->mode = result;
  /*if (result == 4)
    {// if play mode is "single track" (i.e. 4) track to be played has to be selected
      voiceMenu(playData, 3);
    }
  else*/
  nfcData->mode = 1;
  nfcData->special = 1;
  nfcData->cookie = 42;

  Serial.println(F("NFC Data:"));
  /*Serial.print(F("cookie:\t"));
  Serial.println(nfcData->cookie);
  Serial.print(F("folder:\t"));
  Serial.println(nfcData->pname);
  Serial.print(F("tracknumber:\t"));
  Serial.println(nfcData->trackcnt);
  Serial.print(F("playmode:\t"));
  Serial.println(nfcData->mode);
  Serial.print(F("function:\t"));
  Serial.println(nfcData->special);*/
  // write data for card

  writeCard(nfcData);

  /*
  // Hörbuchmodus -> Fortschritt im EEPROM auf 1 setzen
  EEPROM.write(myCard.folder,1);

  // Einzelmodus -> Datei abfragen
  if (myCard.mode == 4)
    myCard.special = voiceMenu(mp3.getFolderTrackCount(myCard.folder), 320, 0,
                               true, myCard.folder);

  // Admin Funktionen
  if (myCard.mode == 6)
    myCard.special = voiceMenu(3, 320, 320);
;*/

  // read buttons before leaving in order to avoid button event when reentering main loop
  delay(100);

  leftButton.read();
  middleButton.read();
  rightButton.read();
  upButton.read();
  downButton.read();
  returnValue = 1;
  return returnValue;
}

bool readCard(nfcTagData *dataIn)
{
  bool returnValue = true;

  // init read buffer
  byte byteCount = 18; // ultralight cards are always read in chunks of 16byte + 2byte CRC
  byte readBuffer[18];

  // read first 18 byte from card ()
  status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(pageAddr, readBuffer, &byteCount);
  if (status != MFRC522::STATUS_OK)
  {
    Serial.print(F("MIFARE_Read() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    returnValue = false;
    return returnValue;
  }

  dataIn->cookie = readBuffer[0];
  for (uint8_t i = 0; i < 15; i++)
  {
    dataIn->pname[i] = (char)readBuffer[i + 1];
  }

  //read next 18bit
  status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(pageAddr + 4, readBuffer, &byteCount);
  if (status != MFRC522::STATUS_OK)
  {
    Serial.print(F("MIFARE_Read() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    returnValue = false;
    return returnValue;
  }
  for (uint8_t i = 0; i < 13; i++)
  {
    dataIn->pname[i + 15] = (char)readBuffer[i];
  }
  dataIn->trackcnt = (uint8_t)readBuffer[13];
  dataIn->mode = (uint8_t)readBuffer[14];
  dataIn->special = (uint8_t)readBuffer[15];

  return returnValue;
}

bool writeCard(nfcTagData *dataOut)
{
  bool returnValue = true;
  byte writeBuffer[32];

  memcpy(writeBuffer, (const unsigned char *)dataOut, 32); //it might be unnecessary to copy it to write buffer ... CHECK

  //32 byte data is writen in 8 blocks of 4 bytes (4 bytes per page)
  for (uint8_t i = 0; i < 8; i++)
  {
    status = (MFRC522::StatusCode)mfrc522.MIFARE_Ultralight_Write(pageAddr + i, &writeBuffer[i * 4], 4);
    if (status != MFRC522::STATUS_OK)
    {
      Serial.print(F("MIFARE_Write() failed: "));
      Serial.println(mfrc522.GetStatusCodeName(status));
      returnValue = false;
      return returnValue;
    }
  }
  Serial.println(F("MIFARE_Ultralight_Write() OK "));
  return returnValue;
}

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
void dump_byte_array(byte *dumpbuffer, byte dumpbufferSize)
{
  for (byte i = 0; i < dumpbufferSize; i++)
  {
    Serial.print(dumpbuffer[i] < 0x10 ? " 0" : " ");
    Serial.print(dumpbuffer[i], HEX);
  }
}

/*---------------------------------
track handling routines MOVE to extra file later on
---------------------------------*/

// find line in index file for given path (from NFC Tag)
void findPath(playDataInfo *playData)
{
  uint8_t line_number = 0;

  sdin.open("/index.txt"); // open indexfile
  sdin.seekg(0);           // go to file position 0

  while (true)
  {
    if (!sdin.getline(playData->buffer, 50, '\n'))
      break;
    ++line_number;
    if (strstr(playData->buffer, playData->dirname))
    {
      break;
    }
  }
  playData->pathLine = line_number;
  sdin.close();
}

// read trackname from indexfile corresponding to a tracknumber (currentTrack)
void getTrackName(playDataInfo *playData)
{
  sdin.open("/index.txt");
  sdin.seekg(0);

  uint8_t line_number = playData->pathLine - playData->trackcnt + playData->currentTrack - 1;
  for (uint8_t i = 1; i < line_number; i++)
  {
    sdin.ignore(50, '\n');
  }
  sdin.getline(playData->fname, 13, '\n');
  sdin.close();
}

// play next track
void playNext(playDataInfo *playData)
{
  musicPlayer.stopPlaying(); //stop playing first, since SD library is unable to access two files at the time

  playData->currentTrack = playData->currentTrack + 1;
  if (playData->currentTrack > playData->trackcnt)
  {
    playData->currentTrack = playData->trackcnt;
  }
  getTrackName(playData);
  startPlaying(playData); // restart playing
}

// play previous track
void playPrevious(playDataInfo *playData)
{
  musicPlayer.stopPlaying(); //stop playing first, since SD library is unable to access two files at the time
  playData->currentTrack = playData->currentTrack - 1;
  if (playData->currentTrack < 1)
  {
    playData->currentTrack = 1;
  }
  getTrackName(playData);
  startPlaying(playData);
}

void playRandom()
{
}

// playFolder looks-up a filename in the indexfile corresponding to a folder number and starts playing the file
void playFolder(playDataInfo *playData, uint8_t foldernum)
{
  //stop playing before trying to access SD-Card, otherwhise SPI bus is too busy
  musicPlayer.stopPlaying();

  char *pch;
  sdin.open("/index.txt");
  sdin.seekg(0);
  uint8_t i = 1;
  uint8_t line_number = 0;

  while (i <= foldernum)
  {
    if (!sdin.getline(playData->buffer, 50, '\n'))
    {
      break;
    }
    pch = strpbrk(playData->buffer, "/"); // locate "/" in order to filter out lines in index file containing the path
    if (pch != NULL)
    {
      i = i + 1;
    }
    ++line_number;
  }
  sdin.close();

  playData->pathLine = line_number;
  pch = strtok(playData->buffer, "\t");
  strcpy(playData->dirname, pch);
  pch = strtok(NULL, "\t");
  playData->trackcnt = atoi(pch); //convert trackcount to int
  playData->currentTrack = 1;
  getTrackName(playData);
  startPlaying(playData);

  return;
}

void playMenuOption(playDataInfo *playData, int option)
{
  musicPlayer.stopPlaying();
  strcpy(playData->dirname, "/VOICE");
  switch (option)
  {
  case 1:
    strcpy(playData->fname, "0311_M~1.mp3");
    break;
  case 2:
    strcpy(playData->fname, "0312_M~1.mp3");
    break;
  case 3:
    strcpy(playData->fname, "0313_M~1.mp3");
    break;
  case 4:
    strcpy(playData->fname, "0314_M~1.mp3");
    break;
  case 5:
    strcpy(playData->fname, "0315_M~1.mp3");
    break;
  }
  startPlaying(playData);
}

// start playing track
void startPlaying(playDataInfo *playData)
{
  strcpy(playData->buffer, playData->dirname);
  strcat(playData->buffer, "/");
  strcat(playData->buffer, playData->fname);

  Serial.println(F("Start Playing Next Track"));

  /*Serial.print(F("pathline:\t"));
  Serial.println(playData->pathLine, DEC);
  Serial.print(F("dirname:\t"));
  Serial.println(playData->dirname);
  Serial.print(F("trackcnount:\t"));
  Serial.println(playData->trackcnt);
  Serial.print(F("current track:\t"));
  Serial.println(playData->currentTrack);
  Serial.print(F("filename:\t"));
  Serial.println(playData->fname);*/
  Serial.print(F("filepathname:\t"));
  Serial.println(playData->buffer);

  if (!musicPlayer.startPlayingFile(playData->buffer))
  {
    Serial.println(F("Failed to start playing in startPlaying function"));
  }
}

/*---------------------------------
routine to index SD file structure
---------------------------------*/
void indexDirectoryToFile(File dir, File *indexFile)
{
  char fname[13];
  //static char dirname[50] = "/MUSIC";
  static char dirname[50] = "/";

  // Begin at the start of the directory
  dir.rewindDirectory();
  uint8_t trackcnt = 0;
  while (true)
  {
    Serial.print(F("."));
    File entry = dir.openNextFile();
    if (!entry)
    {
      // no more files or folder in this directory
      // print current directory name if there are MP3 tracks in the directory
      if (trackcnt != 0)
      {
        indexFile->write(dirname + 1); // avoid printing / at the "begining"
        indexFile->write('\t');
        indexFile->print(trackcnt);
        indexFile->write('\r');
        indexFile->write('\n');
        trackcnt = 0;
      }
      // roll back directory name
      char *pIndex;
      pIndex = strrchr(dirname, '/'); // search last occurence of '/'
      if (pIndex == NULL)
      {
        break; // no more '/' in the dirname
      }
      uint8_t index = pIndex - dirname; // calculate index position from pointer addresses
      if (index == 0)
      {
        break; // don't roll back any further, reached root
      }
      dirname[index] = '\0'; // null terminate at position of '/'
      break;
    }
    // there is an entry
    entry.getSFN(fname);
    // recurse for directories, otherwise print the file
    if (entry.isDirectory())
    {
      //entry is a directory
      //update dirname and reset trackcnt
      strcat(dirname, "/");
      strcat(dirname, fname);
      trackcnt = 0;
      indexDirectoryToFile(entry, indexFile);
    }
    else
    {
      char *pch;
      pch = strstr(fname, ".MP3");
      if (pch == NULL)
        pch = strstr(fname, ".mp3");
      if (pch)
      {
        indexFile->write(fname);
        indexFile->write('\r');
        indexFile->write('\n');
        trackcnt += 1;
      }
    }
    entry.close();
  }
}