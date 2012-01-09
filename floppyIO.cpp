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
#include <typeinfo>
#include <string.h>

// FloppyIO Exception singleton
static FloppyIOException   __FloppyIOExceptionSingleton;

//
// An I/O union structure for fpio_ctrlbyte
//
// This union helps accessing the fpio_ctrlbyte flags without the
// need of memset.
//
union fpio_ctlbyte_io {
    fpio_ctlbyte flags;
    char         byte;
};

// Advanced Floppy file constructor
// 
// This constructor allows you to open a floppy disk image with extra flags.
// 
// F_NOINIT         Disables the reseting of the image file at open
// F_NOCREATE       Does not truncate the file at open (If not exists, the file will be created)
// F_SYNCHRONIZED   The communication is synchronized, meaning that the code will block until the 
//                  data are read/written from the guest.
// F_EXCEPTIONS     Throw exceptions if something goes wrong.
// F_CLIENT         Swap in/out buffers for use from within the guest.
// 
// @param filename  The filename of the floppy disk image
// @param flags     The flags that define how the FloppyIO class should function
//

FloppyIO::FloppyIO(const char * filename, int flags) {
  // Clear error flag
  this->error = 0;
  
  // Prepare open flags and create file stream
  ios_base::openmode fOpenFlags = fstream::in | fstream::out;
  if ((flags & F_NOCREATE) == 0) fOpenFlags |= fstream::trunc;
  fstream *fIO = new fstream( );
  this->fIO = fIO;
  
  // Enable exceptions on fIO if told so
  if ((flags & F_EXCEPTIONS) != 0) {
    fIO->exceptions( ifstream::failbit | ifstream::badbit );
    this->useExceptions=true;
  } else {
    this->useExceptions=false;
  }
  
  // Try to open the file
  fIO->open(filename, fOpenFlags);

  // Check for errors while F_NOCREATE is there
  if ((flags & F_NOCREATE) != 0) {
      if ( fIO->fail() ) {
          
          // Clear error flags
          fIO->clear();
          
          // Try to create file
          fOpenFlags |= fstream::trunc;
          fIO->open(filename, fOpenFlags);
          
          // Still errors?
          if ( fIO->fail() ) {
            this->setError(-3, "Error while creating floppy I/O file, because it wasn't found even though F_NOCREATE was specified!"); 
            return;
          }
          
          // Managed to open it? Reset it...
          flags &= ~F_NOINIT;
      }
          
  } else {

      // Check for failures on open
      if ( fIO->fail() ) {
        this->setError(-3, "Error while creating floppy I/O file!");         
        return;
      }

  }
  
  // Prepare floppy info
  this->szFloppy = DEFAULT_FIO_FLOPPY_SIZE;
  
  // Setup offsets and sizes of the I/O parts
  if ((flags & F_CLIENT) != 0) {
    // Guest mode
    this->szOutput = this->szFloppy/2-1;
    this->szInput = this->szOutput;
    this->ofsOutput = this->szInput;
    this->ofsInput = 0;
    this->ofsCtrlByteIn = this->szInput+this->szOutput;
    this->ofsCtrlByteOut = this->szInput+this->szOutput+1;
    
  } else {
    // Hypervisor mode
    this->szOutput = this->szFloppy/2-1;
    this->szInput = this->szOutput;
    this->ofsOutput = 0;
    this->ofsInput = this->szOutput;
    this->ofsCtrlByteOut = this->szInput+this->szOutput;
    this->ofsCtrlByteIn = this->szInput+this->szOutput+1;
  }
    
  // Update synchronization flags
  this->synchronized = false;
  this->syncTimeout = DEFAULT_FIO_SYNC_TIMEOUT;
  if ((flags & F_SYNCHRONIZED) != 0) this->synchronized=true;
  
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
  // Check for ready state
  if (!this->ready()) {
    this->setError(-4, "Stream is not ready!");
    return;
  }
  
  // Reset to the beginnig of file and fill with zeroes
  this->fIO->seekp(0);
  char * buffer = new char[this->szFloppy];
  memset(buffer, 0, this->szFloppy);
  this->fIO->write(buffer, this->szFloppy);
  delete[] buffer;      
}


