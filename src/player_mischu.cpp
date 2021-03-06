/***************************************************
Adruino Play

MischuS

****************************************************/

//TODO: Play Buffer
//TODO: Relative Lautstärke auf TAG

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

#include "user_fonts.h" // add user defined fonts for 

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES1 1
#define MAX_DEVICES2 3
#define CLK_PIN 26  
#define DATA_PIN 22 
#define CS_PIN1 24
#define CS_PIN2 28

// Font parameters
#define CHAR_SPACING 1 // pixels between characters
MD_MAX72XX::fontType_t *pFontNormal = fontSmallNormal; // normal font
MD_MAX72XX::fontType_t *pFontCondensed = fontSmallCondensed; // condensed font
MD_MAX72XX::fontType_t *pFontWide = fontSmallWide; // wide font

// Global message buffers shared by Serial and Scrolling functions
#define BUF_SIZE 75
char message[BUF_SIZE] = " ";
bool newMessageAvailable = true;

// define the pins used for SPI Communication
#define CLK 52       // SPI Clock on MEGA / 13 on UNO
#define MISO 50      // SPI master input data MEGA /12 on UNO
#define MOSI 51      // SPI master output data MEGA /11 on UNO
#define SHIELD_CS 7  // VS1053 chip select pin 
#define SHIELD_DCS 6 // VS1053 Data/command select pin (output)
#define DREQ 3       // VS1053 Data request
#define CARDCS 4     // SD chip select pin
#define RST_PIN 9    // MFRC522 reset pin
#define SS_PIN 10    // MFRC522 chip select pin

// define reset pin
#define SHIELD_RESET -1 // VS1053 reset pin (unused!)

// define button PINs
#define yellowButton A5
#define blueButton A2
#define greenButton A3
#define redButton A4
#define whiteButton A1

// define communication settings
#define BAUDRATE 38400 // baudrate

// define behaviour of buttons
#define LONG_PRESS 1000

// define volume behavior and limits
#define MAX_VOLUME 25
#define MIN_VOLUME 100
#define VOLUME_STEPTIME 200

//custom type definitions
struct nfcTagData // struct to hold NFC tag data
{
  uint8_t cookie;   // byte 0 nfc cookie to identify the nfc tag to belong to the player
  char pname[28];   // byte 1-28 char array to hold the path to the folder
  uint8_t trackCnt; // byte 29 number of tracks in the specific folder
  uint8_t mode;     // byte 30play mode assigne to the nfcTag
  uint8_t special;  // byte 31 track or function for admin nfcTags
};

struct playDataInfo //
{
  uint8_t mode;          // play mode
  uint16_t pathLine;      // line number of the current play path in the index file
  uint8_t trackCnt;      // track count of
  uint8_t currentTrack;  // current track
  char dirName[37];      // buffer to hold current dirName
  uint8_t trackList[32]; // list of the tracks with their order to play
};

// function definition
void indexDirectoryToFile(File dir, File *indexFile);
static void nextTrack(uint16_t track);
int voiceMenu(playDataInfo *playData, int option);          // voice menu for setting up device
void resetCard();                                           // resets a card
int setupCard(nfcTagData *nfcData, playDataInfo *playData); // first time setup of a card
bool readCard(nfcTagData *dataIn);                          // reads card content and save it in nfcTagObject
bool writeCard(nfcTagData *dataOut);                        // writes card content from nfcTagObject
void findPath(playDataInfo *playData);
void playFolder(playDataInfo *playData, uint8_t foldernum);
void playMenuOption(playDataInfo *playData, int option);
void startPlaying(playDataInfo *playData);   // start playing selected track
void selectNext(playDataInfo *playData);     // selects next track
void selectPrevious(playDataInfo *playData); // selects previous track
void printerror(int errorcode, int source);
void printText(uint8_t modStart, uint8_t modEnd, char *pMsg);

// helper function for development
void dump_byte_array(byte *dumpbuffer, byte dumpbufferSize); // dump a byte aray as hex to the Serial port

// instanciate global objects
// create instance of musicPlayer object
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(SHIELD_RESET, SHIELD_CS, SHIELD_DCS, DREQ, CARDCS);

// create instance of card reader
MFRC522 mfrc522(SS_PIN, RST_PIN); // create instance of MFRC522 object

