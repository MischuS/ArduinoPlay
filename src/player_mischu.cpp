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

#include <MFRC522.h>
#include <EEPROM.h>

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
#define yellowButton A2
#define blueButton A1
#define greenButton A0

// define communication settings
#define BAUDRATE 38400 // baudrate

// define behaviour of buttons
#define LONG_PRESS 1000

// define limits
#define MAX_VOLUME 5
#define MIN_VOLUME 100
#define VOLUME_STEPTIME 500

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
  char fullname[50];    // general buffer used throught the program
};

// function definition
void indexDirectoryToFile(File dir, File *indexFile);
static void nextTrack(uint16_t track);
int voiceMenu(playDataInfo *playData,
              int numberOfOptions, int startMessage,
              int messageOffset, bool preview = false,
              int previewFromFolder = 0);                 // voice menu for setting up device
void resetCard();                                         // resets a card
void setupCard(nfcTagData *data, playDataInfo *playData); // first time setup of a card
bool readCard(nfcTagData *dataIn);                        // reads card content and save it in nfcTagObject
void writeCard(nfcTagData *dataOut);                      // writes card content from nfcTagObject
void findPath(playDataInfo *playData);
void getTrackName(playDataInfo *playData);
void playFolder(playDataInfo *playData, uint8_t foldernum);
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
Button leftButton(yellowButton);
Button middleButton(blueButton);
Button rightButton(greenButton);