// Send data to the floppy image I/O
//
// @param strData   The string to send
// @param ctrlByte  The extra parameters you want to write on the control byte
// @return          The number of bytes sent if successful or -1 if an error occured.
//
// TODO: String is time-costly, add also the char*/size version
//
int FloppyIO::send(string strData, fpio_ctlbyte * ctrlByte) {
    // Prepare send buffer
    char * dataToSend = new char[this->szOutput];
    memset(dataToSend, 0, this->szOutput);
    
    // Check for ready state
    if (!this->ready()) return this->setError(-4, "Stream is not ready!");

    // Initialize variables
    int szData = strData.length();
    int szPad = 0;
    int bytesSent = szData;
    
    // Copy the first szInput bytes
    if (szData > this->szOutput-1) { // -1 for the null-termination
        // Data more than the pad size? Trim...
        strData.copy(dataToSend, this->szOutput-1, 0);
        bytesSent = this->szOutput-1;
    } else {
        // Else, copy the string to send buffer
        strData.copy(dataToSend, szData, 0);
    }
    
    // Check for stream status
    if (!this->fIO->good()) return this->setError(-1, "I/O Stream reported no-good state while sending!");
    
    // Write the data to file
    this->fIO->seekp(this->ofsOutput);
    this->fIO->write(dataToSend, this->szOutput);
    
    // Check if something went wrong after writing
    if (!this->fIO->good()) return this->setError(-1, "I/O Stream reported no-good state while sending!");

    // Prepare control byte
    fpio_ctlbyte_io cB; cB.byte=1;
    if (ctrlByte != NULL) {
        cB.flags=*ctrlByte;
        cB.flags.bDataPresent = 1;
    }
    
    // Notify the client that we placed data (Client should clear this on read)
    this->fIO->seekp(this->ofsCtrlByteOut);
    this->fIO->write(&cB.byte, 1);
    this->fIO->flush();

    // If synchronized, wait for data to be written
    if (this->synchronized) {
        // Wait for output control byte to become 0
        int iState = this->waitForSync(this->ofsCtrlByteOut, this->syncTimeout, 0, 0x01);
        if (iState<0) return iState;
    }

    // Return number of bytes sent
    return bytesSent;
    
}

//
// Receive the input buffer contents
//
// @return  Returns a string object with the buffer contents
//
string FloppyIO::receive() {
    static string ansBuffer;
    this->receive(&ansBuffer);
    return ansBuffer;
}

//
// Receive the input buffer contents
//
// @param string   A pointer to a string object that will receive the data
// @param ctrlByte The extra parameters received from the control byte
// @return         Returns the length of the data received or -1 if an error occured.
//
// TODO: String is time-costly, add also the char*/size version
//
int FloppyIO::receive(string * ansBuffer, fpio_ctlbyte * ctrlByte) {
    char * dataToReceive = new char[this->szInput];
    int dataLength = this->szInput;

    // Check for ready state
    if (!this->ready()) return this->setError(-4, "Stream is not ready!");

    // If synchronized, wait for input data
    if (this->synchronized) {
        // Wait for input control byte to become 1
        int iState = this->waitForSync(this->ofsCtrlByteIn, this->syncTimeout, 1, 0x01);
        if (iState<0) return iState;
    }
    
    // Check for stream status
    if (!this->fIO->good()) return this->setError(-1, "I/O Stream reported no-good state while receiving!");
    
    // Read the input bytes from FD
    this->fIO->seekg(this->ofsInput, ios_base::beg);
    this->fIO->read(dataToReceive, this->szInput);

    // Prepare the control byte
    fpio_ctlbyte_io cB; cB.byte=0;
    if (ctrlByte != NULL) {
        this->fIO->seekg(this->ofsCtrlByteIn);
        this->fIO->read(&cB.byte, 1);
        cB.flags.bDataPresent = 0;
    }

    // Notify the client that we have read the data
    this->fIO->seekp(this->ofsCtrlByteIn);
    this->fIO->write(&cB.byte, 1);
    this->fIO->flush();
    
    // Copy input data to string object
    *ansBuffer = dataToReceive;
    return ansBuffer->length();
    
}

//
// Receive contents and write them on an output stream
//
// @param ostream  A pointer to an output stream where the data should be placed.
// @return         Returns the length of the data received or -1 if an error occured.
//
// TODO: Do not rely on null-byte termination: Allow binary file transfer
//
int FloppyIO::receive(ostream * stream) {
    string data;
    fpio_ctlbyte cB;
    int readLength, rd;

    // Synchronized? Do proper stream reading..
    if (this->synchronized) {

        // Read data until end of data flag present on control byte
        cB.bEndOfData=0;
        while (cB.bEndOfData==0) {

            // Read and check for errors
            rd = this->receive(&data, &cB);
            if (rd<0) {
                stream->setstate(ostream::failbit);
                return rd;
            }

            // Write data
            stream->write(data.c_str(), data.size());
            readLength+=rd;
            
        }

    // Not synchronized? Read only one block
    } else {

        // Read one block and check for errors
        rd = this->receive(&data);
        if (rd<0) {
            stream->setstate(ostream::failbit);
            return rd;
        }

        // Write data
        stream->write(data.c_str(), data.size());
        readLength+=rd;
        
    }

    // Flush output
    stream->flush();
    
    return readLength;
}