// create display instance
MD_MAX72XX mx1 = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN1, MAX_DEVICES1);
MD_MAX72XX mx2 = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN2, MAX_DEVICES2);

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

/* SETUP */
void setup()
{
  /*------------------------
  setup communication
  ------------------------*/

  Serial.begin(BAUDRATE); // init serial communication
  while (!Serial); // wait until serial has started
  Serial.println(F("Startup"));
  SPI.begin(); // start SPI communication

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
    Serial.println(F("SD error"));
    while (1)
      ; // SD failed, program stopped
  }
  Serial.println(F("init ok"));

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
  set pin mode of GPIOS where SPI is connected to to input in order not to distrub SPI communication (for UNO compatibility)
  ------------------------*/
  pinMode(11, INPUT);
  pinMode(12, INPUT);
  pinMode(13, INPUT);

  /*------------------------
  setup display
  ------------------------*/
  mx1.begin();
  mx2.begin();
  mx1.control(MD_MAX72XX::INTENSITY,MAX_INTENSITY/20);
  mx1.setFont(pFontNormal);
  mx1.transform(MD_MAX72XX::TFLR);
  printText(0, MAX_DEVICES1 - 1, message);
  newMessageAvailable = false;
  /*for (uint8_t i = 0; i < ROW_SIZE; i++)
  {
    mx1.setPoint(i, i, true);
    mx1.setPoint(0, i, true);
    mx1.setPoint(i, 0, true);
    delay(100);
  }*/
  Serial.println(F("Displayed Message"));

  /*------------------------
  startup program
  ------------------------*/
  randomSeed(analogRead(A5)); // initialize random number geneartor
  
  // index SD card if left, middle and right button are pressed during startup
  if (leftButton.read() && middleButton.read() && rightButton.read())
  {
    
    Serial.println(F("Index Card;"));

    if (musicPlayer.playingMusic) // stop music player in order to avoid parallel access to SD card
      musicPlayer.stopPlaying();

    if (SD.exists("/index.txt")) // remove existing index file from SD card
    {
      SD.remove("/index.txt");
      Serial.println(F("removed old index"));
    }
    File indexfile = SD.open("/index.txt", FILE_WRITE); // open new indexfile on SD card
    Serial.println(F("created new empty file"));
    if (indexfile)
    {
      Serial.println(F("start indexing"));
    }
    else
    {
      Serial.println(F("error indexing"));
    }
    File root = SD.open("/");
    indexDirectoryToFile(root, &indexfile);
    indexfile.close();
    Serial.println(F("index ok"));
    root.close();
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
  static bool middleButtonLongPressDetect = false; // state variable allowing to ignore release after long press
  playDataInfo playData;
  nfcTagData dataIn;

  /*------------------------
  player status handling
  ------------------------*/
  if (!musicPlayer.playingMusic && tagStatus && !musicPlayer.paused())
  {
    if (playData.currentTrack < playData.trackCnt)
    {
      // tag is present but no music is playing play next track if possible
      selectNext(&playData);
      startPlaying(&playData);
    }
    else
    {
      sprintf(message, " ");
      printText(0, MAX_DEVICES1 - 1, message);
    }
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
      Serial.println(F("maximum vol"));
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
      Serial.println(F("min vol"));
    }
    musicPlayer.setVolume(volume, volume);
    Serial.println(volume);
    delay(VOLUME_STEPTIME); // delay the program execution not to step up volume too fast
  }

  // left/right button handling for next track, previous track
  if (leftButton.wasReleased() && tagStatus) // short press left button goes back 1 track
  {
    Serial.println(F("previous"));
    selectPrevious(&playData);
    startPlaying(&playData);
  }
  // right button handling
  if (rightButton.wasReleased() && tagStatus) // short press of right button is next track
  {
    Serial.println(F("next"));
    selectNext(&playData);
    startPlaying(&playData);
  }

  // middle button handling long press for card setup, short press for play/pause
  if (middleButton.pressedFor(LONG_PRESS)) // long press allows to setup new card
  {
    Serial.println(F("M Long"));
    if (tagStatus == false) // no card present enter reset card menu
    {
      Serial.println(F("Reset Tag"));
      if (!musicPlayer.startPlayingFile("/VOICE/0800_R~1.MP3"))
        printerror(201, 1);
      resetCard();
    }
    middleButtonLongPressDetect = true; // long press detected, thus set state to ignore button release
  }
  else if (middleButton.wasReleased() && tagStatus) // short press of  middle button is play/pause
  {
    Serial.println(F("M release"));
    if (middleButtonLongPressDetect == true) // check whether it is a release after longPress
      middleButtonLongPressDetect = false;   // reset long press detect to initial state, since button is released
    else
    {
      Serial.println(F("M short"));
      if (!musicPlayer.paused())
      {
        sprintf(message,"=");
        printText(0, MAX_DEVICES1 - 1, message);
        Serial.println(F("pause"));
        musicPlayer.pausePlaying(true);
      }
      else
      {
        sprintf(message, "%d", playData.currentTrack);
        printText(0, MAX_DEVICES1 - 1, message);
        Serial.println(F("resume"));
        musicPlayer.pausePlaying(false);
      }
    }
    if (middleButton.wasReleased())
    {
    }
  }

  /*------------------------
  serial IF handling
  ------------------------*/
  
  if (Serial.available())
  {
    char c = Serial.read();

    // print debug information when received a d on console
    if (c == 'd')
    {
      // print current trackList for debug purposes
      for (size_t i = 0; i < playData.trackCnt; i++)
        Serial.println(playData.trackList[i]);
      Serial.println(playData.currentTrack);
    }
  }

  /*------------------------
  NFC Tag handling
  ------------------------*/
  bool newTagStatus = false; // state variable to check tag status
  
  for (uint8_t i = 0; i < 3; i++) //try to check tag status several times since eventhough tag is present it is not continiously seen
  {
    if (mfrc522.PICC_IsNewCardPresent())
    {
      newTagStatus = true;
      if (mfrc522.PICC_ReadCardSerial())
        ;
      break;
      Serial.println(F("error reading tag"));
    }
  }
  if (newTagStatus != tagStatus) // nfc card status changed
  {
    tagStatus = newTagStatus;
    if (tagStatus) // nfc card added
    {
      int knownTag = 1;
      Serial.println(F("tag detected"));
      readCard(&dataIn);
      if (dataIn.cookie != 42)
      {
        // tag is not configured, init card
        knownTag = 0;
        Serial.println(F("unknown card"));
        nfcTagData writeData;
        knownTag = setupCard(&writeData, &playData);
        readCard(&dataIn);
      }
      if (knownTag)
      {
        //tag is known setup playData struct
        
        
        Serial.println(F("added known card"));
        strcpy(playData.dirName, "/MUSIC/");
        strcat(playData.dirName, dataIn.pname);
        playData.trackCnt = dataIn.trackCnt;
        playData.mode = dataIn.mode;
        
        //init empty tracklist
        for (size_t i = 0; i < 32; i++)
          playData.trackList[i] = 0;
        switch (playData.mode)
        {
        case 3: //random -> shuffle n elements trackList
          Serial.println(F("random"));
          for (size_t i = 0; i < playData.trackCnt; i++) // generate trackList 1..trackCnt
            playData.trackList[i] = i + 1;
          for (size_t i = 0; i < playData.trackCnt; i++) // shuffle trackList
          {
            size_t j = random(0, playData.trackCnt);       // select one entry [j] to exchange with entry at index [i]
            uint8_t track = playData.trackList[j];         // save track at position [j]
            playData.trackList[j] = playData.trackList[i]; // copy value from [i] to [j]
            playData.trackList[i] = track;                 // copy saved value to postition [i]
          }
          break;
        case 4: // single track
          Serial.println(F("single"));
          playData.trackList[0] = dataIn.special;
          break;
        default:
          Serial.print(F("other Mode: "));
          Serial.println(playData.mode);
          for (size_t i = 0; i < playData.trackCnt; i++)
            playData.trackList[i] = i + 1;
          break;
        }
        playData.currentTrack = 1;
        findPath(&playData);
        // get TrackName and fullfilepath from indexfile
        // getTrackName(&playData);
        // start playing immediately
        startPlaying(&playData);
      }
    }
    else // nfc card removed
    {
      Serial.println(F("tag removed"));
      if (musicPlayer.playingMusic)
      {  
        musicPlayer.stopPlaying();
        sprintf(message, " ");
        printText(0, MAX_DEVICES1, message);
      }
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
  if (musicPlayer.playingMusic)
    musicPlayer.stopPlaying();
  switch (option) // TODO: replace by better filenaming and sprintf statement later on!!!!
  {
  case 1:
    if (!musicPlayer.startPlayingFile("/VOICE/0300_N~1.mp3"))
      printerror(201, 1);
    break;
  case 2:
    if (!musicPlayer.startPlayingFile("/VOICE/0310_T~1.mp3"))
      printerror(201, 1);
    break;
  case 3:
    if (!musicPlayer.startPlayingFile("/VOICE/0320_S~1.mp3"))
      printerror(201, 1);
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
      if (musicPlayer.playingMusic)
        musicPlayer.stopPlaying();
      return returnValue;
    }
    
    switch (option)
    {
    case 1: // folder select
      // browse folders by up/down buttons
      if (upButton.wasPressed())
      {
        returnValue += 1;
        Serial.print("index:\t");
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
        selectNext(playData);
        startPlaying(playData);
      }
      if (leftButton.wasPressed())
      {
        selectPrevious(playData);
        startPlaying(playData);
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
        if (musicPlayer.playingMusic)
          musicPlayer.stopPlaying();
        if (returnValue == 0)
        {
          returnValue = 1;
          startPlaying(playData);
        }
        else
        {
          selectNext(playData);
          startPlaying(playData);
          returnValue = playData->currentTrack;
          Serial.println(returnValue);
        }
      }
      if (downButton.wasPressed())
      {
        if (returnValue == 0)
        {
          returnValue = 1;
          startPlaying(playData);
        }
        else
        {
          selectPrevious(playData);
          startPlaying(playData);
          returnValue = playData->currentTrack;
          Serial.println(returnValue);
        }
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
      if (musicPlayer.playingMusic)
        musicPlayer.stopPlaying();
      if (!musicPlayer.startPlayingFile("/VOICE/0802_R~1.MP3"))
        printerror(201,0); 
      return;
    }
  } while (!mfrc522.PICC_IsNewCardPresent());
  if (!mfrc522.PICC_ReadCardSerial())
    return; // wait until card is there, otherwise return to the beginning of the loop
  tagStatus = true;
  Serial.print(F("Reset Card!"));
  writeCard(&emptyData);
  if (musicPlayer.playingMusic)
    musicPlayer.stopPlaying();
  if (!musicPlayer.startPlayingFile("/VOICE/0801_R~1.MP3"))
    printerror(201,0);
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
    strncpy(nfcData->pname, playData->dirName + 7, 28);
    nfcData->trackCnt = playData->trackCnt;
    if (musicPlayer.playingMusic)
      musicPlayer.stopPlaying();
    if (!musicPlayer.startPlayingFile("/VOICE/0310_T~1.mp3"))
      printerror(201,0);
  }
  else // play error message and exit card setup
  {
    if (musicPlayer.playingMusic)
      musicPlayer.stopPlaying();
    if (!musicPlayer.startPlayingFile("/VOICE/0401_E~1.mp3"))
      printerror(201,0);
    // read buttons before leaving in order to avoid button event when reentering main loop
    delay(100);
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
  if (result == 4)
  { // if play mode is "single track" (i.e. 4) track to be played has to be selected
    result = voiceMenu(playData, 3);
    if (result > 0)
      nfcData->special = result;
    else
      return returnValue;
  }
  nfcData->cookie = 42;

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
  Serial.print(F("Cookie: "));
  Serial.println(dataIn->cookie);
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
  dataIn->trackCnt = (uint8_t)readBuffer[13];
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
      Serial.print(F("MIFARE write failed: "));
      Serial.println(mfrc522.GetStatusCodeName(status));
      returnValue = false;
      return returnValue;
    }
  }
  Serial.println(F("MIFARE write OK "));
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
  uint16_t line_number = 0;
  char buffer[37];

  sdin.open("/index.txt"); // open indexfile
  sdin.seekg(0);               // go to file position 0

  while (true)
  {
    if (!sdin.getline(buffer, 37, '\n'))
      break;
    ++line_number;
    if (strstr(buffer, playData->dirName))
    {
      break;
    }
  }
  playData->pathLine = line_number;
  sdin.close();
}

