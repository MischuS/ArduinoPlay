/***************************************************
Adruino Play

MischuS

****************************************************/

//TODO: Relative Lautstärke auf TAG
//TODO: play handling if single track selected
//TODO: end by setting current track to 0 and check status
//TODO: fast forward fast backward

// include SPI, MP3, Buttons and SDfat libraries
#include <Arduino.h>
#include <SPI.h>
#include <AdaMisch_VS1053.h>
#include <SdFat.h>
#include <JC_Button.h>
#include <sdios.h>
#include <MD_MAX72xx.h>
#include <MFRC522.h>
#include "user_fonts.h" // add user defined fonts for LED Matrix

// Definitions for LED Matrix
#define HARDWARE_TYPE1 MD_MAX72XX::FC16_HW
#define HARDWARE_TYPE2 MD_MAX72XX::FC16_HW
#define MAX_DEVICES1 1
#define MAX_DEVICES2 4
#define CHAR_SPACING 1 // pixels between characters
MD_MAX72XX::fontType_t *pFontNormal = fontSmallNormal; // normal font
MD_MAX72XX::fontType_t *pFontCondensed = fontSmallCondensed; // condensed font
MD_MAX72XX::fontType_t *pFontWide = fontSmallWide; // wide font
#define CLK_PIN 26 
#define DATA_PIN 22
#define CS_PIN1 24
#define CS_PIN2 28

// Global message buffers shared by Serial and Scrolling functions
#define BUF_SIZE 75
char message[BUF_SIZE] = " ";
bool newMessageAvailable = true;

// define the pins used for SPI Communication
#define CLK        52// SPI Clock on MEGA / 13 on UNO
#define MISO       50// SPI master input data MEGA /12 on UNO
#define MOSI       51// SPI master output data MEGA /11 on UNO
#define SHIELD_CS  7 // VS1053 chip select pin 
#define SHIELD_DCS 6 // VS1053 Data/command select pin (output)
#define DREQ       3 // VS1053 Data request
#define CARDCS     4 // SD chip select pin
#define RST_PIN    9 // MFRC522 reset pin
#define SS_PIN     10// MFRC522 chip select pin

// define reset pin
#define SHIELD_RESET -1 // VS1053 reset pin (unused!)

// define button PINs
#define yellowButton A5
#define blueButton   A2
#define greenButton  A3
#define redButton    A4
#define whiteButton  A1

// define empty input for random seed
#define randSource A15

// define communication settings
#define BAUDRATE 38400 // baudrate

// define behaviour of buttons
#define LONG_PRESS 1000

// define volume behavior and limits
#define VOLUME_MAX 25
#define VOLUME_MIN 97
#define VOLUME_INIT 58
#define VOLUME_STEP 3
#define VOLUME_STEPTIME 150

//custom type definitions
struct nfcTagData // struct to hold NFC tag data
{
  uint8_t cookie;   // byte 0 nfc cookie to identify the nfc tag to belong to the player
  char pname[28];   // byte 1-28 char array to hold the path to the folder
  uint8_t trackCnt; // byte 29 number of tracks in the specific folder
  uint8_t mode;     // byte 30 play mode assigne to the nfcTag
  uint8_t special;  // byte 31 track or function for admin nfcTags
};

struct playInfo // struct with essential infos about a tag
{
  uint32_t uid = 0;             // first four bytes of the tags uid
  uint8_t  mode = 1;            // play mode
  uint16_t pathLine = 0;        // line number of the current play path in the index file
  uint8_t  trackCnt = 0;        // track count of the folder
  uint8_t  currentTrack = 1;    // current track, 0 if ended playing
  uint32_t playPos = 0;         // last position within file when removed tag
};

