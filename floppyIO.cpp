// This file is part Floppy I/O, a Virtual Machine - Hypervisor intercommunication system.
// Copyright (C) 2011 Ioannis Charalampidis 
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// File:   FloppyIO.cpp
// Author: Ioannis Charalampidis <ioannis.charalampidis AT cern DOT ch>
// License: GNU Lesser General Public License - Version 3.0
//
// Hypervisor-Virtual machine bi-directional communication
// through floppy disk.
//
// This class provides the hypervisor-side of the script.
// For the guest-side, check the perl scripts that
// were available with this code.
// 
// Here is the layout of the floppy disk image (Example of 28k):
//
// +-----------------+------------------------------------------------+
// | 0x0000 - 0x37FF |  Hypervisor -> Guest Buffer                    |
// | 0x3800 - 0x6FFE |  Guest -> Hypervisor Buffer                    |
// |     0x6FFF      |  "Data available for guest" flag byte          |
// |     0x7000      |  "Data available for hypervisor" flag byte     |
// +-----------------+------------------------------------------------+
// 
// Updated at January 5, 2012, 13:06 PM

#include "floppyIO.h"


// Floppy file constructor
// 
// This constructor opens the specified floppy disk image, fills everything
// with zeroes and initializes the topology variables.
// 
// @param filename The filename of the floppy disk image

FloppyIO::FloppyIO(const char * filename) {
    
  // Open file
  fstream *fIO = new fstream(filename, fstream::in | fstream::out | fstream::trunc);

  // Prepare floppy info
  this->fIO = fIO;
  this->szFloppy = DEFAULT_FLOPPY_SIZE;
  
  // Setup offsets and sizes of the I/O parts
  this->szOutput = this->szFloppy/2-1;
  this->ofsOutput = 0;
  this->szInput = this->szOutput;
  this->ofsInput = this->szOutput;
  this->ofsCtrlByteOut = this->szInput+this->szOutput;
  this->ofsCtrlByteIn = this->szInput+this->szOutput+1;
  
  // Reset floppy file
  this->reset();

}

// Advanced Floppy file constructor
// 
// This constructor allows you to open a floppy disk image with extra flags.
// 
// F_NOINIT         Disables the reseting of the image file at open
// F_NOCREATE       Does not truncate the file at open (If not exists, the file will be created)
// F_SYNCHRONIZED   The communication is synchronized, meaning that the code will block until the 
//                  data are read/written from the guest. [NOT YET IMPLEMENTED]
// 
// @param filename The filename of the floppy disk image

FloppyIO::FloppyIO(const char * filename, int flags) {
    
  // Open file
  ios_base::openmode fOpenFlags = fstream::in | fstream::out;
  if ((flags & F_NOCREATE) == 0) fOpenFlags = fstream::trunc;
  fstream *fIO = new fstream(filename, fOpenFlags);
  
  // Check for errors while F_NOCREATE is there
  if ((flags & F_NOCREATE) != 0) {
      if ( (fIO->rdstate() & ifstream::failbit ) != 0 ) {
          
          // Clear error flag
          fIO->clear();
          
          // Try to create file
          fOpenFlags |= fstream::trunc;
          fIO->open(filename, fOpenFlags);
          
          // Still errors?
          if ( (fIO->rdstate() & ifstream::failbit ) != 0 ) {
            cerr << "Error opening '" << filename << "'!\n";
            return;
          }
          
          // Managed to open it? Reset it...
          flags &= ~F_NOINIT;
      }
          
  } else {

      // Check for failures on open
      if ( (fIO->rdstate() & ifstream::failbit ) != 0 ) {


        
      }

  }
  
  // Prepare floppy info
  this->fIO = fIO;
  this->szFloppy = DEFAULT_FLOPPY_SIZE;
  
  // Setup offsets and sizes of the I/O parts
  this->szOutput = this->szFloppy/2-1;
  this->ofsOutput = 0;
  this->szInput = this->szOutput;
  this->ofsInput = this->szOutput;
  this->ofsCtrlByteOut = this->szInput+this->szOutput;
  this->ofsCtrlByteIn = this->szInput+this->szOutput+1;
  
  // Reset floppy file
  if ((flags & F_NOINIT) == 0) this->reset();

}