// select next track
void selectNext(playDataInfo *playData)
{
  playData->currentTrack = playData->currentTrack + 1;
  if (playData->currentTrack > playData->trackCnt)
  {
    playData->currentTrack = playData->trackCnt;
  }
}

// play previous track
void selectPrevious(playDataInfo *playData)
{
  playData->currentTrack = playData->currentTrack - 1;
  if (playData->currentTrack < 1)
  {
    playData->currentTrack = 1;
  }
}

// playFolder looks-up a filename in the indexfile corresponding to a folder number and starts playing the file
void playFolder(playDataInfo *playData, uint8_t foldernum)
{
  
  //stop playing before trying to access SD-Card, otherwhise SPI bus is too busy
  if (musicPlayer.playingMusic)
    musicPlayer.stopPlaying();

  Serial.println(F("select Playfolder"));

  char *pch;
  sdin.open("/index.txt");
  
  sdin.seekg(0);
  uint8_t i = 1;
  uint16_t line_number = 0;
  char buffer[50];

  while (i <= foldernum)
  {
    if (!sdin.getline(buffer, 50, '\n'))
    {
      break;
    }
    pch = strpbrk(buffer, "/"); // locate "/" in order to filter out lines in index file containing the path
    if (pch != NULL)
    {
      i = i + 1;
    }
    ++line_number;
  }
  sdin.close();

  playData->pathLine = line_number;
  pch = strtok(buffer, "\t");
  strcpy(playData->dirName, pch);
  pch = strtok(NULL, "\t");
  playData->trackCnt = atoi(pch); //convert trackcount to int
  playData->currentTrack = 1;
  // generate trackList for current folder
  for (size_t i = 0; i < playData->trackCnt; i++) // generate trackList 1..trackCnt
    playData->trackList[i] = i + 1;

  startPlaying(playData);

  return;
}