//
// Send the contents of an input stream
//
// @param ostream  A pointer to an input stream that will fetch the data from. Non fixed-size streams are also accepted.
// @return         Returns the length of the data received or -1 if an error occured.
//
// TODO: Do not rely on null-byte termination: Allow binary file transfer
//
int FloppyIO::send(istream * stream) {
    string data;
    fpio_ctlbyte_io cBIO;
    fpio_ctlbyte * cB;
    char * inBuffer = new char[this->szOutput+1];
    int sentLength, rd;

    // Reset byte and get a reference to (zeroed) flags
    cBIO.byte=0;
    cB = &cBIO.flags;

    // While stream is good, start processing
    while (stream->good()) {

        // Zero the input buffer (Plus one null-termination byte)
        memset(inBuffer, 0, this->szOutput+1);

        // Read data 
        // TODO: Use this when you solve the binary-sending: rd=stream->tellg();
        stream->read(inBuffer, this->szOutput);

        // Check status
        if (stream->eof()) {
            // EOF? Mark end-of-data on the current block
            cB->bEndOfData=1;

        } else if (stream->fail()) {
            // Got fail without getting eof? Something went wrong
            return this->setError(-5, "Unable to read from input stream");
            
        }

        // Cast to string to use the send function
        // TODO: Time-costly, try to use char*
        data = inBuffer;

        // Count bytes written
        // TODO: Use this when you solve the binary-sending: rd= (int) stream->tellg() - rd;
        rd = this->send(data, cB);
        if (rd<0) return rd; // Error occured
        
        sentLength+=rd;

    }
    
    return sentLength;
}


// Wait for synchronization byte to be cleared.
// This function blocks until the byte at controlByteOffset has
// the synchronization bit cleared.
//
// @param controlByteOffset The offset (from the beginning of file) where to look for the control byte
// @param timeout           The time (in seconds) to wait for a change. 0 Will wait forever
// @param state             The state the controlByte must be for this function to exit.
// @param mask              The bit mask to apply on control byte before checking the state
// @return                  Returns 0 if everything succeeded, -1 if an error occured, -2 if timed out.

int  FloppyIO::waitForSync(int controlByteOffset, int timeout, char state, char mask) {
    time_t tExpired = time (NULL) + timeout;
    char cStatusByte;

    // Wait until expired or forever.
    while ((timeout == 0) || ( time(NULL) <= tExpired)) {

        // Check for stream status
        if (!this->fIO->good()) return this->setError(-1, "I/O Stream reported no-good state while waiting for sync!");

        // Check the synchronization byte
        this->fIO->seekg(controlByteOffset, ios_base::beg);
        this->fIO->read(&cStatusByte, 1);

        // Is the control byte 0? Our job is finished...
        if ((cStatusByte & mask) == state) return 0;

        // Sleep for a few milliseconds to decrease CPU-load
        usleep( 10000 );
    }

    // If we reached this point, we timed out
    return this->setError(-2, "Timed-out while waiting for sync!");

}

//
// Set last error.
// This is a short-hand function to update the error variables.
//
// @param   code    The error code
// @param   message The error message
// @return          The error code
//
int  FloppyIO::setError(int code, const string message) {
    this->error = code;

    // Chain errors
    if (this->errorStr.empty()) {
        this->errorStr = message;
    } else {
        this->errorStr = message + " (" + this->errorStr + ")";
    }

    // Should we raise an exception?
    if (this->useExceptions) 
        throw *__FloppyIOExceptionSingleton.set(code, message);

    // Otherwise return code
    // (Useful for using single-lined: return this->setError(-1, "message..');
    return code;
}

//
// Clear error state flags
//
void FloppyIO::clear() {
    this->error = 0;
    this->errorStr = "";
    this->fIO->clear();
}

//
// Check if everything is in ready state
// @return Returns true if there are no errors and stream hasn't failed
//
bool FloppyIO::ready() {
    if (this->error!=0) return false;
    return this->fIO->good();
}