// global variables
uint8_t volume = 40;    // settings of amplifier
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
  leftButton.begin();
  middleButton.begin();
  rightButton.begin();

  pinMode(yellowButton, INPUT_PULLUP);
  pinMode(blueButton, INPUT_PULLUP);
  pinMode(greenButton, INPUT_PULLUP);

  /*------------------------
  startup program
  ------------------------*/
  randomSeed(analogRead(A0)); // initialize random number geneartor

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
  static bool leftButtonIgnoreRelease = false;   // this is used to ignore release after long press
  static bool middleButtonIgnoreRelease = false; // this is used to ignore release after long press
  static bool rightButtonIgnoreRelease = false;  // this is used to ignore release after long press

  playDataInfo playData;
  nfcTagData dataIn;

  /*------------------------
  buttons handling
  ------------------------*/
  leftButton.read();
  middleButton.read();
  rightButton.read();

  // left button handling
  if (leftButton.pressedFor(LONG_PRESS)) // long press left button decreses volume
  {
    volume = volume + 5;
    if (volume >= MIN_VOLUME)
      volume = MIN_VOLUME;
    musicPlayer.setVolume(volume, volume);
    Serial.println(volume);
    delay(VOLUME_STEPTIME); // delay the program execution not to step up volume too fast
    leftButtonIgnoreRelease = true;
  }
  else if (leftButton.wasReleased() && tagStatus) // short press left button goes back 1 track
  {
    if (leftButtonIgnoreRelease == true)
    {
      leftButtonIgnoreRelease = false; // disable ignoring
    }
    else
    {
      Serial.println(F("Yellow Button"));
      playPrevious(&playData);
    }
  }

  // middle button handling
  if (middleButton.pressedFor(LONG_PRESS)) // long press allows to setup new card
  {
    if (tagStatus == false) // no card present enter reset card menu
    {
      Serial.println(F("Reset nfc Tag ..."));
      if (!musicPlayer.startPlayingFile("/VOICE/0800_R~1.MP3"))
      {
        Serial.println(F("Failed to start playing"));
      }
      resetCard();
    }
    middleButtonIgnoreRelease = true;
  }
  else if (middleButton.wasReleased() && tagStatus) // short press of  middle button is play/pause
  {
    if (middleButtonIgnoreRelease == true)
    {
      middleButtonIgnoreRelease = false;
    }
    else
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
  }

  // right button handling
  if (rightButton.pressedFor(LONG_PRESS)) // right button long press increases volume
  {
    volume = volume - 5;
    if (volume <= MAX_VOLUME)
      volume = MAX_VOLUME;
    musicPlayer.setVolume(volume, volume);
    Serial.println(volume);
    delay(VOLUME_STEPTIME); // delay the program execution not to step up volume too fast
    rightButtonIgnoreRelease = true;
  }
  else if (rightButton.wasReleased() && tagStatus) // short press of right button is next track
  {
    if (rightButtonIgnoreRelease == true)
    {
      rightButtonIgnoreRelease = false;
    }
    else
    {
      playNext(&playData);
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
      File root = SD.open("/MUSIC");
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

  bool newTagStatus = false;
  for (uint8_t i = 0; i < 3; i++) //try to check tag status several times since eventhough tag is present it is not continisouly seen
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
      Serial.println(F("Card detected"));
      readCard(&dataIn);
      Serial.print("Read Data Cookie:  ");
      Serial.println(dataIn.cookie, DEC);
      //if (dataIn.cookie != 857536)
      if (dataIn.cookie != 42)
      {
        // tag is not know, init card
        Serial.println(F("unknown card"));
        nfcTagData writeData;
        setupCard(&writeData, &playData);
        /*nfcTagData testdata = {42,             // cookie to identify the nfc tag to belong to the player
                               "AIR/10000H~1", // Byte1-28 char array to hold the path to the folder
                               11,             // Byte29 number of tracks in the specific folder
                               1,              // Byte30 play mode assigne to the nfcTag
                               1};             // Byte31 track or function for admin nfcTags

        writeCard(&testdata); // write NFC Data*/
      }
      else
      {
        Serial.println(F("known card"));
        //tag is known copy data to playData struct
        strcpy(playData.dirname, "/MUSIC/");
        strcat(playData.dirname, dataIn.pname);
        playData.trackcnt = dataIn.trackcnt;
        Serial.println(playData.trackcnt, DEC);
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
  //delay(200);
  //Serial.print(F("."));
}

/* support functions
==========================================================================================
*/

int voiceMenu(playDataInfo *playData, int numberOfOptions, int startMessage, int messageOffset,
              bool preview = false, int previewFromFolder = 0)
{
  Serial.println("entering voice menu");

  musicPlayer.stopPlaying();
  int returnValue = 0;
  File messageFile;
  File messageRoot = SD.open("/VOICE");

  strcpy(playData->dirname, "/VOICE/");
  //Serial.println(playData->dirname);

  if (startMessage != 0)
  {
    messageRoot.rewindDirectory();
    for (int i = 0; i < startMessage; i++)
    {
      messageFile = messageRoot.openNextFile();
    }
    messageFile.getSFN(playData->fname);
    messageRoot.close();
    strcpy(playData->fullname, playData->dirname);
    strcat(playData->fullname, playData->fname);
    startPlaying(playData);
  }

  do
  {
    middleButton.read();
    rightButton.read();
    leftButton.read();

    if (middleButton.wasPressed())
    {
      musicPlayer.stopPlaying();
      if (returnValue != 0)
        return returnValue;
      delay(1000);
    }

    //if (rightButton.pressedFor(LONG_PRESS))
    //{
    //returnValue = min(returnValue + 10, numberOfOptions);
    //mp3.playMp3FolderTrack(messageOffset + returnValue);
    //delay(1000);
    //if (preview) {
    //  do {
    //    delay(10);
    //  } while (isPlaying());
    //  if (previewFromFolder == 0)
    //    mp3.playFolderTrack(returnValue, 1);
    //  else
    //    mp3.playFolderTrack(previewFromFolder, returnValue);
    //}
    //ignorerightButton = true;
    //}
    //else
    if (rightButton.wasReleased())
    {
      musicPlayer.stopPlaying();
      returnValue += 1;
      Serial.print("folder index:\t");
      Serial.println(returnValue, DEC);
      playFolder(playData, returnValue);
      //if (!ignorerightButton)
      //{
      //returnValue = min(returnValue + 1, numberOfOptions);

      //mp3.playMp3FolderTrack(messageOffset + returnValue);
      //delay(1000);
      //if (preview)
      //{
      //  do
      // {
      //    delay(10);
      //  } while (isPlaying());
      //  if (previewFromFolder == 0)
      //    mp3.playFolderTrack(returnValue, 1);
      //  else
      //    mp3.playFolderTrack(previewFromFolder, returnValue);
      //  }
      //}
      //else
      //ignorerightButton = false;
    }

    /*if (leftButton.pressedFor(LONG_PRESS)) {
      returnValue = max(returnValue - 10, 1);
      mp3.playMp3FolderTrack(messageOffset + returnValue);
      delay(1000);
      if (preview) {
        do {
          delay(10);
        } while (isPlaying());
        if (previewFromFolder == 0)
          mp3.playFolderTrack(returnValue, 1);
        else
          mp3.playFolderTrack(previewFromFolder, returnValue);
      }
      ignoreleftButton = true;
    } else if (leftButton.wasReleased()) {
      if (!ignoreleftButton) {
        returnValue = max(returnValue - 1, 1);
        mp3.playMp3FolderTrack(messageOffset + returnValue);
        delay(1000);
        if (preview) {
          do {
            delay(10);
          } while (isPlaying());
          if (previewFromFolder == 0)
            mp3.playFolderTrack(returnValue, 1);
          else
            mp3.playFolderTrack(previewFromFolder, returnValue);
        }
      } else
        ignoreleftButton = false;
    }*/
  } while (true);
}

void resetCard()
{
  nfcTagData emptyData = {0, "", 0, 0, 0};
  do
  {
    leftButton.read();
    middleButton.read();
    rightButton.read();

    if (leftButton.wasReleased() || rightButton.wasReleased())
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

void setupCard(nfcTagData *data, playDataInfo *playData)
{
  //musicPlayer.stopPlaying();
  //SD.chdir("/VOICE/");
  Serial.println(F("configure new card"));
  musicPlayer.stopPlaying();

  // find playfolder by voiceMenu
  voiceMenu(playData, 99, 256, 0, true);
  //myCard.folder = 1;

  // Wiedergabemodus abfragen
  // for dev: myCard.mode = voiceMenu(6, 310, 310);
  //myCard.mode = 1;

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

  
  // Karte ist konfiguriert -> speichern
  musicPlayer.pausePlaying(true);
  writeCard(myCard);*/
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
    return;
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
    return;
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

void writeCard(nfcTagData *dataOut)
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
      return;
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
  uint8_t trackcnt = 0;
  uint8_t line_number = 0;
  char linebuffer[50]; // for optimization fullname buffer might be reused...

  sdin.open("/index.txt"); // open indexfile
  sdin.seekg(0);           // go to file position 0

  while (true)
  {
    if (!sdin.getline(linebuffer, 50, '\n'))
      break;
    if (strstr(linebuffer, playData->dirname))
    {
      // read out track number, not required here but for testing purposes
      // this will be necessary in adding a card
      //char *pch;
      //pch = strtok(linebuffer, "\t");
      //pch = strtok(NULL, "\t");
      //int trackcnt = pch - '0';
      //Serial.println(pch);
      break;
    }
    ++line_number;
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
  for (uint8_t i = 0; i < line_number; i++)
  {
    sdin.ignore(50, '\n');
  }
  sdin.getline(playData->fname, 13, '\n');

  strcpy(playData->fullname, playData->dirname);
  strcat(playData->fullname, "/");
  strcat(playData->fullname, playData->fname);
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

void playFolder(playDataInfo *playData, uint8_t foldernum)
{
  char linebuffer[50]; // for optimization fullname buffer might be reused...
  char *pch;
  sdin.open("/index.txt");
  sdin.seekg(0);
  uint8_t i = 1;
  uint8_t line_number = 0;

  while (i <= foldernum)
  {
    if (!sdin.getline(linebuffer, 50, '\n'))
    {
      break;
    }
    //Serial.println(linebuffer);
    pch = strpbrk(linebuffer, "/"); // locate "/" in order to filter out lines in index file containing the path
    if (pch != NULL)
    {
      //Serial.println("/ found ");
      i = i + 1;
    }
    ++line_number;
  }
  sdin.close();

  playData->pathLine = line_number;
  Serial.print(F("pathline:\t"));
  Serial.println(playData->pathLine, DEC);

  pch = strtok(linebuffer, "\t");
  strcpy(playData->dirname, pch);
  Serial.print(F("dirname:\t"));
  Serial.println(playData->dirname);

  pch = strtok(NULL, "\t");
  playData->trackcnt = atoi(pch); //convert trackcount to int
  Serial.print(F("trackcnount:\t"));
  Serial.println(playData->trackcnt);

  playData->currentTrack = 0;
  Serial.print(F("current track:\t"));
  Serial.println(playData->currentTrack);

  getTrackName(playData);
  Serial.print(F("filename:\t"));
  Serial.println(playData->fname);

  strcpy(playData->fullname, playData->dirname);
  strcat(playData->fullname, "/");
  strcat(playData->fullname, playData->fname);
  Serial.print(F("fullname:\t"));
  Serial.println(playData->fullname);
  startPlaying(playData);
  //Serial.println(playData->trackcnt,DEC);
  //Serial.println(playData->dirname);
  //Serial.println(playData->trackcnt, DEC);

  return;
}

// start playing track
void startPlaying(playDataInfo *playData)
{
  Serial.println(F("Start Playing"));

  //Serial.println(playData->fullname);
  if (!musicPlayer.startPlayingFile(playData->fullname))
  {
    Serial.println(F("Failed to start playing"));
  }
}

/*---------------------------------
routine to index SD file structure
---------------------------------*/
void indexDirectoryToFile(File dir, File *indexFile)
{
  char fname[13];
  static char dirname[50] = "/MUSIC";

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
        indexFile->write(dirname);
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