void playMenuOption(playDataInfo *playData, int option)
{
  if (musicPlayer.playingMusic)
    musicPlayer.stopPlaying();
  switch (option)
  {
  case 1:
    if (!musicPlayer.startPlayingFile("/VOICE/0311_M~1.mp3"))
      printerror(201,0);
    break;
  case 2:
    if (!musicPlayer.startPlayingFile("/VOICE/0312_M~1.mp3"))
      printerror(201,0);
    break;
  case 3:
    if (!musicPlayer.startPlayingFile("/VOICE/0313_M~1.mp3"))
      printerror(201,0);
    break;
  case 4:
    if (!musicPlayer.startPlayingFile("/VOICE/0314_M~1.mp3"))
      printerror(201,0);
    break;
  case 5:
    if (!musicPlayer.startPlayingFile("/VOICE/0315_M~1.mp3"))
      printerror(201,0);
    break;
  }
}

// start playing track
void startPlaying(playDataInfo *playData)
{
  char fBuffer[13];
  char buffer[50];

  // copy data from play dataStruct to play buffer
  strcpy(buffer, playData->dirName);
  strcat(buffer, "/");
  
  //get filename
  if (musicPlayer.playingMusic)
    musicPlayer.stopPlaying(); //stop playing first, since SD library is unable to access two files at the time
  sdin.open("/index.txt");    // open indexfile on SD card
  sdin.seekg(0);              // rewind filepointer to the start of the card

  uint8_t trackpos = playData->currentTrack;
  uint8_t tracknum = playData->trackList[trackpos - 1];
  uint16_t line_number = playData->pathLine - playData->trackCnt + tracknum - 1; //calculate linenumber to lookup
  
  for (uint16_t i = 1; i < line_number; i++) //go to corresponding line
  {
    sdin.ignore(50, '\n');
  }
  sdin.getline(fBuffer, 13, '\n'); //retrieve filename
  sdin.close();
  strcat(buffer, fBuffer); //concatenate the two strings

  // debug prints
  for (size_t i = 0; i < playData->trackCnt; i++)
  {
    Serial.print(playData->trackList[i]);
    Serial.print(" ");
  }
  Serial.println();
  Serial.print(F("current track pos:     "));
  Serial.println(trackpos);
  sprintf(message, "%d", trackpos);
  if (trackpos<10)
  {    
      mx1.setFont(pFontWide);
      printText(0, MAX_DEVICES1 - 1, message);
  }
  else if (trackpos>=10 && trackpos <20)
  {
    mx1.setFont(pFontNormal);
    printText(0, MAX_DEVICES1 - 1, message);
  }
  else
  {
    mx1.setFont(pFontCondensed);
    printText(0, MAX_DEVICES1 - 1, message);
  }
  
  
  Serial.print(F("selected track number: "));
  Serial.println(tracknum);
  Serial.print(F("Trackname:             "));
  Serial.println(fBuffer);
  Serial.print(F("Fullpath:              "));
  Serial.println(buffer);

  // start playing
  if (!musicPlayer.startPlayingFile(buffer))
    printerror(201,0);
}

