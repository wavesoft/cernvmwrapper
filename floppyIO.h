//  This file is part Floppy I/O, a Virtual Machine - Hypervisor intercommunication system.
//  Copyright (C) 2011 Ioannis Charalampidis 
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

// -------------------------------------------------------------------
//  File:   FloppyIO.h
//  Author: Ioannis Charalampidis <ioannis.charalampidis AT cern DOT ch>
//  License: GNU Lesser General Public License - Version 3.0
// -------------------------------------------------------------------
// 
//  Hypervisor-Virtual machine bi-directional communication
//  through floppy disk.
// 
//  This class provides the hypervisor-side of the script.
//  For the guest-side, check the perl scripts that
//  were available with this code.
//  
//  Here is the layout of the floppy disk image (Example of 28k):
// 
// +-----------------+------------------------------------------------+
// |     0x0001      |  "Data available for hypervisor" flag byte     |
// | 0x0001 - 0x3800 |  Hypervisor -> Guest Buffer                    |
// | 0x3801 - 0x6FFF |  Guest -> Hypervisor Buffer                    |
// |     0x7000      |  "Data available for guest" flag byte          |
// +-----------------+------------------------------------------------+
//
//  Updated at January 5, 2012, 13:06 PM
//

#ifndef FLOPPYIO_H
#define	FLOPPYIO_H

#include <iostream>
#include <sstream>
#include <fstream>

using namespace std;

// FloppyIO Version
#define FPIO_VERSION  0,2

// Do not initialize (reset) floppy disk image at open.
// (Flag used by the FloppyIO constructor)
#define FPIO_NOINIT 1

// Do not create the filename (assume it exists)
// (Flag used by the FloppyIO constructor)
#define FPIO_NOCREATE 2

// Synchronize I/O.
// This flag will block the script until the guest has read/written the data.
// (Flag used by the FloppyIO constructor)
#define FPIO_SYNCHRONIZED 4

// Use exceptions instead of error codes
// (Flag used by the FloppyIO constructor)
#define FPIO_EXCEPTIONS 8

// Initialize FloppyIO in client mode.
// This flag will swap the input/output buffers, making the script usable from
// within the virtual machine.
// (Flag used by the FloppyIO constructor)
#define FPIO_CLIENT 16

// Enable binary transfers.
// This flag enables binary transfers by prefixing the data with a 4-byte data-length
// number, rather than relying on the null-termination byte.
// (Flag used by the FloppyIO constructor)
#define FPIO_BINARY 32

//
// Error code constants
//
#define FPIO_NOERR          0  // No error occured
#define FPIO_ERR_IO        -1  // There was an I/O error on the strea,
#define FPIO_ERR_TIMEOUT   -2  // The operation timed out
#define FPIO_ERR_CREATE    -3  // Unable to freate the floppy file
#define FPIO_ERR_NOTREADY  -4  // The I/O object is not ready
#define FPIO_ERR_INPUT     -5  // Error while reading from input (ex. input stream)
#define FPIO_ERR_ABORTED   -6  // An operation was aborted from the remote end

//
// Structure of the synchronization control byte.
//
// This byte usually resides at the beginning of the
// floppy file for the receive buffer and at the end
// of the file for the sending buffer.
//
// It's purpose is to force the entire floppy image
// to be re-written/re-read by the hypervisor/guest OS and
// to synchronize the I/O in case of large ammount of
// data being exchanged.
//
typedef struct fpio_ctlbyte {
    unsigned short bDataPresent   : 1;
    unsigned short bEndOfData     : 1;
    unsigned short bLengthPrefix  : 1;
    unsigned short bAborted       : 1;
    unsigned short usID           : 4;
};


// Default floppy disk size (In bytes)
// 
// VirtualBox complains if bigger than 28K
// It's supposed to go till 1474560 however (!.44 Mb)

#define DEFAULT_FIO_FLOPPY_SIZE 28672

// Default synchronization timeout (seconds).
// This constant defines how long we should wait for synchronization
// feedback from the guest before aborting.

#define DEFAULT_FIO_SYNC_TIMEOUT 5

//
// Floppy I/O Communication class
//
class FloppyIO {
public:
    
    // Construcors
    FloppyIO(const char * filename, int flags = 0);
    virtual ~FloppyIO();
    
    // Functions
    void        reset();
    int         send(string strData, fpio_ctlbyte * ctrlByte = NULL);
    int         send(char * data, int dataLen, fpio_ctlbyte * ctrlByte = NULL);
    int         send(istream * stream);
    string      receive();
    int         receive(char * data, int dataLen, fpio_ctlbyte * ctrlByte = NULL);
    int         receive(string * strBuffer, fpio_ctlbyte * ctrlByte = NULL);
    int         receive(ostream * stream);
    
    // Topology info
    int         ofsInput;   // Input buffer offset & size
    int         szInput;
    int         ofsOutput;  // Output buffer offset & size
    int         szOutput;
    
    int         ofsCtrlByteIn;  // Control byte offset for input
    int         ofsCtrlByteOut; // Control byte offset for output
    
    // Synchronization stuff
    bool        synchronized;   // The read/writes are synchronized
    int         syncTimeout;    // For how long should we wait

    // Error reporting and checking
    int         error;
    string      errorStr;
    bool        useExceptions;  // If TRUE errors will raise exceptions
    
    void        clear();        // Clear errors
    bool        ready();        // Returns TRUE if there are no errors

private:

    // Floppy Info
    fstream *   fIO;
    int         szFloppy;

    // Re-open information
    char *      openName;
    int         openFlags;

    // Use binary data transfer
    // (Cannot be changed during run-time)
    bool        binary;

    // Functions
    int         waitForSync(int controlByteOffset, int timeout, char state, char mask = 0xff);
    int         setError(int code, string message);
    int         flush();
    
};

//
// Floppy I/O Exceptions
//
class FloppyIOException: public exception {
public:

    int         code;
    string      message;

    // Default constructor/destructor
    FloppyIOException() { this->code=0; this->message=""; };
    virtual ~FloppyIOException() throw() { };

    // Get description
    virtual const char* what() const throw() {
        static ostringstream oss (ostringstream::out);
        oss << this->message << ". Error code = " << this->code;
        return oss.str().c_str();
    }

    // Change the message and return my instance
    // (Used for singleton format)
    virtual FloppyIOException * set(int code, string message) {
        this->code = code;
        this->message = message;
        return this;
    }
    
};

#endif	// FLOPPYIO_H 

