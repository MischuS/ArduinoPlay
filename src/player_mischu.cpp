/***************************************************
  This is an example for the Adafruit VS1053 Codec Breakout

  Designed specifically to work with the Adafruit VS1053 Codec Breakout
  ----> https://www.adafruit.com/products/1381

  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  BSD license, all text above must be included in any redistribution
 ****************************************************/

// include SPI, MP3, EEPROM and SD libraries
#include <Arduino.h>
#include <SPI.h>
#include "Adafruit_VS1053.h"
//#include <SD.h>
#include <SdFat.h>
#include <sdios.h>

#include <MFRC522.h>
#include <EEPROM.h>

// define the pins used
#define CLK 13       // SPI Clock
#define MISO 12      // SPI master input data
#define MOSI 11      // SPI master output data
#define SHIELD_CS 7  // VS1053 chip select pin
#define SHIELD_DCS 6 // VS1053 Data/command select pin (output)
#define DREQ 3       // VS1053 Data request, ideally an Interrupt pin
#define CARDCS 4     // SD chip select pin
#define RST_PIN 9    // MFRC522 reset pin
#define SS_PIN 10    // MFRC522 chip select pin

#define SHIELD_RESET -1 // VS1053 reset pin (unused!)

// define communication settings
#define BAUDRATE 38400 // baudrate

//custom type definitions
struct nfcTagData // struct to hold NFC tag data
{
  uint8_t cookie;   // byte 0 nfc cookie to identify the nfc tag to belong to the player
  char pname[28];   // byte 1-28 char array to hold the path to the folder
  uint8_t trackcnt; // byte 29 number of tracks in the specific folder
  uint8_t mode;     // byte 30play mode assigne to the nfcTag
  uint8_t special;  // byte 31 track or function for admin nfcTags
};

struct playDataInfo     //
{
  uint8_t mode;           // play mode
  uint8_t pathLine;       // line number of the current play path in the index file
  uint8_t trackcnt;       // track count of 
  uint8_t currentTrack;   // current track
  char dirname[37];       // buffer to hold current dirname
  char fname[13];         // buffer to hold current file name
  char fullname[50];      // general buffer used throught the program
};

// function definition
void indexDirectoryToFile(File dir, File *indexFile);
static void nextTrack(uint16_t track);
int voiceMenu(int numberOfOptions, int startMessage,
              int messageOffset, bool preview = false,
              int previewFromFolder = 0); // voice menu for setting up device
void resetCard();                         // resets a card
void setupCard();                         // first time setup of a card
bool readCard(nfcTagData *dataIn);        // reads card content and save it in nfcTagObject
void writeCard(nfcTagData *dataOut);      // writes card content from nfcTagObject
void findPath(playDataInfo *playData);
void getTrackName(playDataInfo *playData);
void playNext(playDataInfo *playData);

// helper function for development
void dump_byte_array(byte *dumpbuffer, byte dumpbufferSize); // dump a byte aray as hex to the Serial port

// instanciate global objects
// create instance of musicPlayer object
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(SHIELD_RESET, SHIELD_CS, SHIELD_DCS, DREQ, CARDCS);

// create instance of MFRC522 object
MFRC522 mfrc522(SS_PIN, RST_PIN);

// global variables
File root;           // root file
File indexfile;      // indexfile
SdFat SD;            // file system object
//char dirname[50];    // buffer to hold current dirname
char buffer[50];     // general buffer used throught the program
uint8_t volume = 40; // settings of amplifier
//uint8_t currentTrack;
nfcTagData dataIn;
ifstream sdin; // input stream for searching in indexfile

// NFC management
bool tagStatus = false;     // tagStatus=true, tag is present, tagStatus=false no tag present
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
      ; // don't do anything more
  }
  Serial.println(F("initialization done."));

  /*------------------------
  startup program
  ------------------------*/

  // index SD card //HAS TO BE MOVED TO AN EXTRA FUNCTION LATER ON SINCE IT DOESN'T MAKE SENCE TO BUILD INDEX EVERYTIME
  if (SD.exists("/index.txt")) // remove existing index file from SD card
  {
    SD.remove("/index.txt");
  }
  indexfile = SD.open("/index.txt", FILE_WRITE); // open new indexfile on SD card
  if (indexfile)
  {
    Serial.println(F("Index File opend, start indexing"));
  }
  else
  {
    Serial.println(F("error opening Index File"));
  }
  //strcpy(dirname, "/MUSIC");
  root = SD.open("/MUSIC");
  indexDirectoryToFile(root, &indexfile);
  indexfile.close();
  Serial.println(F("Indexed SD card"));
  root.close();

  // initialize random number geneartor by reading analog value from A0
  randomSeed(analogRead(A0));
}

/* Main Loop of program
 *  
 */