/*---------------------------------
display test text
---------------------------------*/
void printText(uint8_t modStart, uint8_t modEnd, char *pMsg)
// Print the text string to the LED matrix modules specified.
// Message area is padded with blank columns after printing.
{
  uint8_t state = 0;
  uint8_t curLen;
  uint16_t showLen;
  uint8_t cBuf[8];
  int16_t col = ((modEnd + 1) * COL_SIZE) - 1;

  mx1.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

  do // finite state machine to print the characters in the space available
  {
    switch (state)
    {
    case 0: // Load the next character from the font table
      // if we reached end of message, reset the message pointer
      if (*pMsg == '\0')
      {
        showLen = col - (modEnd * COL_SIZE); // padding characters
        state = 2;
        break;
      }

      // retrieve the next character form the font file
      showLen = mx1.getChar(*pMsg++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
      curLen = 0;
      state++;
      // !! deliberately fall through to next state to start displaying

    case 1: // display the next part of the character
      mx1.setColumn(col--, cBuf[curLen++]);

      // done with font character, now display the space between chars
      if (curLen == showLen)
      {
        showLen = CHAR_SPACING;
        state = 2;
      }
      break;

    case 2: // initialize state for displaying empty columns
      curLen = 0;
      state++;
      // fall through

    case 3: // display inter-character spacing or end of message padding (blank columns)
      mx1.setColumn(col--, 0);
      curLen++;
      if (curLen == showLen)
        state = 0;
      break;

    default:
      col = -1; // this definitely ends the do loop
    }
  } while (col >= (modStart * COL_SIZE));
  mx1.transform(MD_MAX72XX::TSR);
  mx1.transform(MD_MAX72XX::TRC);
  mx1.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

/*---------------------------------
routine to index SD file structure
---------------------------------*/
void indexDirectoryToFile(File dir, File *indexFile)
{
  char fname[13];
  static char dirName[50] = "/";

  // Begin at the start of the directory
  dir.rewindDirectory();
  uint8_t trackCnt = 0;
  while (true)
  {
    Serial.print(F("."));
    File entry = dir.openNextFile();
    if (!entry)
    {
      // no more files or folder in this directory
      // print current directory name if there are MP3 tracks in the directory
      if (trackCnt != 0)
      {
        indexFile->write(dirName + 1); // avoid printing / at the "begining"
        indexFile->write('\t');
        indexFile->print(trackCnt);
        indexFile->write('\r');
        indexFile->write('\n');
        trackCnt = 0;
      }
      // roll back directory name
      char *pIndex;
      pIndex = strrchr(dirName, '/'); // search last occurence of '/'
      if (pIndex == NULL)
      {
        break; // no more '/' in the dirName
      }
      uint8_t index = pIndex - dirName; // calculate index position from pointer addresses
      if (index == 0)
      {
        break; // don't roll back any further, reached root
      }
      dirName[index] = '\0'; // null terminate at position of '/'
      break;
    }
    // there is an entry
    entry.getSFN(fname);
    // recurse for directories, otherwise print the file
    if (entry.isDirectory())
    {
      //entry is a directory
      //update dirName and reset trackCnt
      strcat(dirName, "/");
      strcat(dirName, fname);
      trackCnt = 0;
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
        trackCnt += 1;
      }
    }
    entry.close();
  }
}

/*---------------------------------
routine to print errors
---------------------------------*/
void printerror(int errorcode, int source)
{
  switch (errorcode)
  {
    case 201:
      Serial.println(F("failed to start playing"));
      break;
    default:
      Serial.println(F("unknown error"));
      break;
  }
}