// function definition
void indexDirectoryToFile(File dir, File *indexFile);
int voiceMenu(playInfo playInfoList[], int option);         // voice menu for setting up device
void resetCard();                                           // resets a card
int setupCard(nfcTagData *nfcData, playInfo playInfoList[]); // first time setup of a card
bool readCard(nfcTagData *dataIn);                          // reads card content and save it in nfcTagObject
bool writeCard(nfcTagData *dataOut);                        // writes card content from nfcTagObject
uint16_t findLine(nfcTagData *tagData);                     // find line in index file corresponding to the path saved on the tag
void selectPlayFolder(playInfo playInfoList[], uint8_t foldernum);
void playMenuOption(int option);
void startPlaying(playInfo playInfoList[]);   // start playing selected track
bool selectNext(playInfo playInfoList[]);     // selects next track
void selectPrevious(playInfo playInfoList[]); // selects previous track
void printerror(int errorcode, int source);
void printText(uint8_t modStart, uint8_t modEnd, char *pMsg);
bool handleRecentList(uint32_t currentUid, playInfo  playInfoList[]);
void addToRecentList(uint32_t currentUid, playInfo playInfoList[]);
void updateRecentList(uint32_t currentUid, playInfo playInfoList[], int8_t pos);
void printPlayInfoList(playInfo playInfoList[]);

// instanciate global objects
// create instance of musicPlayer object
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(SHIELD_RESET, SHIELD_CS, SHIELD_DCS, DREQ, CARDCS);

// create instance of card reader
MFRC522 mfrc522(SS_PIN, RST_PIN); // create instance of MFRC522 object

// create display instance
MD_MAX72XX mx1 = MD_MAX72XX(HARDWARE_TYPE1, DATA_PIN, CLK_PIN, CS_PIN1, MAX_DEVICES1);
MD_MAX72XX mx2 = MD_MAX72XX(HARDWARE_TYPE2, DATA_PIN, CLK_PIN, CS_PIN2, MAX_DEVICES2);

// objects for SD handling
ifstream sdin; // input stream for searching in indexfile
SdFat SD;      // file system object

// Buttons
Button uButton(blueButton);
Button lButton(greenButton);
Button mButton(whiteButton);
Button rButton(redButton);
Button dButton(yellowButton);

// global variables
uint8_t volume = VOLUME_INIT; // settings of amplifier
bool tagStatus = false;          // tagStatus=true, tag is present, tagStatus=false no tag present
playInfo playInfoList[3];        // FIFO of recent holds 3 entries

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
  Serial.println(F("start"));
  SPI.begin(); // start SPI communication

  /*------------------------  
  setup nfc HW
  ------------------------*/
  mfrc522.PCD_Init();                // initialize card reader
  Serial.println(F("init NFC Reader"));
  mfrc522.PCD_DumpVersionToSerial(); // print FW version of the reader

  /*------------------------
  setup player
  ------------------------*/
  musicPlayer.begin();                                 // setup music player
  Serial.println(F("VS1053 ok"));                   // print music player info
  musicPlayer.setVolume(volume, volume);               // set volume for R and L chan, 0: loudest, 256: quietest
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT); // setup music player to use interupts

  /*------------------------
  setup SD card
  ------------------------*/
  Serial.print(F("initSD "));
  if (!SD.begin(CARDCS))
  {
    printerror(301, 0);
    while (1)
      ; // SD failed, program stopped
  }
  Serial.println(F("ok"));

  /*------------------------
  set pin mode of GPIOs with buttons
  ------------------------*/
  mButton.begin();
  lButton.begin();
  rButton.begin();
  uButton.begin();
  dButton.begin();

  pinMode(yellowButton,INPUT_PULLUP);
  pinMode(blueButton,  INPUT_PULLUP);
  pinMode(greenButton, INPUT_PULLUP);
  pinMode(whiteButton, INPUT_PULLUP);
  pinMode(redButton,   INPUT_PULLUP);

  /*------------------------
  set pin mode of GPIOS where SPI is connected to to input in order not to distrub SPI communication (for UNO compatibility)
  ------------------------*/
  pinMode(11, INPUT);
  pinMode(12, INPUT);
  pinMode(13, INPUT);

  /*------------------------
  setup display
  ------------------------*/
  mx1.begin(); // display part for numbers
  mx2.begin(); // display part with bar
  mx1.control(MD_MAX72XX::INTENSITY,MAX_INTENSITY/20);
  mx2.control(MD_MAX72XX::INTENSITY,MAX_INTENSITY/20);
  
  mx1.setFont(pFontNormal);
  mx1.transform(MD_MAX72XX::TFLR);
  
  printText(0, MAX_DEVICES1 - 1, message);
  newMessageAvailable = false;

  for (uint16_t c = 32; c > 7; c--)
  {
    mx2.setColumn(c, 0b00011000);
    delay(20);
  }
  delay(200);
  mx2.clear();
  
  /*------------------------
  startup program
  ------------------------*/ 
  // index SD card if left, middle and right button are pressed during startup
  if (lButton.read() && mButton.read() && rButton.read())
  {
    
    Serial.println(F("index "));

    if (musicPlayer.playingMusic) // stop music player in order to avoid parallel access to SD card
      musicPlayer.stopPlaying();

    if (SD.exists("/index.txt")) // remove existing index file from SD card
    {
      SD.remove("/index.txt");
    }
    File indexfile = SD.open("/index.txt", FILE_WRITE); // open new indexfile on SD card
    if (indexfile)
    {
      Serial.println(F("start"));
    }
    else
    {
      printerror(302, 0);
    }
    File root = SD.open("/");
    indexDirectoryToFile(root, &indexfile);
    indexfile.close();
    Serial.println(F(" ok"));
    root.close();
  }
  Serial.println(F("start main loop"));
}