void loop()
{
  playDataInfo playData;
  // serial IF handling
  // i = index SD card for music folders and mp3 files
  // s = STOP / START
  // p = PAUSE/PLAY
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
      //index filesystem
    }
    if (c == 's')
    {
      if (musicPlayer.stopped())
      {
        //currentTrack.seek(0);
        Serial.print(F("Start"));
        //Serial.print(currentTrack.name());
        //Serial.print("  Current Position: ");
        //Serial.println(currentTrack.position());
        //rewind(&currentTrack);
        //musicPlayer.startPlayingFile(currentTrack);
      }
      else
      {
        Serial.print(F("Stop"));
        musicPlayer.stopPlaying();
        //Serial.println(currentTrack.name());
      }
    }

    // if we get an 'p' on the serial console, pause/unpause!
    if (c == 'p')
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
    if (c == 'n')
    {
      musicPlayer.stopPlaying();
      playNext(&playData);
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

      //if (dataIn.cookie != 857536)
      if (dataIn.cookie != 42)
      {
        // tag is not know, init it with demo data
        Serial.println(F("Unknown Card"));

        nfcTagData testdata = {42,             // cookie to identify the nfc tag to belong to the player
                               "AIR/10000H~1", // Byte1-28 char array to hold the path to the folder
                               11,             // Byte29 number of tracks in the specific folder
                               1,              // Byte30 play mode assigne to the nfcTag
                               1};             // Byte31 track or function for admin nfcTags

        // write NFC Data
        writeCard(&testdata);
      }
      else
      {
        //tag is known
        
        Serial.println(F("known card"));
        Serial.print(F("Path Name"));
        Serial.println(dataIn.pname);
        Serial.print(F("Trackcount: "));
        Serial.println(dataIn.trackcnt);
        
        strcpy(playData.dirname, "/MUSIC/");
        strcat(playData.dirname, dataIn.pname);

        // search linenumber of path in indexfile
        findPath(&playData);
        playData.trackcnt=dataIn.trackcnt;
        playData.currentTrack=1;
        playData.mode=dataIn.mode;

        getTrackName(&playData);
        strcpy(buffer, playData.dirname);
        strcat(buffer, "/");
        strcat(buffer, playData.fname);
        musicPlayer.startPlayingFile(buffer);
      }
    }
    else // nfc card removed
    {
      Serial.println(F("Card removed"));
      musicPlayer.stopPlaying();
    }
  }
  // if player is stopped start player
  /*if (musicPlayer.stopped())
  {
    Serial.println("No track playing");
    if (musicPlayer.startPlayingFile(currentTrack))
    {
      Serial.print(F("Started Playing: "));
      Serial.println(currentTrack.name());
    }
    else
    {
      Serial.println(F("Failed to start"));
    }
  }*/

  // no card present stop playing music
  /*if (!musicPlayer.stopped())
  {
    musicPlayer.stopPlaying();
    
  }*/
  delay(500);
  Serial.print(".");
}

/* support functions
==========================================================================================
*/

int voiceMenu(int numberOfOptions, int startMessage, int messageOffset,
              bool preview = false, int previewFromFolder = 0)
{
  int returnValue = 0;
  /*
  if (startMessage != 0)
    mp3.playMp3FolderTrack(startMessage);
  do {
    pauseButton.read();
    upButton.read();
    downButton.read();
    mp3.loop();
    if (pauseButton.wasPressed()) {
      if (returnValue != 0)
        return returnValue;
      delay(1000);
    }

    if (upButton.pressedFor(LONG_PRESS)) {
      returnValue = min(returnValue + 10, numberOfOptions);
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
      ignoreUpButton = true;
    } else if (upButton.wasReleased()) {
      if (!ignoreUpButton) {
        returnValue = min(returnValue + 1, numberOfOptions);
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
        ignoreUpButton = false;
    }
    
    if (downButton.pressedFor(LONG_PRESS)) {
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
      ignoreDownButton = true;
    } else if (downButton.wasReleased()) {
      if (!ignoreDownButton) {
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
        ignoreDownButton = false;
    }
  } while (true);
  */
}

void resetCard()
{
  /*do {
    pauseButton.read();
    upButton.read();
    downButton.read();

    if (upButton.wasReleased() || downButton.wasReleased()) {
      Serial.print(F("Abgebrochen!"));
      mp3.playMp3FolderTrack(802);
      return;
    }
  } while (!mfrc522.PICC_IsNewCardPresent());

  if (!mfrc522.PICC_ReadCardSerial())
    return;

  Serial.print(F("Karte wird neu Konfiguriert!"));
  setupCard();*/
}

/*void setupCard()
{
  musicPlayer.pausePlaying(true);
  Serial.print(F("Neue Karte konfigurieren"));

  // Ordner abfragen
  // for dev: myCard.folder = voiceMenu(99, 300, 0, true);
  myCard.folder = 1;
  // Wiedergabemodus abfragen
  // for dev: myCard.mode = voiceMenu(6, 310, 310);
  myCard.mode = 1;

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
  writeCard(myCard);
}
*/

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
void findPath(playDataInfo *playData)
{
  uint8_t trackcnt = 0;
  uint8_t line_number =0;
  sdin.open("/index.txt"); // open indexfile
  sdin.seekg(0);           // go to file position 0
  
  Serial.print("Find Path Name: ");
  Serial.println(playData->dirname);
  while (true)
  {
    if (!sdin.getline(buffer, 50, '\n'))
      break;
    if (strstr(buffer, playData->dirname))
    {
      Serial.print("found at line: ");
      //Serial.println(buffer);

      // read out track number, not required here but for testing purposes
      // this will be necessary in adding a card
      char *pch;
      pch = strtok(buffer, "\t");
      pch = strtok(NULL, "\t");
      int trackcnt = pch - '0';
      //Serial.println(pch);

      break;
    }
    ++line_number;
  }
  playData->pathLine = line_number;
  sdin.close();
}

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
  sdin.close();
}

void playNext(playDataInfo *playData)
{
  playData->currentTrack = playData->currentTrack+1;
  Serial.println(playData->currentTrack,DEC);
  if (playData->currentTrack <= playData->trackcnt)
  {
    getTrackName(playData);
    strcpy(buffer, playData->dirname);
    strcat(buffer, "/");
    strcat(buffer, playData->fname);
    Serial.println(buffer);
    if (musicPlayer.startPlayingFile(buffer))
    {
      Serial.println(F("Started Playing"));
    }
    else
    {
      Serial.println(F("Failed to start playing"));
    }
  }
}

void playPrevious()
{
}

void playRandom()
{
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
        //Serial.println(dirname);
        indexFile->write('\t');
        //indexFile->print(trackcnt, DEC);
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