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

//  File:   FloppyIO.h
//  Author: Ioannis Charalampidis <ioannis.charalampidis AT cern DOT ch>
//  License: GNU Lesser General Public License - Version 3.0
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
//  +-----------------+------------------------------------------------+
//  | 0x0000 - 0x37FF |  Hypervisor -> Guest Buffer                    |
//  | 0x3800 - 0x6FFE |  Guest -> Hypervisor Buffer                    |
//  |     0x6FFF      |  "Data available for guest" flag byte          |
//  |     0x7000      |  "Data available for hypervisor" flag byte     |
//  +-----------------+------------------------------------------------+
//
//  Updated at January 5, 2012, 13:06 PM
//

#ifndef FLOPPYIO_H
#define	FLOPPYIO_H

#include <iostream>
#include <fstream>
#include <string.h>

using namespace std;

// Do not initialize (reset) floppy disk image at open.
// (Flag used by the FloppyIO constructor)
#define F_NOINIT 1

// Do not create the filename (assume it exists)
// (Flag used by the FloppyIO constructor)
#define F_NOCREATE 2

// Synchronize I/O.
// This flag will block the script until the guest has read/written the data.
// (Flag used by the FloppyIO constructor)
#define F_SYNCHRONIZED 4

// Use exceptions instead of error codes
// (Flag used by the FloppyIO constructor)
#define F_EXCEPTIONS 8

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
    unsigned short bDataPresent  : 1;
    unsigned short bReserved     : 7;
};

// Default floppy disk size (In bytes)
// 
// VirtualBox complains if bigger than 28K
// It's supposed to go till 1474560 however (!.44 Mb)

#define DEFAULT_FLOPPY_SIZE 28672


// Floppy I/O Communication class

class FloppyIO {
public:
    
    // Construcors
    FloppyIO(const char * filename);
    FloppyIO(const char * filename, int flags);
    virtual ~FloppyIO();
    
    // Functions
    void        reset();
    int         send(string strData);
    string      receive();
    int         receive(string * strBuffer);
    
    // Topology info
    int     ofsInput;   // Input buffer offset & size
    int     szInput;
    int     ofsOutput;  // Output buffer offset & size
    int     szOutput;
    
    int     ofsCtrlByteIn;  // Control byte offset for input
    int     ofsCtrlByteOut; // Control byte offset for output

    // Last error code
    int     error;

private:

    // Floppy Info
    fstream * fIO;
    int     szFloppy;

    // Functions
    int     waitForSync(int controlByteOffset, int timeout);
    
};

#endif	// FLOPPYIO_H 