/* Main Loop of program
 *  
 */

void loop()
{
  /*------------------------
  init variables
  ------------------------*/
  static bool lockState = true;    // box is locked unless key card is used on nfc reader, no configuration possible
  static bool mButtonLong = false; // state variable allowing to ignore release after long press
  static bool rButtonLong = false;
  static bool lButtonLong = false;
  nfcTagData dataIn;
  
  /*------------------------
  player status handling
  ------------------------*/
  if (!musicPlayer.playingMusic && tagStatus && !musicPlayer.paused()) // tag is present but no music is playing play next track if possible
  {
    if(selectNext(playInfoList))
    {
      Serial.println(F("next track selected"));
      startPlaying(playInfoList);
    }
    else
    {
      // tag is present but music playing ended
      // display nothing on LED display
      Serial.println(F("ended playing all files"));
      sprintf(message, " ");
      printText(0, MAX_DEVICES1 - 1, message);
      // remove uid from play info list, reset complete entry
      playInfoList[0].uid = 0;
      playInfoList[0].mode = 1;
      playInfoList[0].pathLine = 0;
      playInfoList[0].trackCnt = 0;
      playInfoList[0].currentTrack = 1;
      playInfoList[0].playPos = 0;
    }
  }

  /*------------------------
  buttons handling
  ------------------------*/
  // read out all button states
  mButton.read();
  lButton.read();
  rButton.read();
  uButton.read();
  dButton.read();

  // up/down button handling for volume control
  if (uButton.wasReleased() || uButton.pressedFor(LONG_PRESS)) //increase volume
  {
    volume = volume - VOLUME_STEP;
    if (volume <= VOLUME_MAX)
    {
      volume = VOLUME_MAX;
      Serial.println(F("max vol"));
    }
    musicPlayer.setVolume(volume, volume);
    Serial.println(volume);
    delay(VOLUME_STEPTIME); // delay the program execution not to step up volume too fast
  }
  if (dButton.wasReleased() || dButton.pressedFor(LONG_PRESS)) //decrease volume
  {
    volume = volume + VOLUME_STEP;
    if (volume >= VOLUME_MIN)
    {
      volume = VOLUME_MIN;
      Serial.println(F("min vol"));
    }
    musicPlayer.setVolume(volume, volume);
    Serial.println(volume);
    delay(VOLUME_STEPTIME); // delay the program execution not to step up volume too fast
  }

  // left/right button handling for next track, previous track
  if(tagStatus)
  {
    // left button handling
    if (lButton.pressedFor(LONG_PRESS)) // fast backward
    {
      Serial.println(F("fast backward"));
      lButtonLong = true;
    }
    if (lButton.wasReleased()) // previous track
    {
      if(lButtonLong)
        lButtonLong = false;
      else
      {
        Serial.println(F("previous"));
        selectPrevious(playInfoList);
        startPlaying(playInfoList);
      }
    }
    // right button handling
    if (rButton.pressedFor(LONG_PRESS)) // fast forward
    {
      rButtonLong = true;
      Serial.println(F("fast forward"));
      Serial.print(F("file size: "));     Serial.println(musicPlayer.fileSize());
      Serial.print(F("file position: ")); Serial.println(musicPlayer.filePosition());
      long targetPosition = musicPlayer.filePosition() + (1024L * 256L);
      Serial.print(F("target progress: ")); Serial.println(100L * musicPlayer.filePosition());
      //if targetPosition*100 < musicPlayer.fileSize(){
      musicPlayer.fileSeek(musicPlayer.filePosition() + (1024L * 256L));
      //}
      //else
      //{
       // if (selectNext(playInfoList))
       // {
       //   startPlaying(playInfoList);
       // }
      //}
      
    }
    if (rButton.wasReleased()) // next track
    {
      if(rButtonLong)
        rButtonLong = false;
      else
        {
          Serial.println(F("next"));
          if(selectNext(playInfoList))
          {
            startPlaying(playInfoList);
          }
        }     
    }
  }
  // middle button handling
  if (mButton.pressedFor(LONG_PRESS)) // setup new card
  {
    Serial.println(F("M long"));
    if (tagStatus == false && lockState == false) // no card present enter reset card menu
    {
      Serial.println(F("reset tag"));
      if (!musicPlayer.startPlayingFile("/VOICE/0800_R~1.MP3"))
        printerror(201, 1);
      resetCard();
    }
    mButtonLong = true; // long press detected, thus set state to ignore button release
  }
  else if (mButton.wasReleased() && tagStatus) // play/pause
  {
    Serial.println(F("M release"));
    if (mButtonLong == true) // check whether it is a release after longPress
      mButtonLong = false;   // reset long press detect to initial state, since button is released
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
        sprintf(message, "%d", playInfoList[0].currentTrack);
        printText(0, MAX_DEVICES1 - 1, message);
        Serial.println(F("resume"));
        musicPlayer.pausePlaying(false);
      }
    }
    if (mButton.wasReleased())
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
    if (c == 'd') // debug print
    {
      Serial.println(F("current playInfoList"));
      printPlayInfoList(playInfoList);
    }
    if (c == 'k') // create key card
    {
      Serial.println(F("create new key card"));
      if (tagStatus == true) 
      {
        // create new key card
        dataIn.cookie = 24;
        writeCard(&dataIn);
      }
      else
      {
        Serial.println(F("no tag found, pleas repeate"));
      }
    }
    if (c == 'l') // lock / unlock
    {
      lockState = !lockState;
      if (lockState) Serial.println(F("device locked"));
      else Serial.println(F("device unlocked"));
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
      printerror(101, 0);
    }
  }
  if (newTagStatus != tagStatus) // nfc card status changed
  {
    tagStatus = newTagStatus;
    if (tagStatus) // nfc card added
    {
      Serial.print(F("tag detected: "));
      readCard(&dataIn);
      Serial.print(F("cookie: "));
      Serial.println(dataIn.cookie);
      switch (dataIn.cookie)
      {
      default:
        // tag is not configured, init card
        Serial.println(F("unknown"));
        nfcTagData writeData;
        setupCard(&writeData, playInfoList);
        readCard(&dataIn);
        break;
      case 24: // key card
        lockState = !lockState;
        if (lockState)
          Serial.println(F("device locked"));
        else
          Serial.println(F("device unlocked"));
        break;
      case 42: // configured tag
        Serial.println(F("configured tag"));
        // check if the card is in the playInfoList
        uint32_t currentUid;
        memcpy(&currentUid, mfrc522.uid.uidByte, sizeof(uint32_t));
        bool uidKnown = handleRecentList(currentUid, playInfoList); //if uid in list, element is moved to pos [0] otherwhise uid is added to pos [0] and otheres are pushed backwards
        if (!uidKnown)                                              //uid was not in playInfoList, lookup information and populate all required information in list
        {
          playInfoList[0].mode = dataIn.mode;
          playInfoList[0].pathLine = findLine(&dataIn);
          playInfoList[0].trackCnt = dataIn.trackCnt;
          if (dataIn.mode == 4)
          {
            playInfoList[0].currentTrack = dataIn.special;
          }
          else
          {
            playInfoList[0].currentTrack = 1;
          }
          playInfoList[0].playPos = 0;
        }
        else // uid already existing in playInfoList use it
        {
          Serial.println(F("uid in list"));
        }
        Serial.println(F("Data Loaded:"));
        printPlayInfoList(playInfoList);
        Serial.println(F("start playing:"));
        startPlaying(playInfoList);
        break;
      
      }
    }
    else // nfc card removed
    {
      Serial.println(F("tag removed"));
      // save current trackPos to recent list in order to resume correctly is the same tag is reapplied
      if (musicPlayer.playingMusic || musicPlayer.paused())
      {
        playInfoList[0].playPos = musicPlayer.stopPlaying();
        printPlayInfoList(playInfoList);
        
        sprintf(message, " ");
        printText(0, MAX_DEVICES1, message);
      }
    }
  }
}

