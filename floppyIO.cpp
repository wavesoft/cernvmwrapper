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

//
// An I/O union structure for the data-length prefix
//
// This union helps accessing the size int and it's 4-byte representation
// without needing any memcpy.
//
union fpio_datalen_io {
    int          size;
    char         bytes[4];
};

// Advanced Floppy file constructor
// 
// This constructor allows you to open a floppy disk image with extra flags.
// 
// FPIO_NOINIT         Disables the reseting of the image file at open
// FPIO_NOCREATE       Does not truncate the file at open (If not exists, the file will be created)
// FPIO_SYNCHRONIZED   The communication is synchronized, meaning that the code will block until the 
//                  data are read/written from the guest.
// FPIO_EXCEPTIONS     Throw exceptions if something goes wrong.
// FPIO_CLIENT         Swap in/out buffers for use from within the guest.
// 
// @param filename  The filename of the floppy disk image
// @param flags     The flags that define how the FloppyIO class should function
//

FloppyIO::FloppyIO(const char * filename, int flags) {
  // Clear error flag
  this->error = 0;
  
  // Prepare open flags and create file stream
  ios_base::openmode fOpenFlags = fstream::in | fstream::out;
  if ((flags & FPIO_NOCREATE) == 0) fOpenFlags |= fstream::trunc;
  if ((flags & FPIO_BINARY) != 0)  fOpenFlags |= fstream::binary;
  fstream *fIO = new fstream( );
  this->fIO = fIO;
  
  // Enable exceptions on fIO if told so
  if ((flags & FPIO_EXCEPTIONS) != 0) {
    fIO->exceptions( ifstream::failbit | ifstream::badbit );
    this->useExceptions=true;
  } else {
    this->useExceptions=false;
  }
  
  // Try to open the file
  fIO->open(filename, fOpenFlags);

  // Check for errors while FPIO_NOCREATE is there
  if ((flags & FPIO_NOCREATE) != 0) {
      if ( fIO->fail() ) {
          
          // Clear error flags
          fIO->clear();
          
          // Try to create file
          fOpenFlags |= fstream::trunc;
          fIO->open(filename, fOpenFlags);
          
          // Still errors?
          if ( fIO->fail() ) {
            this->setError(-3, "Error while creating floppy I/O file, because it wasn't found even though FPIO_NOCREATE was specified!"); 
            return;
          }
          
          // Managed to open it? Reset it...
          flags &= ~FPIO_NOINIT;
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
  if ((flags & FPIO_CLIENT) != 0) {
    // Guest mode
    this->szOutput = this->szFloppy/2-1;
    this->szInput = this->szOutput;
    this->ofsOutput = this->szInput;
    this->ofsInput = 0;
    this->ofsCtrlByteIn = this->szInput+this->szOutput;
    this->ofsCtrlByteOut = this->szInput+this->szOutput+1;

    cerr << "FPIO Started in client mode\n";
    
  } else {
    // Hypervisor mode
    this->szOutput = this->szFloppy/2-1;
    this->szInput = this->szOutput;
    this->ofsOutput = 0;
    this->ofsInput = this->szOutput;
    this->ofsCtrlByteOut = this->szInput+this->szOutput;
    this->ofsCtrlByteIn = this->szInput+this->szOutput+1;

    cerr << "FPIO Started in hypervisor mode\n";
  }
    
  // Update synchronization flags
  this->synchronized = false;
  this->syncTimeout = DEFAULT_FIO_SYNC_TIMEOUT;
  if ((flags & FPIO_SYNCHRONIZED) != 0) this->synchronized=true;

  // Update binary flags
  this->binary = false;
  if ((flags & FPIO_BINARY) != 0) {

    // Reduce the I/O buffers by 4 bytes (used by the data-length prefix)
    this->szInput -= 3;
    this->szOutput -= 3; // 3 = 4 bytes - 1 null-termination (not used in binary mode)

    // Enable binary transfers
    this->binary=true;
    
  }
  
  // Reset floppy file
  if ((flags & FPIO_NOINIT) == 0) this->reset();

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
  char * buffer = new char[this->szFloppy+1];
  memset(buffer, 0, this->szFloppy+1);
  this->fIO->write(buffer, this->szFloppy+1);
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

    // Initialize variables
    int szData = strData.length();
    
    // Copy the first szInput bytes
    if (szData > this->szOutput-1) { // -1 for the null-termination
        // Data more than the pad size? Trim...
        strData.copy(dataToSend, this->szOutput-1, 0);
        szData = this->szOutput-1;
        
    } else {
        // Else, copy the string to send buffer
        strData.copy(dataToSend, szData, 0);
    }

    // And send the data
    this->send(dataToSend, szData, ctrlByte);
}

// Send data to the floppy image I/O using char buffer
//
// @param data      A pointer to the data buffer to send
// @param dataLen   The size of the input buffer
// @param ctrlByte  The extra parameters you want to write on the control byte
// @return          The number of bytes sent if successful or -1 if an error occured.
//
int FloppyIO::send(char * dataToSend, int szData, fpio_ctlbyte * ctrlByte) {
    
    // Check for ready state
    if (!this->ready()) return this->setError(-4, "Stream is not ready!");

    // Initialize variables
    int szPad = 0;
    int bytesSent = szData;

    // Wrap overflow
    if (szData > this->szOutput-1)
        szData = this->szOutput-1; // -1 for the null-termination
    
    // Check for stream status
    if (!this->fIO->good()) return this->setError(-1, "I/O Stream reported no-good state while sending!");

    // Prepare control byte
    fpio_ctlbyte_io cB; cB.byte=1;
    if (ctrlByte != NULL) {
        cB.flags=*ctrlByte;
        cB.flags.bDataPresent = 1;
    }
    
    // Move pointer to output
    this->fIO->seekp(this->ofsOutput);

    // Check if we should prefix the data
    if (this->binary) {
        fpio_datalen_io dL;
        cB.flags.bLengthPrefix = 1;    // Update control byte : set 'we have length prefix'
        dL.size = szData;              // Set the data length int
        this->fIO->write(dL.bytes, 4); // And send the 4-byte representation
        cerr << "Binary mode selected. Prefixing: " << dL.size << "\n";
    }

    // Send the data
    this->fIO->write(dataToSend, szData);
    
    // Check if something went wrong after writing
    if (!this->fIO->good()) return this->setError(-1, "I/O Stream reported no-good state while sending!");
    
    // Notify the client that we placed data (Client should clear this on read)
    this->fIO->seekp(this->ofsCtrlByteOut);
    this->fIO->write(&cB.byte, 1);
    this->fIO->flush();

    cerr << "Just sent in sync at " << this->ofsCtrlByteOut << " value= " << (int)cB.byte << "\n";

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
    int bLen;

    bLen = this->receive(dataToReceive, dataLength, ctrlByte);
    if (bLen<0) return bLen;

    ansBuffer->assign(dataToReceive, bLen);
    return bLen;
}

//
// Receive the input buffer contents using char buffers
//
// @param dataToReceive     A pointer to a char buffer that will receive the data
// @param szData            The size of the target buffer
// @param ctrlByte          The extra parameters received from the control byte
// @return                  Returns the length of the data received or -1 if an error occured.
//
//
int FloppyIO::receive(char * dataToReceive, int szData, fpio_ctlbyte * ctrlByte) {
    int dataLength = szData;

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

    // Prepare the control byte
    fpio_ctlbyte_io cB; cB.byte=0;
    this->fIO->seekg(this->ofsCtrlByteIn);
    this->fIO->read(&cB.byte, 1);

    cerr << "Got control byte: " << (int)cB.byte << "\n";

    // Update control byte if we have it specified
    if (ctrlByte != NULL) *ctrlByte = cB.flags;

    cB.flags.bDataPresent = 0;      // We need to say we read the data (when we are done)
    
    // Go to the input buffer
    this->fIO->seekg(this->ofsInput, ios_base::beg);

    // If we are using binary mode and we have data prefix, read it
    if (this->binary && (cB.flags.bLengthPrefix == 1)) {
        fpio_datalen_io dL;
        this->fIO->read(dL.bytes, 4);  // Read the 4-byte representation
        cerr << "Binary mode detected. Read: " << dL.size << "\n";
        dataLength = dL.size;
        if (dataLength > this->szInput) 
            dataLength = this->szInput; // Protect from overflows
    }

    // Now read the appropriate data length    
    this->fIO->read(dataToReceive, dataLength);

    // Locate the end of the char buffer using null-termination if we are not using binary mode
    if (!this->binary || !cB.flags.bLengthPrefix)
        dataLength = strlen(dataToReceive);

    // Notify the client that we have read the data
    this->fIO->seekp(this->ofsCtrlByteIn);
    this->fIO->write(&cB.byte, 1);
    this->fIO->flush();

    cerr << "Just sent out sync at " << this->ofsCtrlByteIn << " value= " << (int)cB.byte << "\n";

    // Return the data length
    return dataLength;
    
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

        // Did we got a data failure?
        if (cB.bAborted==1) {
            stream->flush();
            stream->setstate(ostream::failbit);
            return FPIO_ERR_ABORTED;
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
    fpio_ctlbyte_io cBIO;
    fpio_ctlbyte * cB;
    char * inBuffer = new char[this->szOutput+1];
    int sentLength, rd, res;

    // Check if stream is not good
    if (!stream->good()) return this->setError(FPIO_ERR_INPUT, "Unable to open input stream!");

    // Reset byte and get a reference to (zeroed) flags
    cBIO.byte=0;
    cB = &cBIO.flags;

    // While stream is good, start processing
    while (stream->good()) {

        // Zero the input buffer (Plus one null-termination byte)
        memset(inBuffer, 0, this->szOutput+1);

        // Read data 
        stream->read(inBuffer, this->szOutput-1);
        rd = stream->gcount();

        // Check status
        if (stream->eof() || (stream->tellg() < 0)) {
            // EOF? Mark end-of-data on the current block
            cB->bEndOfData=1;

        } else if (stream->fail()) {
            // Got fail without getting eof? Something went wrong

            // Notify the remote end that we failed the transmittion
            cB->bAborted=1;
            cB->bEndOfData=1;
            res = this->send("", cB); // Send Zero data and the appropriate control bits

            // Return error            
            return this->setError(-5, "Unable to read from input stream");
            
        }

        // Count bytes written
        res = this->send(inBuffer, rd, cB);
        if (res<0) return res; // Error occured
        
        sentLength+=res;

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

    cerr << "Waiting for sync at " << controlByteOffset << " waiting for " << (int)state << " (Mask: " << (int)mask << ")\n";

    // Wait until expired or forever.
    while ((timeout == 0) || ( time(NULL) <= tExpired)) {

        // Check for stream status
        if (!this->fIO->good()) return this->setError(-1, "I/O Stream reported non-good state while waiting for sync!");
        
        // Check the synchronization byte
        this->fIO->seekg(controlByteOffset, ios_base::beg);
        this->fIO->read(&cStatusByte, 1);

        // Is the control byte 0? Our job is finished...
        if (((int)cStatusByte & (int)mask) == (int)state) return 0;

        // Sleep for a few milliseconds to decrease CPU-load
        usleep( FPIO_TUNE_SLEEP );
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