// FloppyIO Destructor
// Closes the file descriptor and releases used memory

FloppyIO::~FloppyIO() {
    // Close file
    this->fIO->close();
    
    // Release memory
    delete this->fIO;
}

// Reset the floppy disk image
// This function zeroes-out the contents of the FD image
 
void FloppyIO::reset() {
  this->fIO->seekp(0);
  char * buffer = new char[this->szFloppy];
  this->fIO->write(buffer, this->szFloppy);
  delete[] buffer;      
}


// Send data to the floppy image I/O
//
// @param strData   The string to send
// @return          The number of bytes sent if successful or -1 if an error occured.
//
int FloppyIO::send(string strData) {
    // Prepare send buffer
    char * dataToSend = new char[this->szOutput];
    memset(dataToSend, 0, this->szOutput);
    
    // Initialize variables
    int szData = strData.length();
    int szPad = 0;
    int bytesSent = 0;
    
    // Copy the first szInput bytes
    if (szData > this->szOutput-1) { // -1 for the null-termination
        // Data more than the pad size? Trim...
        strData.copy(dataToSend, this->szOutput-1, 0);
    } else {
        // Else, copy the string to send buffer
        strData.copy(dataToSend, szData, 0);
    }
    
    // Check for stream status
    if (!this->fIO->good()) return -1;
    
    // Write the data to file
    this->fIO->seekp(this->ofsOutput);
    bytesSent = this->fIO->write(dataToSend, this->szOutput);
    
    // Check if something went wrong after writing
    if (!this->fIO->good()) return -1;
    
    // Notify the client that we placed data (Client should clear this on read)
    this->fIO->seekp(this->ofsCtrlByteOut);
    this->fIO->write("\x01", 1);

    // Return number of bytes sent
    return bytesSent;
    
}


// Receive the input buffer contents
// @return Returns a string object with the file contents

string FloppyIO::receive() {
    static string ansBuffer;
    char * dataToReceive = new char[this->szInput];
    int dataLength = this->szInput;
    
    // Find the size of the input string
    for (int i=0; i<this->szInput; i++) {
        if (dataToReceive[0] == '\0') {
            dataLength=i;
            break;
        }
    }
    
    // Read the input bytes from FD
    this->fIO->seekg(this->ofsInput, ios_base::beg);
    this->fIO->read(dataToReceive, this->szInput);
    
    // Notify the client that we have read the data
    this->fIO->seekp(this->ofsCtrlByteIn);
    this->fIO->write("\x00", 1);
    
    // Copy input data to string object
    ansBuffer = dataToReceive;
    return ansBuffer;
    
}

// Wait for synchronization byte to be cleared.
// This function blocks until the byte at controlByteOffset has
// the synchronization bit cleared.
//
// @param controlByteOffset The offset (from the beginning of file) where to look for the control byte
// @param timeout           The time (in seconds) to wait for a change. 0 Will wait forever
// @return                  Returns 0 if everything succeeded, -1 if an error occured, -2 if timed out.

int  FloppyIO::waitForSync(int controlByteOffset, int timeout) {
    time_t tExpired = time (NULL) + timeout;
    char cStatusByte;

    // Wait until expired or forever.
    while ((timeout == 0) || ( time(NULL) <= tExpired)) {

        // Check for stream status
        if (!this->fIO->good()) return -1;

        // Check the synchronization byte
        this->fIO->seekg(controlByteOffset, ios_base::beg);
        this->fIO->read(&cStatusByte, 1);

        // Is the control byte 0? Our job is finished...
        if (cStatusByte == "\x00") return 0;
    }

    // If we reached this point, we timed out
    return -2;

}