/* support functions
==========================================================================================
*/

int voiceMenu(playInfo playInfoList[], int option)
{
  int returnValue = 0;
  playInfoList[0].playPos = 0;

  Serial.print(F("voice menu"));
  Serial.println(option, DEC);
  if (musicPlayer.playingMusic)
    musicPlayer.stopPlaying();
  
  switch (option) // explenation text
  {
  case 1: // folder select
    if (!musicPlayer.startPlayingFile("/VOICE/0300_N~1.mp3"))
      printerror(201, 1);
    break;
  case 2: // play mode select
    if (!musicPlayer.startPlayingFile("/VOICE/0310_T~1.mp3"))
      printerror(201, 1);
    break;
  case 3: // select track within folder
    if (!musicPlayer.startPlayingFile("/VOICE/0320_S~1.mp3"))
      printerror(201, 1);
    break;
  }

  do
  {
    mButton.read();
    rButton.read();
    lButton.read();
    uButton.read();
    dButton.read();

    // abort voice menu by long middle button
    if (mButton.pressedFor(LONG_PRESS))
    {
      if (musicPlayer.playingMusic)
        musicPlayer.stopPlaying();
      return 0;
    }

    // exit voice menu by middle button
    if (mButton.wasPressed())
    {
      if (musicPlayer.playingMusic)
        musicPlayer.stopPlaying();
      return returnValue;
    }
    
    switch (option)
    {
    case 1: // folder select
      // browse folders by up/down buttons
      if (uButton.wasPressed())
      {
        returnValue += 1;
        Serial.print("index: ");
        Serial.println(returnValue, DEC);
        selectPlayFolder(playInfoList, returnValue);
        startPlaying(playInfoList);
      }
      if (dButton.wasPressed())
      {
        if (returnValue <= 1)
          returnValue = 1;
        else
          returnValue -= 1;
        selectPlayFolder(playInfoList, returnValue);
        startPlaying(playInfoList);
      }
      // browse within a folder by left/right buttons
      if (rButton.wasPressed())
      {
        if(selectNext(playInfoList))
        {
          startPlaying(playInfoList);
        }
      }
      if (lButton.wasPressed())
      {
        selectPrevious(playInfoList);
        startPlaying(playInfoList);
      }
      break;

    case 2: // play mode select
      if (uButton.wasPressed())
      {
        returnValue += 1;
        if (returnValue >= 6)
          returnValue = 1;
        playMenuOption(returnValue);
      }
      if (dButton.wasPressed())
      {
        returnValue -= 1;
        if (returnValue <= 0)
          returnValue = 5;
        playMenuOption(returnValue);
      }
      break;

    case 3: // select track within folder
      if (uButton.wasPressed())
      {
        if (musicPlayer.playingMusic)
          musicPlayer.stopPlaying();
        if (returnValue == 0)
        {
          returnValue = 1;
          startPlaying(playInfoList);
        }
        else
        {
          if(selectNext(playInfoList))
          {
            startPlaying(playInfoList);
          }
          returnValue = playInfoList[0].currentTrack;
          Serial.println(returnValue);
        }
      }
      if (dButton.wasPressed())
      {
        if (returnValue == 0)
        {
          returnValue = 1;
          startPlaying(playInfoList);
        }
        else
        {
          selectPrevious(playInfoList);
          startPlaying(playInfoList);
          returnValue = playInfoList[0].currentTrack;
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
    lButton.read();
    uButton.read();
    dButton.read();

    if (uButton.wasReleased() || dButton.wasReleased())
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
  Serial.print(F("reset tag"));
  writeCard(&emptyData);
  if (musicPlayer.playingMusic)
    musicPlayer.stopPlaying();
  if (!musicPlayer.startPlayingFile("/VOICE/0801_R~1.MP3"))
    printerror(201,0);
}

int setupCard(nfcTagData *nfcData, playInfo playInfoList[])
{
  // variables for function
  int returnValue = 0;
  int result = 0;
  char dirName[50];

  // STEP 1 select selectPlayFolder by voiceMenu
  result = voiceMenu(playInfoList, 1);
  if (result > 0) // copy selected folder to nfcData struct
  {
    sdin.open("/index.txt"); // open indexfile on SD card
    sdin.seekg(0);           // rewind filepointer to the start of the file
    for (uint16_t i = 1; i < playInfoList[0].pathLine; i++) //go to path line
    {
      sdin.ignore(50, '\n');
    }
    sdin.getline(dirName, 50, '\n'); //copy pathname to buffer
    sdin.close();
    strtok(dirName, "\t");
    Serial.println(dirName);

    strncpy(nfcData->pname, dirName + 7, 28);
    
    nfcData->trackCnt = playInfoList[0].trackCnt;
    
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
    lButton.read();
    mButton.read();
    rButton.read();
    uButton.read();
    dButton.read();

    return returnValue;
  }

  // STEP 2 select play mode by voiceMenu
  result = voiceMenu(playInfoList, 2);
  
  if (result > 0)
  {
    playInfoList[0].mode = result;
    playInfoList[0].currentTrack = 1;
    nfcData->mode = result;
  }
  
  if (result == 4)
  { // if play mode is "single track" (i.e. 4) track to be played has to be selected
    result = voiceMenu(playInfoList, 3);
    if (result > 0)
    {
      playInfoList[0].currentTrack = result;
      nfcData->special = result;
    }
    else
    {
      return returnValue;
    }
  }
   
  nfcData->cookie = 42;

  writeCard(nfcData);

  // read buttons before leaving in order to avoid button event when reentering main loop
  delay(100);

  lButton.read();
  mButton.read();
  rButton.read();
  uButton.read();
  dButton.read();
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
    printerror(101, 0);
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
    printerror(101, 0);
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
      printerror(102, 0);
      Serial.println(mfrc522.GetStatusCodeName(status));
      returnValue = false;
      return returnValue;
    }
  }
  Serial.println(F("tag write ok"));
  return returnValue;
}

/*---------------------------------
track handling routines MOVE to extra file later on
---------------------------------*/
// find line in index file for given path (from NFC tag)
uint16_t findLine(nfcTagData *tagData)
{
  uint16_t lineNumber = 0;
  char buffer[37];

  sdin.open("/index.txt"); // open indexfile
  sdin.seekg(0);           // go to file position 0

  while (true)
  {
    if (!sdin.getline(buffer, 37, '\n'))
      break;
    ++lineNumber;
    if (strstr(buffer, tagData->pname))
    {
      break;
    }
  }
  sdin.close();
  return lineNumber;
}

// select next track
bool selectNext(playInfo playInfoList[])
{ 
  // add 1 to current track
  playInfoList[0].currentTrack = playInfoList[0].currentTrack + 1;
  if (playInfoList[0].currentTrack > playInfoList[0].trackCnt)
  {
    playInfoList[0].currentTrack = playInfoList[0].trackCnt;
    return false;
  }
  else
  {
    // reset playPos to 0 in order to restart at the beginning
    playInfoList[0].playPos = 0;
    return true;
  }
}

// play previous track
void selectPrevious(playInfo playInfoList[])
{
  // reset playPos to 0 in order to restart at the beginning
  playInfoList[0].playPos = 0;

  // remove 1 from current track
  playInfoList[0].currentTrack = playInfoList[0].currentTrack - 1;
  if (playInfoList[0].currentTrack < 1)
  {
    playInfoList[0].currentTrack = 1;
  }
}

// selectPlayFolder looks-up a lineNumber and trackCount in the indexfile corresponding to a folder number
void selectPlayFolder(playInfo playInfoList[], uint8_t foldernum)
{

  //stop playing before trying to access SD-Card, otherwhise SPI bus is too busy
  if (musicPlayer.playingMusic)
    musicPlayer.stopPlaying();

  Serial.println(F("select folder"));

  char *pch;
  sdin.open("/index.txt");
  
  sdin.seekg(0);
  uint8_t i = 1;
  uint16_t lineNumber = 0;
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
    ++lineNumber;
  }
  sdin.close();

  playInfoList[0].pathLine = lineNumber;
  pch = strtok(buffer, "\t");
  pch = strtok(NULL, "\t");
  playInfoList[0].trackCnt = atoi(pch); //convert trackcount to int
  playInfoList[0].currentTrack = 1;
  playInfoList[0].playPos = 0;
  return;
}

void playMenuOption(int option)
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
void startPlaying(playInfo playInfoList[])
{
  char fBuffer[13]; //file buffer
  char buffer[50];  //full path buffer

 
  //get full path to file
  //---------------------
  //1. open index file and go to beginning of file
  if (musicPlayer.playingMusic)
    musicPlayer.stopPlaying(); //stop playing first, since SD library is unable to access two files at the time
  
  sdin.open("/index.txt");    // open indexfile on SD card
  sdin.seekg(0);              // rewind filepointer to the start of the file
  
  //2. calculate linenumbers to look up for filename
  uint16_t fLine = playInfoList[0].pathLine - playInfoList[0].trackCnt + playInfoList[0].currentTrack - 1;

  //3. retrieve filename  
  for (uint16_t i = 1; i < fLine; i++) //go to line with filename
  {
    sdin.ignore(50, '\n');
  }
  sdin.getline(fBuffer, 13, '\n'); //copy filename to fBuffer

  //4. retrieve pathname
  for (uint16_t i = fLine+1; i < playInfoList[0].pathLine; i++) //go to path line
  {
    sdin.ignore(50, '\n');
  }
  sdin.getline(buffer, 50, '\n'); //copy pathname to buffer
  sdin.close();
  strtok(buffer, "\t");
  Serial.println(buffer);
  strcat(buffer, "/");
  strcat(buffer, fBuffer); //concatenate the two strings

  Serial.println();
  Serial.print(F("current track pos: "));
  
  //update tracknum on display
  //--------------------------
  Serial.println(playInfoList[0].currentTrack);
  sprintf(message, "%d", playInfoList[0].currentTrack);
  if (playInfoList[0].currentTrack < 10)
  {    
      mx1.setFont(pFontWide);
      printText(0, MAX_DEVICES1 - 1, message);
  }
  else if (playInfoList[0].currentTrack >= 10 && playInfoList[0].currentTrack < 20)
  {
    mx1.setFont(pFontNormal);
    printText(0, MAX_DEVICES1 - 1, message);
  }
  else
  {
    mx1.setFont(pFontCondensed);
    printText(0, MAX_DEVICES1 - 1, message);
  }
  
  //start playing selected File
  //----------------------------

  if (playInfoList[0].playPos !=0)
  {
    if (!musicPlayer.startPlayingFile(buffer,playInfoList[0].playPos)) // start playing from given position
      printerror(201, 0);
  }
  else {
    if (!musicPlayer.startPlayingFile(buffer)) // start playing from file start
      printerror(201,0);
  }
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
routine manage playlist
---------------------------------*/

// handle recent list, false if new element added, true if element already exist
bool handleRecentList(uint32_t currentUid, playInfo playInfoList[])
{
  int8_t pos = 3;
  // iterrate through list of recent Uid
  for (int8_t i = 0; i < 3; i++)
  {
    if (playInfoList[i].uid == currentUid)
    {
      pos = i;
    }
  }
  if (pos == 3) // current uid is not in recent list
  {
    Serial.println(F("uid not in list, add to list"));
    addToRecentList(currentUid, playInfoList);
    return false;
  }
  else // current pos is not in recent list
  {
    Serial.println(F("uid is in list, move to beginning"));
    updateRecentList(currentUid, playInfoList, pos);
    return true;
  }
  
}

// uid is not in recent list, insert new uid at beginning of list, move other entries within list
void addToRecentList(uint32_t currentUid, playInfo playInfoList[])
{
  for (int8_t i = 2; i < 3; i++)
  {
    playInfoList[2] = playInfoList[1];
    playInfoList[1] = playInfoList[0];
    playInfoList[0].uid = currentUid;
  }
}

// uid is in recent list, reanrange recent list to have current uid at beginning
void updateRecentList(uint32_t currentUid, playInfo playInfoList[], int8_t pos)
{
  playInfo temp;
  // copy from pos0 to temp;
  temp = playInfoList[0];

  // TODO: implement this in a more generic way
  if (pos == 0) // nothing to do, uid is already at pos 0
  {
    return;
  }
  if (pos == 1)
  {
    playInfoList[0] = playInfoList[1];
    playInfoList[1] = temp;
    return;
  }
  if (pos == 2)
  {
    playInfoList[0] = playInfoList[2];
    playInfoList[2] = playInfoList[1];
    playInfoList[1] = temp;
    return;
  }
}

// print recent list to serial
void printPlayInfoList(playInfo playInfoList[])
{
  Serial.println(F("playInfoList:"));
  for (uint8_t i = 0; i < 3; i++)
  {
    Serial.print(F("uid:"));
    Serial.print(playInfoList[i].uid);

    Serial.print(F("\t mode:"));
    Serial.print(playInfoList[i].mode);

    Serial.print(F("\t line:"));
    Serial.print(playInfoList[i].pathLine);

    Serial.print(F("\t track:"));
    Serial.print(playInfoList[i].currentTrack);
    
    Serial.print(F("\t file pos:"));
    Serial.println(playInfoList[i].playPos);
  }
  Serial.println();
}

/*---------------------------------
routine to print errors
---------------------------------*/
void printerror(int errorcode, int source)
{
  Serial.print(F("ERROR: "));
  switch (errorcode)
  {
  // error codes for nfc tag 100 -199
  case 101:
  {
    Serial.println(F("reading nfc"));
    break;
  }
  case 102:
  {
    Serial.println(F("writing nfc"));
  }
  // error codes for player 200 - 299
  case 201:
  {
    Serial.println(F("start playing"));
    break;
  }
  // error codes for SD card 300 - 399
  case 301:
  {
    Serial.println(F("opnening SD"));
    break;
  }
  case 302:
  {
    Serial.println(F("opening file"));
    break;
  }
  // default error
  default:
  {
    Serial.println(F("unknown"));
    break;
  }
  }
}
